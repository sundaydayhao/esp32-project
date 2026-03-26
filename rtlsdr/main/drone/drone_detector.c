/**
 * @file drone_detector.c
 * @brief 无人机飞行监测告警系统
 *
 * 工作原理：
 *  RTL-SDR 依次扫描无人机常用频段，测量各频段信号功率。
 *  通过与自适应噪底比较计算 SNR，超过阈值时触发分级告警。
 *
 * 检测频段：
 *  - 433 MHz   : RC 遥控器（433 MHz ISM 频段）
 *  - 868 MHz   : 欧洲 Remote ID / LoRa 遥控
 *  - 915 MHz   : 北美 Remote ID / 数传链路
 *  - 1080 MHz  : 部分 FPV 视频下行
 *  - 1090 MHz  : ADS-B（装备 ADS-B 的无人机）
 *  - 1200 MHz  : 1.2 GHz FPV 视频
 */

#include "drone_detector.h"
#include "rtlsdr_usb.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "drone_det";

// ---------------------------------------------------------------------------
// 频段定义
// ---------------------------------------------------------------------------
const drone_band_t DRONE_BANDS[DRONE_BAND_COUNT] = {
    { 433920000,  200000, "433MHz",  "RC遥控/ISM" },
    { 868000000,  500000, "868MHz",  "Remote ID/LoRa" },
    { 915000000,  500000, "915MHz",  "数传链路" },
    { 1080000000, 500000, "1080MHz", "FPV视频下行" },
    { 1090000000, 500000, "1090MHz", "ADS-B无人机" },
    { 1200000000, 500000, "1200MHz", "1.2G FPV" },
};

// ---------------------------------------------------------------------------
// 内部状态
// ---------------------------------------------------------------------------
static drone_event_t  s_events[DRONE_MAX_EVENTS];
static int            s_event_count = 0;
static drone_stats_t  s_stats       = {0};
static drone_config_t s_config      = {
    .snr_low_db  = 10.0f,
    .snr_med_db  = 20.0f,
    .snr_high_db = 30.0f,
    .dwell_ms    = 300,
};

static SemaphoreHandle_t s_mutex    = NULL;
static bool              s_running  = false;
static TaskHandle_t      s_task_hdl = NULL;

static httpd_handle_t s_server      = NULL;
static int  s_ws_clients[4];
static int  s_ws_count = 0;

// ---------------------------------------------------------------------------
// 功率计算：从 IQ 样本计算平均功率（dBFS）
// ---------------------------------------------------------------------------
static float calc_power_db(const int8_t *iq, int samples)
{
    if (samples <= 0) return -100.0f;
    float sum = 0.0f;
    for (int i = 0; i < samples; i++) {
        float iv = iq[i * 2]     / 128.0f;
        float qv = iq[i * 2 + 1] / 128.0f;
        sum += iv * iv + qv * qv;
    }
    float power = sum / samples;
    if (power < 1e-10f) return -100.0f;
    return 10.0f * log10f(power);
}

// ---------------------------------------------------------------------------
// 自适应噪底估计（指数移动平均）
// ---------------------------------------------------------------------------
#define NOISE_ALPHA  0.05f   // 慢速跟踪噪底

static float s_noise_floor[DRONE_BAND_COUNT];
static bool  s_noise_init[DRONE_BAND_COUNT];

static float update_noise_floor(int band_idx, float power_db)
{
    if (!s_noise_init[band_idx]) {
        s_noise_floor[band_idx] = power_db;
        s_noise_init[band_idx]  = true;
    } else {
        // 只在功率低于当前噪底+5dB 时更新（避免信号污染噪底）
        if (power_db < s_noise_floor[band_idx] + 5.0f) {
            s_noise_floor[band_idx] = NOISE_ALPHA * power_db +
                                      (1.0f - NOISE_ALPHA) * s_noise_floor[band_idx];
        }
    }
    return s_noise_floor[band_idx];
}

// ---------------------------------------------------------------------------
// 告警级别判断
// ---------------------------------------------------------------------------
static drone_alert_level_t classify_alert(float snr_db)
{
    if (snr_db >= s_config.snr_high_db) return DRONE_ALERT_HIGH;
    if (snr_db >= s_config.snr_med_db)  return DRONE_ALERT_MEDIUM;
    if (snr_db >= s_config.snr_low_db)  return DRONE_ALERT_LOW;
    return DRONE_ALERT_NONE;
}

// ---------------------------------------------------------------------------
// 记录告警事件
// ---------------------------------------------------------------------------
static void record_event(int band_idx, float power_db, float snr_db,
                          drone_alert_level_t level)
{
    uint32_t now_s = (uint32_t)(esp_timer_get_time() / 1000000);

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // 检查是否已有同频段活跃事件（合并）
    for (int i = 0; i < s_event_count; i++) {
        if (s_events[i].freq_hz == DRONE_BANDS[band_idx].freq_hz &&
            s_events[i].active) {
            s_events[i].timestamp_s = now_s;
            s_events[i].power_db    = power_db;
            s_events[i].snr_db      = snr_db;
            s_events[i].level       = level;
            xSemaphoreGive(s_mutex);
            return;
        }
    }

    // 新事件
    int idx = s_event_count < DRONE_MAX_EVENTS
              ? s_event_count++
              : DRONE_MAX_EVENTS - 1;  // 覆盖最旧

    s_events[idx].timestamp_s = now_s;
    s_events[idx].freq_hz     = DRONE_BANDS[band_idx].freq_hz;
    s_events[idx].power_db    = power_db;
    s_events[idx].snr_db      = snr_db;
    s_events[idx].level       = level;
    s_events[idx].active      = true;
    strncpy(s_events[idx].band_name, DRONE_BANDS[band_idx].name,
            sizeof(s_events[idx].band_name) - 1);

    s_stats.total_alerts++;
    xSemaphoreGive(s_mutex);

    ESP_LOGW(TAG, "[%s] ALERT-%s  Power=%.1fdBFS  SNR=%.1fdB",
             DRONE_BANDS[band_idx].name,
             drone_alert_level_str(level), power_db, snr_db);
}

// ---------------------------------------------------------------------------
// 扫描任务
// ---------------------------------------------------------------------------
#define SCAN_BUF_SAMPLES  (s_config.dwell_ms * 250)  // 250 kHz 采样率

static void drone_scan_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Drone scan task started (%d bands)", DRONE_BAND_COUNT);

    const int sample_rate = 250000;
    int8_t *iq_buf = malloc(sample_rate / 5 * 2);  // 200ms @ 250kHz
    if (!iq_buf) {
        ESP_LOGE(TAG, "OOM");
        vTaskDelete(NULL);
        return;
    }

    int band_idx = 0;

    while (s_running) {
        const drone_band_t *band = &DRONE_BANDS[band_idx];

        // 调谐到当前频段
        rtlsdr_set_frequency(band->freq_hz);
        rtlsdr_set_sample_rate(sample_rate);
        vTaskDelay(pdMS_TO_TICKS(50));  // 等待 PLL 锁定

        // 读取 IQ 数据
        int buf_bytes = (sample_rate / 5) * 2;  // 200ms
        int bytes_read = rtlsdr_read_sync(iq_buf, buf_bytes);
        int samples = (bytes_read > 0) ? bytes_read / 2 : 0;

        if (samples > 0) {
            float power_db = calc_power_db(iq_buf, samples);
            float noise_db = update_noise_floor(band_idx, power_db);
            float snr_db   = power_db - noise_db;

            drone_alert_level_t level = classify_alert(snr_db);

            // 更新频段状态
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            band_status_t *bs = &s_stats.bands[band_idx];
            bs->freq_hz       = band->freq_hz;
            bs->power_db      = power_db;
            bs->noise_floor_db= noise_db;
            bs->snr_db        = snr_db;
            bs->alert         = level;
            if (level > DRONE_ALERT_NONE) {
                bs->hit_count++;
                bs->last_hit_ms = (uint32_t)(esp_timer_get_time() / 1000);
            }
            s_stats.scan_count++;
            xSemaphoreGive(s_mutex);

            // 触发告警
            if (level > DRONE_ALERT_NONE) {
                record_event(band_idx, power_db, snr_db, level);
            } else {
                // 清除该频段活跃事件
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                for (int i = 0; i < s_event_count; i++) {
                    if (s_events[i].freq_hz == band->freq_hz)
                        s_events[i].active = false;
                }
                xSemaphoreGive(s_mutex);
            }
        }

        // 切换到下一频段
        band_idx = (band_idx + 1) % DRONE_BAND_COUNT;

        // 驻留时间
        vTaskDelay(pdMS_TO_TICKS(s_config.dwell_ms));
    }

    free(iq_buf);
    ESP_LOGI(TAG, "Drone scan task stopped");
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// WebSocket 广播
// ---------------------------------------------------------------------------
static char *build_status_json(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root,   "active",        s_running);
    cJSON_AddNumberToObject(root, "scan_count",    s_stats.scan_count);
    cJSON_AddNumberToObject(root, "total_alerts",  s_stats.total_alerts);
    cJSON_AddNumberToObject(root, "uptime_s",      s_stats.uptime_s);

    // 频段状态
    cJSON *bands = cJSON_AddArrayToObject(root, "bands");
    for (int i = 0; i < DRONE_BAND_COUNT; i++) {
        band_status_t *bs = &s_stats.bands[i];
        cJSON *b = cJSON_CreateObject();
        cJSON_AddStringToObject(b, "name",    DRONE_BANDS[i].name);
        cJSON_AddStringToObject(b, "desc",    DRONE_BANDS[i].desc);
        cJSON_AddNumberToObject(b, "freq",    DRONE_BANDS[i].freq_hz);
        cJSON_AddNumberToObject(b, "power",   bs->power_db);
        cJSON_AddNumberToObject(b, "noise",   bs->noise_floor_db);
        cJSON_AddNumberToObject(b, "snr",     bs->snr_db);
        cJSON_AddNumberToObject(b, "alert",   bs->alert);
        cJSON_AddStringToObject(b, "alert_str", drone_alert_level_str(bs->alert));
        cJSON_AddNumberToObject(b, "hits",    bs->hit_count);
        cJSON_AddItemToArray(bands, b);
    }

    // 最近告警事件（最多20条）
    cJSON *evts = cJSON_AddArrayToObject(root, "events");
    int start = s_event_count > 20 ? s_event_count - 20 : 0;
    for (int i = s_event_count - 1; i >= start; i--) {
        drone_event_t *e = &s_events[i];
        cJSON *ev = cJSON_CreateObject();
        cJSON_AddNumberToObject(ev, "ts",       e->timestamp_s);
        cJSON_AddNumberToObject(ev, "freq",     e->freq_hz);
        cJSON_AddStringToObject(ev, "band",     e->band_name);
        cJSON_AddNumberToObject(ev, "power",    e->power_db);
        cJSON_AddNumberToObject(ev, "snr",      e->snr_db);
        cJSON_AddNumberToObject(ev, "level",    e->level);
        cJSON_AddStringToObject(ev, "level_str",drone_alert_level_str(e->level));
        cJSON_AddBoolToObject(ev,   "active",   e->active);
        cJSON_AddItemToArray(evts, ev);
    }

    xSemaphoreGive(s_mutex);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

void drone_ws_broadcast(void)
{
    if (!s_server || s_ws_count == 0) return;
    char *json = build_status_json();
    if (!json) return;

    httpd_ws_frame_t pkt = {
        .final = true, .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };
    int i = 0;
    while (i < s_ws_count) {
        if (httpd_ws_send_frame_async(s_server, s_ws_clients[i], &pkt) != ESP_OK)
            s_ws_clients[i] = s_ws_clients[--s_ws_count];
        else i++;
    }
    free(json);
}

// ---------------------------------------------------------------------------
// HTTP 处理器
// ---------------------------------------------------------------------------
static esp_err_t drone_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        for (int i = 0; i < s_ws_count; i++)
            if (s_ws_clients[i] == fd) return ESP_OK;
        if (s_ws_count < 4) s_ws_clients[s_ws_count++] = fd;
        return ESP_OK;
    }
    httpd_ws_frame_t pkt = {0};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK || pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        for (int i = 0; i < s_ws_count; i++) {
            if (s_ws_clients[i] == fd) { s_ws_clients[i] = s_ws_clients[--s_ws_count]; break; }
        }
    }
    return ESP_OK;
}

static esp_err_t drone_start_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = drone_detector_start();
    httpd_resp_sendstr(req, ret == ESP_OK ? "{\"ok\":true}" : "{\"ok\":false}");
    return ESP_OK;
}

static esp_err_t drone_stop_handler(httpd_req_t *req)
{
    drone_detector_stop();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t drone_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    char *json = build_status_json();
    if (!json) { httpd_resp_send_500(req); return ESP_FAIL; }
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

static esp_err_t drone_clear_handler(httpd_req_t *req)
{
    drone_clear_events();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t drone_config_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    if (req->method == HTTP_GET) {
        drone_config_t cfg;
        drone_get_config(&cfg);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "snr_low",  cfg.snr_low_db);
        cJSON_AddNumberToObject(root, "snr_med",  cfg.snr_med_db);
        cJSON_AddNumberToObject(root, "snr_high", cfg.snr_high_db);
        cJSON_AddNumberToObject(root, "dwell_ms", cfg.dwell_ms);
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        httpd_resp_sendstr(req, json);
        free(json);
        return ESP_OK;
    }
    // POST: 读取 body
    char buf[256] = {0};
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len > 0) {
        cJSON *root = cJSON_Parse(buf);
        if (root) {
            drone_config_t cfg;
            drone_get_config(&cfg);
            cJSON *v;
            if ((v = cJSON_GetObjectItem(root, "snr_low"))  && cJSON_IsNumber(v)) cfg.snr_low_db  = (float)v->valuedouble;
            if ((v = cJSON_GetObjectItem(root, "snr_med"))  && cJSON_IsNumber(v)) cfg.snr_med_db  = (float)v->valuedouble;
            if ((v = cJSON_GetObjectItem(root, "snr_high")) && cJSON_IsNumber(v)) cfg.snr_high_db = (float)v->valuedouble;
            if ((v = cJSON_GetObjectItem(root, "dwell_ms")) && cJSON_IsNumber(v)) cfg.dwell_ms    = (uint32_t)v->valueint;
            drone_set_config(&cfg);
            cJSON_Delete(root);
        }
    }
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// 广播任务
static void drone_broadcast_task(void *pvParameters)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (s_running) {
            s_stats.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
            drone_ws_broadcast();
        }
    }
}

// ---------------------------------------------------------------------------
// 公共 API
// ---------------------------------------------------------------------------
esp_err_t drone_detector_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    memset(s_events, 0, sizeof(s_events));
    memset(&s_stats,  0, sizeof(s_stats));
    memset(s_noise_init, 0, sizeof(s_noise_init));
    s_event_count = 0;
    s_running     = false;
    ESP_LOGI(TAG, "Drone detector initialized (%d bands)", DRONE_BAND_COUNT);
    return ESP_OK;
}

esp_err_t drone_detector_start(void)
{
    if (s_running) return ESP_OK;
    s_running = true;
    xTaskCreate(drone_scan_task, "drone_scan", 8192, NULL, 5, &s_task_hdl);
    ESP_LOGI(TAG, "Drone detector started");
    return ESP_OK;
}

void drone_detector_stop(void)
{
    s_running = false;
    if (s_task_hdl) { vTaskDelay(pdMS_TO_TICKS(500)); s_task_hdl = NULL; }
    ESP_LOGI(TAG, "Drone detector stopped");
}

bool drone_detector_is_active(void) { return s_running; }

void drone_get_stats(drone_stats_t *stats)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *stats = s_stats;
    xSemaphoreGive(s_mutex);
}

void drone_get_events(drone_event_t **events, int *count)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *events = s_events;
    *count  = s_event_count;
}

void drone_release_events(void) { xSemaphoreGive(s_mutex); }

void drone_clear_events(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_event_count = 0;
    s_stats.total_alerts = 0;
    memset(s_events, 0, sizeof(s_events));
    xSemaphoreGive(s_mutex);
}

esp_err_t drone_set_config(const drone_config_t *cfg)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_config = *cfg;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

void drone_get_config(drone_config_t *cfg)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *cfg = s_config;
    xSemaphoreGive(s_mutex);
}

esp_err_t drone_register_handlers(httpd_handle_t server)
{
    s_server   = server;
    s_ws_count = 0;

    httpd_uri_t ws = { .uri="/drone/ws", .method=HTTP_GET, .handler=drone_ws_handler, .is_websocket=true };
    httpd_register_uri_handler(server, &ws);

    httpd_uri_t start  = { .uri="/drone/start",  .method=HTTP_POST, .handler=drone_start_handler };
    httpd_uri_t stop   = { .uri="/drone/stop",   .method=HTTP_POST, .handler=drone_stop_handler };
    httpd_uri_t status = { .uri="/drone/status", .method=HTTP_GET,  .handler=drone_status_handler };
    httpd_uri_t clear  = { .uri="/drone/clear",  .method=HTTP_POST, .handler=drone_clear_handler };
    httpd_uri_t cfg_get= { .uri="/drone/config", .method=HTTP_GET,  .handler=drone_config_handler };
    httpd_uri_t cfg_set= { .uri="/drone/config", .method=HTTP_POST, .handler=drone_config_handler };

    httpd_register_uri_handler(server, &start);
    httpd_register_uri_handler(server, &stop);
    httpd_register_uri_handler(server, &status);
    httpd_register_uri_handler(server, &clear);
    httpd_register_uri_handler(server, &cfg_get);
    httpd_register_uri_handler(server, &cfg_set);

    xTaskCreate(drone_broadcast_task, "drone_bcast", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "Drone HTTP handlers registered");
    return ESP_OK;
}

const char *drone_alert_level_str(drone_alert_level_t level)
{
    switch (level) {
    case DRONE_ALERT_LOW:    return "低";
    case DRONE_ALERT_MEDIUM: return "中";
    case DRONE_ALERT_HIGH:   return "高";
    default:                 return "无";
    }
}