#include "web_server.h"
#include "web_ui.h"
#include "content_store.h"
#include "config.h"
#include "wifi_manager.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "web_server";

static httpd_handle_t s_server = NULL;
static content_update_cb_t s_content_cb = NULL;
static wifi_config_cb_t s_wifi_cb = NULL;

// ============================================================
// 辅助：读取请求 body
// ============================================================
static char *read_body(httpd_req_t *req) {
    int total = req->content_len;
    if (total <= 0 || total > 4096) return NULL;

    char *buf = malloc(total + 1);
    if (!buf) return NULL;

    int received = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0) {
            free(buf);
            return NULL;
        }
        received += ret;
    }
    buf[total] = '\0';
    return buf;
}

// ============================================================
// GET / → 返回控制页面 HTML
// ============================================================
static esp_err_t handler_root(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_send(req, WEB_UI_HTML, strlen(WEB_UI_HTML));
    return ESP_OK;
}

// ============================================================
// GET /api/content → 返回当前菜单 JSON
// ============================================================
static esp_err_t handler_get_content(httpd_req_t *req) {
    menu_content_t content;
    content_store_load(&content);

    char *json_buf = malloc(4096);
    if (!json_buf) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    int len = content_to_json(&content, json_buf, 4096);
    if (len <= 0) {
        free(json_buf);
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_buf, len);
    free(json_buf);
    return ESP_OK;
}

// ============================================================
// POST /api/content → 更新菜单内容
// ============================================================
static esp_err_t handler_post_content(httpd_req_t *req) {
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request body");
        return ESP_OK;
    }

    menu_content_t content;
    bool ok = content_from_json(body, &content);
    free(body);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (!ok) {
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"JSON parse failed\"}", -1);
        return ESP_OK;
    }

    // 保存到 NVS
    bool saved = content_store_save(&content);
    if (!saved) {
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Save failed\"}", -1);
        return ESP_OK;
    }

    // 触发显示更新回调
    if (s_content_cb) {
        s_content_cb(&content);
    }

    httpd_resp_send(req, "{\"ok\":true}", -1);
    ESP_LOGI(TAG, "Content updated via web: %s", content.restaurant_name);
    return ESP_OK;
}

// ============================================================
// GET /api/status → 设备状态
// ============================================================
static esp_err_t handler_status(httpd_req_t *req) {
    wifi_status_t wifi_status;
    wifi_manager_get_status(&wifi_status);

    char resp[256];
    snprintf(resp, sizeof(resp),
        "{\"ok\":true,\"ip\":\"%s\",\"ssid\":\"%s\","
        "\"rssi\":%d,\"mode\":\"%s\",\"version\":\"%s\"}",
        wifi_status.ip_addr,
        wifi_status.ssid,
        wifi_status.rssi,
        wifi_status.state == WIFI_STATE_AP_MODE ? "ap" : "sta",
        FIRMWARE_VERSION
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

// ============================================================
// POST /api/wifi → 配置 WiFi
// ============================================================
static esp_err_t handler_wifi(httpd_req_t *req) {
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_OK;
    }

    // 简单解析 JSON（不用 cJSON 避免依赖）
    char ssid[MAX_WIFI_SSID_LEN] = {0};
    char password[MAX_WIFI_PASS_LEN] = {0};

    // 使用 cJSON 解析
    cJSON *root = cJSON_Parse(body);
    free(body);

    if (!root) {
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"JSON parse failed\"}", -1);
        return ESP_OK;
    }

    cJSON *j_ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *j_pass = cJSON_GetObjectItem(root, "password");

    if (cJSON_IsString(j_ssid)) strncpy(ssid, j_ssid->valuestring, MAX_WIFI_SSID_LEN - 1);
    if (cJSON_IsString(j_pass)) strncpy(password, j_pass->valuestring, MAX_WIFI_PASS_LEN - 1);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (strlen(ssid) == 0) {
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"SSID empty\"}", -1);
        return ESP_OK;
    }

    httpd_resp_send(req, "{\"ok\":true}", -1);

    // 异步触发 WiFi 连接（避免阻塞 HTTP 响应）
    if (s_wifi_cb) {
        s_wifi_cb(ssid, password);
    }

    ESP_LOGI(TAG, "WiFi config received: SSID=%s", ssid);
    return ESP_OK;
}

// ============================================================
// POST /api/reset → 恢复出厂设置
// ============================================================
static esp_err_t handler_reset(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true,\"message\":\"Resetting...\"}", -1);

    vTaskDelay(pdMS_TO_TICKS(500));
    content_store_erase_all();
    esp_restart();
    return ESP_OK;
}

// ============================================================
// URI 路由表
// ============================================================
static const httpd_uri_t ROUTES[] = {
    {.uri = "/",            .method = HTTP_GET,  .handler = handler_root},
    {.uri = "/api/content", .method = HTTP_GET,  .handler = handler_get_content},
    {.uri = "/api/content", .method = HTTP_POST, .handler = handler_post_content},
    {.uri = "/api/status",  .method = HTTP_GET,  .handler = handler_status},
    {.uri = "/api/wifi",    .method = HTTP_POST, .handler = handler_wifi},
    {.uri = "/api/reset",   .method = HTTP_POST, .handler = handler_reset},
};

// ============================================================
// 启动 / 停止
// ============================================================
void web_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    config.max_open_sockets = WEB_MAX_OPEN_SOCKETS;
    config.lru_purge_enable = true;
    config.stack_size = WEB_TASK_STACK_SIZE;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    for (int i = 0; i < sizeof(ROUTES) / sizeof(ROUTES[0]); i++) {
        httpd_register_uri_handler(s_server, &ROUTES[i]);
    }

    ESP_LOGI(TAG, "Web server started on port %d", WEB_SERVER_PORT);
}

void web_server_stop(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

void web_server_set_content_callback(content_update_cb_t cb) {
    s_content_cb = cb;
}

void web_server_set_wifi_callback(wifi_config_cb_t cb) {
    s_wifi_cb = cb;
}
