#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include "web_server.h"
#include "wifi_scanner.h"

static const char *TAG = "WEB";

/* ------------------------------------------------------------------ */
/*  EMBED_TXTFILES 生成的符号                                           */
/*  编译时 ESP-IDF 将 index.html 内容嵌入固件，并暴露以下两个符号：     */
/*    _binary_index_html_start  — 文件内容起始地址                      */
/*    _binary_index_html_end    — 文件内容结束地址（末尾有 '\0'）        */
/* ------------------------------------------------------------------ */
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

/* 共享数据保护互斥锁 */
static SemaphoreHandle_t s_data_mutex = NULL;

/* 服务器持有的最新扫描结果 */
static wifi_scan_result_t s_scan_data;
static bool s_data_ready = false;

/* ------------------------------------------------------------------ */
/*  HTTP 处理函数：GET /                                                */
/* ------------------------------------------------------------------ */
static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    /* 使用 EMBED_TXTFILES 嵌入的 index.html 内容
     * 长度 = end - start（不含末尾 '\0'） */
    size_t html_len = (size_t)(index_html_end - index_html_start);
    httpd_resp_send(req, index_html_start, (ssize_t)html_len);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  HTTP 处理函数：GET /api/scan                                        */
/* ------------------------------------------------------------------ */
static esp_err_t handler_scan_api(httpd_req_t *req)
{
    /* 构建 JSON 响应，使用固定大小栈缓冲区 */
    static char json_buf[4096];
    int pos = 0;

    pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "{\"networks\":[");

    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        for (int i = 0; i < s_scan_data.count && pos < (int)sizeof(json_buf) - 128; i++) {
            const wifi_network_t *net = &s_scan_data.networks[i];

            /* 转义 SSID 中的双引号和反斜杠 */
            char safe_ssid[70] = {0};
            int si = 0;
            for (int j = 0; net->ssid[j] && si < 65; j++) {
                if (net->ssid[j] == '"' || net->ssid[j] == '\\') {
                    safe_ssid[si++] = '\\';
                }
                safe_ssid[si++] = net->ssid[j];
            }

            pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
                            "%s{"
                            "\"ssid\":\"%s\","
                            "\"rssi\":%d,"
                            "\"channel\":%d,"
                            "\"auth\":\"%s\","
                            "\"open\":%s,"
                            "\"bssid\":\"%02X:%02X:%02X:%02X:%02X:%02X\""
                            "}",
                            (i > 0) ? "," : "",
                            safe_ssid,
                            net->rssi,
                            net->channel,
                            wifi_auth_mode_str(net->auth_mode),
                            (net->auth_mode == WIFI_AUTH_OPEN) ? "true" : "false",
                            net->bssid[0], net->bssid[1], net->bssid[2],
                            net->bssid[3], net->bssid[4], net->bssid[5]);
        }
        xSemaphoreGive(s_data_mutex);
    }

    pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_buf, pos);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  web_server_init                                                     */
/* ------------------------------------------------------------------ */
esp_err_t web_server_init(void)
{
    s_data_mutex = xSemaphoreCreateMutex();
    if (s_data_mutex == NULL) {
        ESP_LOGE(TAG, "互斥锁创建失败");
        return ESP_ERR_NO_MEM;
    }

    memset(&s_scan_data, 0, sizeof(s_scan_data));

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size      = 8192;
    config.max_uri_handlers = 8;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务器启动失败: %s", esp_err_to_name(err));
        return err;
    }

    /* 注册路由 */
    httpd_uri_t uri_root = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = handler_root,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &uri_root);

    httpd_uri_t uri_scan = {
        .uri      = "/api/scan",
        .method   = HTTP_GET,
        .handler  = handler_scan_api,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &uri_scan);

    ESP_LOGI(TAG, "HTTP 服务器已启动，访问 http://%s", wifi_scanner_get_ap_ip());
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  web_server_update_data                                              */
/* ------------------------------------------------------------------ */
void web_server_update_data(const wifi_scan_result_t *result)
{
    if (result == NULL || s_data_mutex == NULL) return;
    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        memcpy(&s_scan_data, result, sizeof(wifi_scan_result_t));
        s_data_ready = true;
        xSemaphoreGive(s_data_mutex);
    }
}