#include "fm_receiver.h"
#include "rtlsdr_usb.h"
#include "config.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "fm_receiver";

// ---------------------------------------------------------------------------
// 内部状态
// ---------------------------------------------------------------------------
static bool              s_fm_active  = false;
static uint32_t          s_fm_freq    = FM_DEFAULT_FREQ;
static QueueHandle_t     s_audio_queue = NULL;

// 当前正在填充的音频块
static int16_t  s_chunk_buf[FM_AUDIO_CHUNK_SAMPLES];
static int      s_chunk_idx = 0;

// FM 解调状态（跨缓冲区保持）
static float s_prev_i      = 0.0f;
static float s_prev_q      = 0.0f;
static float s_deemph_y    = 0.0f;   // 去加重滤波器状态
static int   s_decim_cnt   = 0;       // 抽取计数器

// 去加重滤波器系数（75 μs，44100 Hz）
// alpha = exp(-1 / (75e-6 * 44100)) ≈ 0.7454
#define DEEMPH_ALPHA  0.7454f

// ---------------------------------------------------------------------------
// FM 解调核心
// ---------------------------------------------------------------------------

/**
 * 将一批 IQ 样本解调为 PCM 音频，并将完整块放入队列。
 *
 * 算法：
 *   1. uint8 → float IQ（减去 127.5，除以 127.5）
 *   2. 鉴频：demod = (I[n]*Q[n-1] - I[n-1]*Q[n]) / (I²+Q²+ε)
 *   3. 抽取：每 FM_DECIMATION 个样本保留 1 个
 *   4. 去加重：一阶 IIR 低通（75 μs）
 *   5. 缩放为 int16_t，填入块缓冲区
 */
void fm_process_iq(const uint8_t *buf, size_t len)
{
    if (!s_fm_active || s_audio_queue == NULL) return;

    size_t num_samples = len / 2;

    for (size_t i = 0; i < num_samples; i++) {
        float I = ((float)buf[i * 2]     - 127.5f) * (1.0f / 127.5f);
        float Q = ((float)buf[i * 2 + 1] - 127.5f) * (1.0f / 127.5f);

        // FM 鉴频（arctangent 近似）
        float denom = I * I + Q * Q + 1e-10f;
        float demod = (I * s_prev_q - s_prev_i * Q) / denom;

        s_prev_i = I;
        s_prev_q = Q;

        // 抽取
        s_decim_cnt++;
        if (s_decim_cnt < FM_DECIMATION) continue;
        s_decim_cnt = 0;

        // 去加重滤波（一阶 IIR）
        s_deemph_y = DEEMPH_ALPHA * s_deemph_y + (1.0f - DEEMPH_ALPHA) * demod;

        // 缩放为 int16_t（FM 解调输出范围约 ±1.0）
        float scaled = s_deemph_y * 20000.0f;
        if (scaled >  32767.0f) scaled =  32767.0f;
        if (scaled < -32768.0f) scaled = -32768.0f;
        s_chunk_buf[s_chunk_idx++] = (int16_t)scaled;

        // 块满时放入队列
        if (s_chunk_idx >= FM_AUDIO_CHUNK_SAMPLES) {
            s_chunk_idx = 0;
            // 分配堆内存传递给队列（接收方负责 free）
            int16_t *chunk = malloc(FM_AUDIO_CHUNK_BYTES);
            if (chunk) {
                memcpy(chunk, s_chunk_buf, FM_AUDIO_CHUNK_BYTES);
                if (xQueueSend(s_audio_queue, &chunk, 0) != pdTRUE) {
                    free(chunk);  // 队列满，丢弃
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// HTTP 处理器
// ---------------------------------------------------------------------------

/**
 * GET /fm/audio
 * 以 HTTP chunked 方式持续推送原始 PCM（16-bit LE，单声道，44100 Hz）。
 * 浏览器端用 Web Audio API 解码播放。
 */
static esp_err_t fm_audio_handler(httpd_req_t *req)
{
    if (!s_fm_active) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "FM not active");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "audio/pcm");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Audio-SampleRate", "44100");
    httpd_resp_set_hdr(req, "X-Audio-Channels",   "1");
    httpd_resp_set_hdr(req, "X-Audio-BitDepth",   "16");

    ESP_LOGI(TAG, "Audio stream client connected");

    while (s_fm_active) {
        int16_t *chunk = NULL;
        // 等待最多 500ms
        if (xQueueReceive(s_audio_queue, &chunk, pdMS_TO_TICKS(500)) == pdTRUE) {
            esp_err_t ret = httpd_resp_send_chunk(req, (const char *)chunk,
                                                  FM_AUDIO_CHUNK_BYTES);
            free(chunk);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Audio stream send failed, client disconnected");
                break;
            }
        }
    }

    httpd_resp_send_chunk(req, NULL, 0);
    ESP_LOGI(TAG, "Audio stream client disconnected");
    return ESP_OK;
}

/**
 * GET /fm/status
 * 返回 FM 状态 JSON：{"active":true,"freq":100000000}
 */
static esp_err_t fm_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root,   "active", s_fm_active);
    cJSON_AddNumberToObject(root, "freq",   (double)s_fm_freq);
    cJSON_AddNumberToObject(root, "freq_mhz", s_fm_freq / 1e6);
    cJSON_AddNumberToObject(root, "min_freq", FM_MIN_FREQ);
    cJSON_AddNumberToObject(root, "max_freq", FM_MAX_FREQ);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

/**
 * POST /fm/freq
 * Body: {"freq":100000000}  或  {"freq_mhz":100.0}
 * 切换 FM 频率。
 */
static esp_err_t fm_freq_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    int  len = req->content_len < (int)sizeof(buf) - 1
               ? req->content_len : (int)sizeof(buf) - 1;

    if (httpd_req_recv(req, buf, len) <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    uint32_t freq = 0;
    cJSON *jfreq = cJSON_GetObjectItem(root, "freq");
    cJSON *jmhz  = cJSON_GetObjectItem(root, "freq_mhz");

    if (jfreq && cJSON_IsNumber(jfreq)) {
        freq = (uint32_t)jfreq->valuedouble;
    } else if (jmhz && cJSON_IsNumber(jmhz)) {
        freq = (uint32_t)(jmhz->valuedouble * 1e6);
    }
    cJSON_Delete(root);

    if (freq < FM_MIN_FREQ || freq > FM_MAX_FREQ) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Freq out of range");
        return ESP_FAIL;
    }

    esp_err_t ret = fm_receiver_set_freq(freq);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":%s,\"freq\":%lu}",
             ret == ESP_OK ? "true" : "false", (unsigned long)freq);
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

/**
 * POST /fm/start
 * Body（可选）: {"freq":100000000}
 * 启动 FM 接收。
 */
static esp_err_t fm_start_handler(httpd_req_t *req)
{
    uint32_t freq = s_fm_freq;

    if (req->content_len > 0) {
        char buf[128] = {0};
        int len = req->content_len < (int)sizeof(buf) - 1
                  ? req->content_len : (int)sizeof(buf) - 1;
        if (httpd_req_recv(req, buf, len) > 0) {
            cJSON *root = cJSON_Parse(buf);
            if (root) {
                cJSON *jf = cJSON_GetObjectItem(root, "freq");
                if (jf && cJSON_IsNumber(jf)) freq = (uint32_t)jf->valuedouble;
                cJSON *jm = cJSON_GetObjectItem(root, "freq_mhz");
                if (jm && cJSON_IsNumber(jm)) freq = (uint32_t)(jm->valuedouble * 1e6);
                cJSON_Delete(root);
            }
        }
    }

    esp_err_t ret = fm_receiver_start(freq);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":%s,\"freq\":%lu}",
             ret == ESP_OK ? "true" : "false", (unsigned long)freq);
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

/**
 * POST /fm/stop
 * 停止 FM 接收，切换回 ADS-B。
 */
static esp_err_t fm_stop_handler(httpd_req_t *req)
{
    esp_err_t ret = fm_receiver_stop();

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

esp_err_t fm_receiver_init(void)
{
    if (s_audio_queue != NULL) return ESP_OK;  // 已初始化

    s_audio_queue = xQueueCreate(FM_AUDIO_QUEUE_DEPTH, sizeof(int16_t *));
    if (!s_audio_queue) {
        ESP_LOGE(TAG, "Failed to create audio queue");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "FM receiver initialized (default freq: %.1f MHz)",
             FM_DEFAULT_FREQ / 1e6);
    return ESP_OK;
}

esp_err_t fm_receiver_start(uint32_t freq_hz)
{
    if (freq_hz < FM_MIN_FREQ || freq_hz > FM_MAX_FREQ) {
        ESP_LOGE(TAG, "Frequency %.1f MHz out of FM range", freq_hz / 1e6);
        return ESP_ERR_INVALID_ARG;
    }

    if (rtlsdr_get_state() < RTLSDR_STATE_CONFIGURED) {
        ESP_LOGE(TAG, "RTL-SDR not ready");
        return ESP_ERR_INVALID_STATE;
    }

    // 停止当前流（ADS-B 或 FM）
    rtlsdr_stop_streaming();
    vTaskDelay(pdMS_TO_TICKS(50));

    // 重置解调状态
    s_prev_i    = 0.0f;
    s_prev_q    = 0.0f;
    s_deemph_y  = 0.0f;
    s_decim_cnt = 0;
    s_chunk_idx = 0;

    // 清空音频队列中残留的块
    int16_t *old = NULL;
    while (xQueueReceive(s_audio_queue, &old, 0) == pdTRUE) {
        free(old);
    }

    // 设置 IQ 回调为 FM 解调函数
    rtlsdr_set_iq_callback(fm_process_iq);

    // 调谐到 FM 频率
    s_fm_freq   = freq_hz;
    s_fm_active = true;

    esp_err_t ret = rtlsdr_set_freq(freq_hz);
    if (ret != ESP_OK) {
        s_fm_active = false;
        rtlsdr_set_iq_callback(NULL);
        return ret;
    }

    ret = rtlsdr_start_streaming();
    if (ret != ESP_OK) {
        s_fm_active = false;
        rtlsdr_set_iq_callback(NULL);
        return ret;
    }

    ESP_LOGI(TAG, "FM receiver started: %.1f MHz", freq_hz / 1e6);
    return ESP_OK;
}

esp_err_t fm_receiver_stop(void)
{
    if (!s_fm_active) return ESP_OK;

    s_fm_active = false;
    rtlsdr_stop_streaming();
    vTaskDelay(pdMS_TO_TICKS(50));

    // 清空队列
    int16_t *chunk = NULL;
    while (xQueueReceive(s_audio_queue, &chunk, 0) == pdTRUE) {
        free(chunk);
    }

    // 切换回 ADS-B
    rtlsdr_set_iq_callback(NULL);
    rtlsdr_set_freq(RTLSDR_ADSB_FREQ);
    rtlsdr_start_streaming();

    ESP_LOGI(TAG, "FM receiver stopped, switched back to ADS-B (1090 MHz)");
    return ESP_OK;
}

esp_err_t fm_receiver_set_freq(uint32_t freq_hz)
{
    if (freq_hz < FM_MIN_FREQ || freq_hz > FM_MAX_FREQ) {
        return ESP_ERR_INVALID_ARG;
    }

    s_fm_freq = freq_hz;

    if (s_fm_active) {
        // 重置解调状态，避免频率切换时的瞬态噪声
        s_prev_i    = 0.0f;
        s_prev_q    = 0.0f;
        s_deemph_y  = 0.0f;
        s_decim_cnt = 0;
        s_chunk_idx = 0;

        esp_err_t ret = rtlsdr_set_freq(freq_hz);
        if (ret != ESP_OK) return ret;
    }

    ESP_LOGI(TAG, "FM frequency set to %.1f MHz", freq_hz / 1e6);
    return ESP_OK;
}

uint32_t fm_receiver_get_freq(void)  { return s_fm_freq; }
bool     fm_receiver_is_active(void) { return s_fm_active; }

esp_err_t fm_receiver_register_handlers(httpd_handle_t server)
{
    // GET /fm/audio - 音频流
    httpd_uri_t audio_uri = {
        .uri     = "/fm/audio",
        .method  = HTTP_GET,
        .handler = fm_audio_handler,
    };
    httpd_register_uri_handler(server, &audio_uri);

    // GET /fm/status - 状态查询
    httpd_uri_t status_uri = {
        .uri     = "/fm/status",
        .method  = HTTP_GET,
        .handler = fm_status_handler,
    };
    httpd_register_uri_handler(server, &status_uri);

    // POST /fm/freq - 设置频率
    httpd_uri_t freq_uri = {
        .uri     = "/fm/freq",
        .method  = HTTP_POST,
        .handler = fm_freq_handler,
    };
    httpd_register_uri_handler(server, &freq_uri);

    // POST /fm/start - 启动 FM
    httpd_uri_t start_uri = {
        .uri     = "/fm/start",
        .method  = HTTP_POST,
        .handler = fm_start_handler,
    };
    httpd_register_uri_handler(server, &start_uri);

    // POST /fm/stop - 停止 FM
    httpd_uri_t stop_uri = {
        .uri     = "/fm/stop",
        .method  = HTTP_POST,
        .handler = fm_stop_handler,
    };
    httpd_register_uri_handler(server, &stop_uri);

    ESP_LOGI(TAG, "FM HTTP handlers registered");
    return ESP_OK;
}