#include "http_server.h"
#include "adc_sampler.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "http_server";

/* Embedded web files */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");

/* WebSocket client tracking */
#define MAX_WS_CLIENTS  4
#define WS_TARGET_FPS   3
#define WS_FRAME_INTERVAL_US (1000000 / WS_TARGET_FPS)
#define WS_TRANSIENT_SEND_FAIL_LIMIT 6
#define WS_BUSY_RETRY_BASE_MS 250

static int ws_fds[MAX_WS_CLIENTS];
static uint8_t ws_send_fail_counts[MAX_WS_CLIENTS];
static int64_t ws_retry_after_us[MAX_WS_CLIENTS];
static int ws_client_count = 0;
static httpd_handle_t server_handle = NULL;
static SemaphoreHandle_t ws_clients_mutex = NULL;

/* WebSocket broadcast task */
static TaskHandle_t ws_task_handle = NULL;

static int ws_find_client_index_locked(int fd)
{
    for (int i = 0; i < ws_client_count; i++) {
        if (ws_fds[i] == fd) {
            return i;
        }
    }
    return -1;
}

static void ws_remove_client_index_locked(int index)
{
    if (index < 0 || index >= ws_client_count) {
        return;
    }

    ws_fds[index] = ws_fds[ws_client_count - 1];
    ws_send_fail_counts[index] = ws_send_fail_counts[ws_client_count - 1];
    ws_retry_after_us[index] = ws_retry_after_us[ws_client_count - 1];
    ws_client_count--;
}

static bool ws_add_client(int fd)
{
    bool added = false;

    if (xSemaphoreTake(ws_clients_mutex, portMAX_DELAY) == pdTRUE) {
        if (ws_find_client_index_locked(fd) >= 0) {
            added = true;
        } else if (ws_client_count < MAX_WS_CLIENTS) {
            ws_fds[ws_client_count] = fd;
            ws_send_fail_counts[ws_client_count] = 0;
            ws_retry_after_us[ws_client_count] = 0;
            ws_client_count++;
            added = true;
        }
        xSemaphoreGive(ws_clients_mutex);
    }

    return added;
}

static bool ws_remove_client_by_fd(int fd)
{
    bool removed = false;

    if (xSemaphoreTake(ws_clients_mutex, portMAX_DELAY) == pdTRUE) {
        int index = ws_find_client_index_locked(fd);
        if (index >= 0) {
            ws_remove_client_index_locked(index);
            removed = true;
        }
        xSemaphoreGive(ws_clients_mutex);
    }

    return removed;
}

static int ws_snapshot_clients(int *fds, size_t max_fds)
{
    int count = 0;

    if (xSemaphoreTake(ws_clients_mutex, portMAX_DELAY) == pdTRUE) {
        count = ws_client_count;
        if ((size_t)count > max_fds) {
            count = (int)max_fds;
        }
        memcpy(fds, ws_fds, count * sizeof(fds[0]));
        xSemaphoreGive(ws_clients_mutex);
    }

    return count;
}

static bool ws_should_send_now(int fd, int64_t now_us)
{
    bool ready = false;

    if (xSemaphoreTake(ws_clients_mutex, portMAX_DELAY) == pdTRUE) {
        int index = ws_find_client_index_locked(fd);
        if (index >= 0) {
            ready = ws_retry_after_us[index] <= now_us;
        }
        xSemaphoreGive(ws_clients_mutex);
    }

    return ready;
}

static int ws_get_client_count(void)
{
    int count = 0;

    if (xSemaphoreTake(ws_clients_mutex, portMAX_DELAY) == pdTRUE) {
        count = ws_client_count;
        xSemaphoreGive(ws_clients_mutex);
    }

    return count;
}

static void ws_report_send_result(int fd, esp_err_t ret, int send_errno, int64_t now_us)
{
    if (xSemaphoreTake(ws_clients_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    int index = ws_find_client_index_locked(fd);
    if (index < 0) {
        xSemaphoreGive(ws_clients_mutex);
        return;
    }

    if (ret == ESP_OK) {
        ws_send_fail_counts[index] = 0;
        ws_retry_after_us[index] = 0;
        xSemaphoreGive(ws_clients_mutex);
        return;
    }

    bool transient = (send_errno == EAGAIN || send_errno == EINTR);
    if (transient) {
        ws_send_fail_counts[index]++;
        uint32_t cooldown_ms = WS_BUSY_RETRY_BASE_MS * ws_send_fail_counts[index];
        if (cooldown_ms > 1500U) {
            cooldown_ms = 1500U;
        }
        ws_retry_after_us[index] = now_us + ((int64_t)cooldown_ms * 1000LL);
        if (ws_send_fail_counts[index] < WS_TRANSIENT_SEND_FAIL_LIMIT) {
            if (ws_send_fail_counts[index] == 1 ||
                (ws_send_fail_counts[index] % 4) == 0) {
                ESP_LOGW(TAG,
                         "WS send busy fd=%d errno=%d fail_count=%u",
                         fd,
                         send_errno,
                         ws_send_fail_counts[index]);
            }
            xSemaphoreGive(ws_clients_mutex);
            return;
        }

        ESP_LOGW(TAG,
                 "WS send busy timeout fd=%d errno=%d fail_count=%u, removing client",
                 fd,
                 send_errno,
                 ws_send_fail_counts[index]);
    } else {
        ESP_LOGW(TAG, "WS send failed fd=%d errno=%d, removing client", fd, send_errno);
    }

    ws_remove_client_index_locked(index);
    xSemaphoreGive(ws_clients_mutex);
}

/* ------------------------------------------------------------------ */
/* WebSocket broadcast task                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief Pack ADC frame into compact binary format for WebSocket
 *
 * Binary format:
 *   [0..1]   magic: 0xAD 0xC1
 *   [2..3]   sample_count (uint16_t LE)
 *   [4..7]   sample_rate (uint32_t LE)
 *   [8..11]  timestamp_ms (uint32_t LE)
 *   [12..]   interleaved uint16_t: ch1[0], ch2[0], ch1[1], ch2[1], ...
 */
static size_t pack_frame(const adc_frame_t *frame, uint8_t *buf, size_t buf_size)
{
    size_t header_size = 12;
    size_t data_size   = ADC_BUFFER_SIZE * 2 * sizeof(uint16_t);
    size_t total       = header_size + data_size;

    if (buf_size < total) return 0;

    buf[0] = 0xAD;
    buf[1] = 0xC1;
    buf[2] = (uint8_t)(ADC_BUFFER_SIZE & 0xFF);
    buf[3] = (uint8_t)((ADC_BUFFER_SIZE >> 8) & 0xFF);
    buf[4] = (uint8_t)(frame->sample_rate & 0xFF);
    buf[5] = (uint8_t)((frame->sample_rate >> 8) & 0xFF);
    buf[6] = (uint8_t)((frame->sample_rate >> 16) & 0xFF);
    buf[7] = (uint8_t)((frame->sample_rate >> 24) & 0xFF);
    buf[8]  = (uint8_t)(frame->timestamp_ms & 0xFF);
    buf[9]  = (uint8_t)((frame->timestamp_ms >> 8) & 0xFF);
    buf[10] = (uint8_t)((frame->timestamp_ms >> 16) & 0xFF);
    buf[11] = (uint8_t)((frame->timestamp_ms >> 24) & 0xFF);

    uint16_t *data_ptr = (uint16_t *)(buf + header_size);
    for (int i = 0; i < ADC_BUFFER_SIZE; i++) {
        data_ptr[i * 2]     = frame->ch1[i];
        data_ptr[i * 2 + 1] = frame->ch2[i];
    }

    return total;
}

static void ws_broadcast_task(void *pvParameters)
{
    static uint8_t tx_buf[12 + ADC_BUFFER_SIZE * 4 + 16];
    int client_fds[MAX_WS_CLIENTS];
    adc_frame_t frame;
    int64_t last_send_us = 0;

    ESP_LOGI(TAG, "WebSocket broadcast task started");

    while (1) {
        /* Wait for new ADC frame */
        if (adc_sampler_get_frame(&frame) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        int client_count = ws_snapshot_clients(client_fds, MAX_WS_CLIENTS);
        if (client_count == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int64_t now_us = esp_timer_get_time();
        if ((now_us - last_send_us) < WS_FRAME_INTERVAL_US) {
            int64_t remaining_us = WS_FRAME_INTERVAL_US - (now_us - last_send_us);
            if (remaining_us > 1000) {
                vTaskDelay(pdMS_TO_TICKS((remaining_us + 999) / 1000));
            } else {
                taskYIELD();
            }
            continue;
        }

        /* Pack frame */
        size_t frame_len = pack_frame(&frame, tx_buf, sizeof(tx_buf));
        if (frame_len == 0) {
            ESP_LOGE(TAG, "Frame packing failed");
            continue;
        }

        /* Send to all connected WebSocket clients */
        httpd_ws_frame_t ws_pkt = {
            .final  = true,
            .fragmented = false,
            .type   = HTTPD_WS_TYPE_BINARY,
            .payload = tx_buf,
            .len    = frame_len,
        };

        bool sent_any = false;
        for (int i = 0; i < client_count; i++) {
            if (!ws_should_send_now(client_fds[i], now_us)) {
                continue;
            }

            errno = 0;
            esp_err_t ret = httpd_ws_send_frame_async(server_handle, client_fds[i], &ws_pkt);
            int send_errno = errno;
            ws_report_send_result(client_fds[i], ret, send_errno, now_us);
            sent_any = true;
        }

        if (sent_any) {
            last_send_us = now_us;
        }
    }
}

/* ------------------------------------------------------------------ */
/* HTTP handlers                                                        */
/* ------------------------------------------------------------------ */

static esp_err_t index_html_handler(httpd_req_t *req)
{
    size_t len = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "identity");
    httpd_resp_send(req, (const char *)index_html_start, len);
    return ESP_OK;
}

static esp_err_t app_js_handler(httpd_req_t *req)
{
    size_t len = app_js_end - app_js_start;
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)app_js_start, len);
    return ESP_OK;
}

static esp_err_t style_css_handler(httpd_req_t *req)
{
    size_t len = style_css_end - style_css_start;
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)style_css_start, len);
    return ESP_OK;
}

/* API: GET /api/info */
static esp_err_t api_info_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", "ESP32-S3 Oscilloscope");
    cJSON_AddNumberToObject(root, "sample_rate", adc_sampler_get_rate());
    cJSON_AddNumberToObject(root, "buffer_size", ADC_BUFFER_SIZE);
    cJSON_AddNumberToObject(root, "channels", ADC_CHANNEL_COUNT);
    cJSON_AddNumberToObject(root, "vref_mv", ADC_VREF_MV);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());

    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

/* API: POST /api/config  body: {"sample_rate": 10000} */
static esp_err_t api_config_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *rate_item = cJSON_GetObjectItem(root, "sample_rate");
    if (cJSON_IsNumber(rate_item)) {
        adc_sampler_set_rate((uint32_t)rate_item->valuedouble);
    }

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* WebSocket handler */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* New WebSocket connection */
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WebSocket handshake, fd=%d", fd);

        if (ws_add_client(fd)) {
            ESP_LOGI(TAG, "WS client connected, total=%d", ws_get_client_count());
        } else {
            ESP_LOGW(TAG, "Max WS clients reached");
        }
        return ESP_OK;
    }

    /* Receive frame (handle ping/close) */
    httpd_ws_frame_t ws_pkt;
    uint8_t buf[128] = {0};
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.payload = buf;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, sizeof(buf));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS recv error: %s", esp_err_to_name(ret));
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WS client closed, fd=%d", fd);
        ws_remove_client_by_fd(fd);
    } else if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_pkt.len > 0) {
        /* Handle text commands from client */
        buf[ws_pkt.len] = '\0';
        ESP_LOGI(TAG, "WS received: %s", buf);

        cJSON *cmd = cJSON_Parse((char *)buf);
        if (cmd) {
            cJSON *rate_item = cJSON_GetObjectItem(cmd, "sample_rate");
            if (cJSON_IsNumber(rate_item)) {
                adc_sampler_set_rate((uint32_t)rate_item->valuedouble);
            }
            cJSON_Delete(cmd);
        }
    }

    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Server start/stop                                                    */
/* ------------------------------------------------------------------ */

httpd_handle_t http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 10;
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.send_wait_timeout = 1;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    if (ws_clients_mutex == NULL) {
        ws_clients_mutex = xSemaphoreCreateMutex();
        if (ws_clients_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create WS client mutex");
            return NULL;
        }
    }

    if (xSemaphoreTake(ws_clients_mutex, portMAX_DELAY) == pdTRUE) {
        ws_client_count = 0;
        memset(ws_fds, 0, sizeof(ws_fds));
        memset(ws_send_fail_counts, 0, sizeof(ws_send_fail_counts));
        memset(ws_retry_after_us, 0, sizeof(ws_retry_after_us));
        xSemaphoreGive(ws_clients_mutex);
    }

    if (httpd_start(&server_handle, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    /* Register URI handlers */
    httpd_uri_t uri_index = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = index_html_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server_handle, &uri_index);

    httpd_uri_t uri_js = {
        .uri      = "/app.js",
        .method   = HTTP_GET,
        .handler  = app_js_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server_handle, &uri_js);

    httpd_uri_t uri_css = {
        .uri      = "/style.css",
        .method   = HTTP_GET,
        .handler  = style_css_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server_handle, &uri_css);

    httpd_uri_t uri_info = {
        .uri      = "/api/info",
        .method   = HTTP_GET,
        .handler  = api_info_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server_handle, &uri_info);

    httpd_uri_t uri_config = {
        .uri      = "/api/config",
        .method   = HTTP_POST,
        .handler  = api_config_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server_handle, &uri_config);

    httpd_uri_t uri_ws = {
        .uri          = "/ws",
        .method       = HTTP_GET,
        .handler      = ws_handler,
        .user_ctx     = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server_handle, &uri_ws);

    /* Start WebSocket broadcast task */
    xTaskCreatePinnedToCore(
        ws_broadcast_task,
        "ws_broadcast",
        8192,
        NULL,
        4,
        &ws_task_handle,
        1   /* Keep core 0 freer for the Wi-Fi driver/task */
    );

    ESP_LOGI(TAG, "HTTP server started successfully");
    return server_handle;
}

void http_server_stop(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
        server_handle = NULL;
    }
    if (ws_task_handle) {
        vTaskDelete(ws_task_handle);
        ws_task_handle = NULL;
    }

    if (ws_clients_mutex && xSemaphoreTake(ws_clients_mutex, portMAX_DELAY) == pdTRUE) {
        ws_client_count = 0;
        memset(ws_fds, 0, sizeof(ws_fds));
        memset(ws_send_fail_counts, 0, sizeof(ws_send_fail_counts));
        memset(ws_retry_after_us, 0, sizeof(ws_retry_after_us));
        xSemaphoreGive(ws_clients_mutex);
    }
}
