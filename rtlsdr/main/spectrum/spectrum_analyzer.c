#include "spectrum_analyzer.h"
#include "rtlsdr_usb.h"
#include "config.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "spectrum";

// ---------------------------------------------------------------------------
// 复数类型
// ---------------------------------------------------------------------------
typedef struct { float r, i; } cf32_t;

// ---------------------------------------------------------------------------
// 内部状态
// ---------------------------------------------------------------------------
static bool     s_active  = false;
static uint32_t s_freq_hz = SPECTRUM_DEFAULT_FREQ;
static uint32_t s_bw_hz   = SPECTRUM_DEFAULT_BW;

// FFT 缓冲区（静态分配，避免堆碎片）
static cf32_t   s_fft_buf[SPECTRUM_FFT_SIZE];
static float    s_window[SPECTRUM_FFT_SIZE];    // Hann 窗
static float    s_mag_db[SPECTRUM_FFT_SIZE];    // 幅度（dBFS）
static float    s_avg_db[SPECTRUM_FFT_SIZE];    // 平均幅度（指数平均）
static int      s_iq_idx  = 0;                  // IQ 样本计数

// 平均系数（指数移动平均）
#define AVG_ALPHA   0.3f    // 新帧权重

// WebSocket 客户端
static httpd_handle_t s_server = NULL;
static int  s_ws_fds[SPECTRUM_WS_MAX_CLIENTS];
static int  s_ws_count = 0;
static SemaphoreHandle_t s_ws_mutex = NULL;

// FFT 就绪信号
static SemaphoreHandle_t s_fft_ready = NULL;

// ---------------------------------------------------------------------------
// Hann 窗初始化
// ---------------------------------------------------------------------------
static void init_window(void)
{
    for (int i = 0; i < SPECTRUM_FFT_SIZE; i++) {
        s_window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (SPECTRUM_FFT_SIZE - 1)));
    }
}

// ---------------------------------------------------------------------------
// 原地 Radix-2 Cooley-Tukey FFT
// ---------------------------------------------------------------------------
static void fft_inplace(cf32_t *x, int n)
{
    // 位反转置换
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { cf32_t t = x[i]; x[i] = x[j]; x[j] = t; }
    }

    // 蝶形运算
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / len;
        float wr0 = cosf(ang), wi0 = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float wr = 1.0f, wi = 0.0f;
            for (int j = 0; j < len / 2; j++) {
                cf32_t u = x[i + j];
                cf32_t v = { x[i + j + len/2].r * wr - x[i + j + len/2].i * wi,
                              x[i + j + len/2].r * wi + x[i + j + len/2].i * wr };
                x[i + j]         = (cf32_t){ u.r + v.r, u.i + v.i };
                x[i + j + len/2] = (cf32_t){ u.r - v.r, u.i - v.i };
                float nwr = wr * wr0 - wi * wi0;
                float nwi = wr * wi0 + wi * wr0;
                wr = nwr; wi = nwi;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// IQ 数据处理（回调）
// ---------------------------------------------------------------------------

void spectrum_process_iq(const uint8_t *buf, size_t len)
{
    if (!s_active) return;

    size_t num_samples = len / 2;

    for (size_t i = 0; i < num_samples; i++) {
        if (s_iq_idx >= SPECTRUM_FFT_SIZE) break;

        float I = ((float)buf[i * 2]     - 127.5f) * (1.0f / 127.5f);
        float Q = ((float)buf[i * 2 + 1] - 127.5f) * (1.0f / 127.5f);

        // 加窗
        s_fft_buf[s_iq_idx].r = I * s_window[s_iq_idx];
        s_fft_buf[s_iq_idx].i = Q * s_window[s_iq_idx];
        s_iq_idx++;
    }

    if (s_iq_idx >= SPECTRUM_FFT_SIZE) {
        s_iq_idx = 0;

        // 计算 FFT
        fft_inplace(s_fft_buf, SPECTRUM_FFT_SIZE);

        // 计算幅度（dBFS），FFT 移位（将 DC 移到中心）
        float scale = 1.0f / SPECTRUM_FFT_SIZE;
        for (int k = 0; k < SPECTRUM_FFT_SIZE; k++) {
            // 频率移位：将负频率移到左侧
            int idx = (k + SPECTRUM_FFT_SIZE / 2) % SPECTRUM_FFT_SIZE;
            float mag2 = s_fft_buf[idx].r * s_fft_buf[idx].r
                       + s_fft_buf[idx].i * s_fft_buf[idx].i;
            float db = 10.0f * log10f(mag2 * scale * scale + 1e-20f);
            // 指数移动平均（平滑显示）
            s_avg_db[k] = AVG_ALPHA * db + (1.0f - AVG_ALPHA) * s_avg_db[k];
            s_mag_db[k] = s_avg_db[k];
        }

        // 通知推送任务
        xSemaphoreGive(s_fft_ready);
    }
}

// ---------------------------------------------------------------------------
// WebSocket 客户端管理
// ---------------------------------------------------------------------------

static void ws_add_client(int fd)
{
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < s_ws_count; i++) {
        if (s_ws_fds[i] == fd) { xSemaphoreGive(s_ws_mutex); return; }
    }
    if (s_ws_count < SPECTRUM_WS_MAX_CLIENTS) {
        s_ws_fds[s_ws_count++] = fd;
        ESP_LOGI(TAG, "WS client connected fd=%d (total=%d)", fd, s_ws_count);
    }
    xSemaphoreGive(s_ws_mutex);
}

static void ws_remove_client(int fd)
{
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < s_ws_count; i++) {
        if (s_ws_fds[i] == fd) {
            s_ws_fds[i] = s_ws_fds[--s_ws_count];
            ESP_LOGI(TAG, "WS client disconnected fd=%d (total=%d)", fd, s_ws_count);
            break;
        }
    }
    xSemaphoreGive(s_ws_mutex);
}

// ---------------------------------------------------------------------------
// 二进制帧格式（发送给浏览器）
// Header (12 bytes):
//   [0..3]  uint32 center_freq_hz
//   [4..7]  uint32 bandwidth_hz
//   [8..9]  uint16 fft_size
//   [10]    uint8  version = 1
//   [11]    uint8  reserved = 0
// Payload (fft_size * 2 bytes):
//   int16[] magnitude in 0.1 dBFS units (offset +1200 → range 0..65535)
// ---------------------------------------------------------------------------

#define FRAME_HEADER_SIZE   12
#define DB_OFFSET           1200    // +120.0 dB 偏移，使值为正
#define DB_SCALE            10      // 0.1 dB 分辨率

static void ws_broadcast_fft(void)
{
    if (s_server == NULL || s_ws_count == 0) return;

    size_t frame_size = FRAME_HEADER_SIZE + SPECTRUM_FFT_SIZE * 2;
    uint8_t *frame = malloc(frame_size);
    if (!frame) return;

    // 填充 header
    uint32_t f = s_freq_hz;
    uint32_t b = s_bw_hz;
    uint16_t n = SPECTRUM_FFT_SIZE;
    frame[0]  = (f >> 24) & 0xFF;
    frame[1]  = (f >> 16) & 0xFF;
    frame[2]  = (f >>  8) & 0xFF;
    frame[3]  =  f        & 0xFF;
    frame[4]  = (b >> 24) & 0xFF;
    frame[5]  = (b >> 16) & 0xFF;
    frame[6]  = (b >>  8) & 0xFF;
    frame[7]  =  b        & 0xFF;
    frame[8]  = (n >>  8) & 0xFF;
    frame[9]  =  n        & 0xFF;
    frame[10] = 1;   // version
    frame[11] = 0;   // reserved

    // 填充 FFT 幅度数据
    for (int i = 0; i < SPECTRUM_FFT_SIZE; i++) {
        int16_t val = (int16_t)(s_mag_db[i] * DB_SCALE + DB_OFFSET);
        frame[FRAME_HEADER_SIZE + i * 2]     = (val >> 8) & 0xFF;
        frame[FRAME_HEADER_SIZE + i * 2 + 1] =  val       & 0xFF;
    }

    httpd_ws_frame_t ws_pkt = {
        .final      = true,
        .fragmented = false,
        .type       = HTTPD_WS_TYPE_BINARY,
        .payload    = frame,
        .len        = frame_size,
    };

    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    int i = 0;
    while (i < s_ws_count) {
        esp_err_t ret = httpd_ws_send_frame_async(s_server, s_ws_fds[i], &ws_pkt);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "WS send failed fd=%d, removing", s_ws_fds[i]);
            s_ws_fds[i] = s_ws_fds[--s_ws_count];
        } else {
            i++;
        }
    }
    xSemaphoreGive(s_ws_mutex);

    free(frame);
}

// ---------------------------------------------------------------------------
// FFT 推送任务
// ---------------------------------------------------------------------------

static void spectrum_push_task(void *arg)
{
    ESP_LOGI(TAG, "Spectrum push task started");
    TickType_t last_push = xTaskGetTickCount();

    while (1) {
        // 等待 FFT 就绪，最多等 200ms
        if (xSemaphoreTake(s_fft_ready, pdMS_TO_TICKS(200)) == pdTRUE) {
            // 限速：最快 SPECTRUM_UPDATE_MS 推送一次
            TickType_t now = xTaskGetTickCount();
            TickType_t elapsed = (now - last_push) * portTICK_PERIOD_MS;
            if (elapsed >= SPECTRUM_UPDATE_MS) {
                ws_broadcast_fft();
                last_push = now;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// HTTP 处理器
// ---------------------------------------------------------------------------

// GET /spectrum/ws - WebSocket 升级
static esp_err_t spectrum_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WS handshake fd=%d", httpd_req_to_sockfd(req));
        ws_add_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t pkt = {0};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK || pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ws_remove_client(httpd_req_to_sockfd(req));
    }
    return ESP_OK;
}

// GET /spectrum/status
static esp_err_t spectrum_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root,   "active",    s_active);
    cJSON_AddNumberToObject(root, "freq",      (double)s_freq_hz);
    cJSON_AddNumberToObject(root, "freq_mhz",  s_freq_hz / 1e6);
    cJSON_AddNumberToObject(root, "bw",        (double)s_bw_hz);
    cJSON_AddNumberToObject(root, "bw_khz",    s_bw_hz / 1e3);
    cJSON_AddNumberToObject(root, "fft_size",  SPECTRUM_FFT_SIZE);
    cJSON_AddNumberToObject(root, "min_freq",  SPECTRUM_MIN_FREQ);
    cJSON_AddNumberToObject(root, "max_freq",  SPECTRUM_MAX_FREQ);

    // 支持的带宽列表
    cJSON *bws = cJSON_AddArrayToObject(root, "bw_list");
    cJSON_AddItemToArray(bws, cJSON_CreateNumber(SPECTRUM_BW_250K));
    cJSON_AddItemToArray(bws, cJSON_CreateNumber(SPECTRUM_BW_500K));
    cJSON_AddItemToArray(bws, cJSON_CreateNumber(SPECTRUM_BW_1M));
    cJSON_AddItemToArray(bws, cJSON_CreateNumber(SPECTRUM_BW_2M));

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// POST /spectrum/freq  body: {"freq":100000000} 或 {"freq_mhz":100.0}
static esp_err_t spectrum_freq_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    int len = req->content_len < (int)sizeof(buf) - 1
              ? req->content_len : (int)sizeof(buf) - 1;
    if (httpd_req_recv(req, buf, len) <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body"); return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(buf);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON"); return ESP_FAIL; }

    uint32_t freq = 0;
    cJSON *jf = cJSON_GetObjectItem(root, "freq");
    cJSON *jm = cJSON_GetObjectItem(root, "freq_mhz");
    if (jf && cJSON_IsNumber(jf)) freq = (uint32_t)jf->valuedouble;
    else if (jm && cJSON_IsNumber(jm)) freq = (uint32_t)(jm->valuedouble * 1e6);
    cJSON_Delete(root);

    if (freq < SPECTRUM_MIN_FREQ || freq > SPECTRUM_MAX_FREQ) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Freq out of range"); return ESP_FAIL;
    }

    esp_err_t ret = spectrum_analyzer_set_freq(freq);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":%s,\"freq\":%lu}",
             ret == ESP_OK ? "true" : "false", (unsigned long)freq);
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// POST /spectrum/bw  body: {"bw":1000000} 或 {"bw_khz":1000}
static esp_err_t spectrum_bw_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    int len = req->content_len < (int)sizeof(buf) - 1
              ? req->content_len : (int)sizeof(buf) - 1;
    if (httpd_req_recv(req, buf, len) <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body"); return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(buf);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON"); return ESP_FAIL; }

    uint32_t bw = 0;
    cJSON *jb = cJSON_GetObjectItem(root, "bw");
    cJSON *jk = cJSON_GetObjectItem(root, "bw_khz");
    if (jb && cJSON_IsNumber(jb)) bw = (uint32_t)jb->valuedouble;
    else if (jk && cJSON_IsNumber(jk)) bw = (uint32_t)(jk->valuedouble * 1e3);
    cJSON_Delete(root);

    esp_err_t ret = spectrum_analyzer_set_bw(bw);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":%s,\"bw\":%lu}",
             ret == ESP_OK ? "true" : "false", (unsigned long)bw);
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// POST /spectrum/start  body（可选）: {"freq":100000000,"bw":1000000}
static esp_err_t spectrum_start_handler(httpd_req_t *req)
{
    uint32_t freq = s_freq_hz, bw = s_bw_hz;

    if (req->content_len > 0) {
        char buf[128] = {0};
        int len = req->content_len < (int)sizeof(buf) - 1
                  ? req->content_len : (int)sizeof(buf) - 1;
        if (httpd_req_recv(req, buf, len) > 0) {
            cJSON *root = cJSON_Parse(buf);
            if (root) {
                cJSON *jf = cJSON_GetObjectItem(root, "freq");
                cJSON *jb = cJSON_GetObjectItem(root, "bw");
                if (jf && cJSON_IsNumber(jf)) freq = (uint32_t)jf->valuedouble;
                if (jb && cJSON_IsNumber(jb)) bw   = (uint32_t)jb->valuedouble;
                cJSON_Delete(root);
            }
        }
    }

    esp_err_t ret = spectrum_analyzer_start(freq, bw);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":%s}", ret == ESP_OK ? "true" : "false");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// POST /spectrum/stop
static esp_err_t spectrum_stop_handler(httpd_req_t *req)
{
    esp_err_t ret = spectrum_analyzer_stop();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char resp[32];
    snprintf(resp, sizeof(resp), "{\"ok\":%s}", ret == ESP_OK ? "true" : "false");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// 公开 API
// ---------------------------------------------------------------------------

esp_err_t spectrum_analyzer_init(void)
{
    init_window();
    memset(s_avg_db, 0, sizeof(s_avg_db));

    s_ws_mutex  = xSemaphoreCreateMutex();
    s_fft_ready = xSemaphoreCreateBinary();
    if (!s_ws_mutex || !s_fft_ready) return ESP_ERR_NO_MEM;

    xTaskCreate(spectrum_push_task, "spectrum_push", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Spectrum analyzer initialized (FFT=%d, default %.1f MHz / %.0f kHz BW)",
             SPECTRUM_FFT_SIZE, SPECTRUM_DEFAULT_FREQ / 1e6, SPECTRUM_DEFAULT_BW / 1e3);
    return ESP_OK;
}

esp_err_t spectrum_analyzer_start(uint32_t freq_hz, uint32_t bw_hz)
{
    if (freq_hz < SPECTRUM_MIN_FREQ || freq_hz > SPECTRUM_MAX_FREQ)
        return ESP_ERR_INVALID_ARG;

    if (rtlsdr_get_state() < RTLSDR_STATE_CONFIGURED) {
        ESP_LOGE(TAG, "RTL-SDR not ready");
        return ESP_ERR_INVALID_STATE;
    }

    rtlsdr_stop_streaming();
    vTaskDelay(pdMS_TO_TICKS(50));

    // 重置状态
    s_iq_idx = 0;
    memset(s_avg_db, 0, sizeof(s_avg_db));

    rtlsdr_set_iq_callback(spectrum_process_iq);

    s_freq_hz = freq_hz;
    s_bw_hz   = bw_hz ? bw_hz : SPECTRUM_DEFAULT_BW;
    s_active  = true;

    esp_err_t ret = rtlsdr_set_freq(freq_hz);
    if (ret != ESP_OK) { s_active = false; rtlsdr_set_iq_callback(NULL); return ret; }

    ret = rtlsdr_start_streaming();
    if (ret != ESP_OK) { s_active = false; rtlsdr_set_iq_callback(NULL); return ret; }

    ESP_LOGI(TAG, "Spectrum started: %.3f MHz / %.0f kHz BW", freq_hz / 1e6, s_bw_hz / 1e3);
    return ESP_OK;
}

esp_err_t spectrum_analyzer_stop(void)
{
    if (!s_active) return ESP_OK;
    s_active = false;
    rtlsdr_stop_streaming();
    vTaskDelay(pdMS_TO_TICKS(50));
    rtlsdr_set_iq_callback(NULL);
    rtlsdr_set_freq(RTLSDR_ADSB_FREQ);
    rtlsdr_start_streaming();
    ESP_LOGI(TAG, "Spectrum stopped, switched back to ADS-B");
    return ESP_OK;
}

esp_err_t spectrum_analyzer_set_freq(uint32_t freq_hz)
{
    if (freq_hz < SPECTRUM_MIN_FREQ || freq_hz > SPECTRUM_MAX_FREQ)
        return ESP_ERR_INVALID_ARG;
    s_freq_hz = freq_hz;
    s_iq_idx  = 0;
    memset(s_avg_db, 0, sizeof(s_avg_db));
    if (s_active) return rtlsdr_set_freq(freq_hz);
    return ESP_OK;
}

esp_err_t spectrum_analyzer_set_bw(uint32_t bw_hz)
{
    // 验证带宽合法性
    if (bw_hz != SPECTRUM_BW_250K && bw_hz != SPECTRUM_BW_500K &&
        bw_hz != SPECTRUM_BW_1M   && bw_hz != SPECTRUM_BW_2M) {
        return ESP_ERR_INVALID_ARG;
    }
    s_bw_hz = bw_hz;
    // 带宽变化需要重启流（RTL-SDR 采样率变化）
    if (s_active) {
        uint32_t f = s_freq_hz;
        spectrum_analyzer_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
        spectrum_analyzer_start(f, bw_hz);
    }
    return ESP_OK;
}

uint32_t spectrum_analyzer_get_freq(void)  { return s_freq_hz; }
uint32_t spectrum_analyzer_get_bw(void)    { return s_bw_hz; }
bool     spectrum_analyzer_is_active(void) { return s_active; }

esp_err_t spectrum_analyzer_register_handlers(httpd_handle_t server)
{
    s_server = server;

    httpd_uri_t ws_uri = {
        .uri          = "/spectrum/ws",
        .method       = HTTP_GET,
        .handler      = spectrum_ws_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws_uri);

    httpd_uri_t status_uri = { .uri = "/spectrum/status", .method = HTTP_GET,  .handler = spectrum_status_handler };
    httpd_uri_t freq_uri   = { .uri = "/spectrum/freq",   .method = HTTP_POST, .handler = spectrum_freq_handler   };
    httpd_uri_t bw_uri     = { .uri = "/spectrum/bw",     .method = HTTP_POST, .handler = spectrum_bw_handler     };
    httpd_uri_t start_uri  = { .uri = "/spectrum/start",  .method = HTTP_POST, .handler = spectrum_start_handler  };
    httpd_uri_t stop_uri   = { .uri = "/spectrum/stop",   .method = HTTP_POST, .handler = spectrum_stop_handler   };

    httpd_register_uri_handler(server, &status_uri);
    httpd_register_uri_handler(server, &freq_uri);
    httpd_register_uri_handler(server, &bw_uri);
    httpd_register_uri_handler(server, &start_uri);
    httpd_register_uri_handler(server, &stop_uri);

    ESP_LOGI(TAG, "Spectrum HTTP/WS handlers registered");
    return ESP_OK;
}