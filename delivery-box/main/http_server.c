/**
 * @file http_server.c
 * @brief HTTP REST API 服务器实现
 */

#include "http_server.h"
#include "locker.h"
#include "wifi_manager.h"
#include "config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "http_srv";
static httpd_handle_t s_server = NULL;

// 嵌入的 Web 页面文件 (由 CMakeLists.txt EMBED_FILES 指令生成)
extern const uint8_t s_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t s_index_html_end[]   asm("_binary_index_html_end");

// ============================================================
// 辅助函数
// ============================================================

static esp_err_t send_json(httpd_req_t *req, int status_code, cJSON *json)
{
    char *json_str = cJSON_PrintUnformatted(json);
    if (!json_str) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (status_code == 200)      httpd_resp_set_status(req, "200 OK");
    else if (status_code == 400) httpd_resp_set_status(req, "400 Bad Request");
    else if (status_code == 404) httpd_resp_set_status(req, "404 Not Found");
    else if (status_code == 409) httpd_resp_set_status(req, "409 Conflict");
    else                         httpd_resp_set_status(req, "500 Internal Server Error");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t ret = httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return ret;
}

static int read_request_body(httpd_req_t *req, char *buf, size_t buf_len)
{
    int total_len = req->content_len;
    if (total_len <= 0 || total_len >= (int)buf_len) return -1;

    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return -1;
        }
        received += ret;
    }
    buf[received] = '\0';
    return received;
}

static int extract_locker_id(const char *uri)
{
    const char *prefix = "/api/lockers/";
    const char *p = strstr(uri, prefix);
    if (!p) return -1;
    p += strlen(prefix);
    int id = atoi(p);
    if (id < 0 || id >= LOCKER_COUNT) return -1;
    return id;
}

static cJSON *locker_info_to_json(const locker_info_t *info)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", info->id);

    const char *state_names[] = {"empty", "occupied", "unlocked", "locked_out"};
    int state_idx = (int)info->state;
    if (state_idx > 3) state_idx = 3;
    cJSON_AddStringToObject(obj, "state", state_names[state_idx]);
    cJSON_AddBoolToObject(obj, "door_open", info->door_open);
    cJSON_AddNumberToObject(obj, "fail_count", info->fail_count);
    cJSON_AddNumberToObject(obj, "deposit_time", (double)info->deposit_time);
    return obj;
}

// ============================================================
// HTTP 处理器
// ============================================================

// GET /  - 返回嵌入的 index.html
static esp_err_t handler_root(httpd_req_t *req)
{
    size_t html_len = s_index_html_end - s_index_html_start;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    return httpd_resp_send(req, (const char *)s_index_html_start, html_len);
}

// GET /api/status
static esp_err_t handler_get_status(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    wifi_state_t ws = wifi_manager_get_state();
    const char *ws_names[] = {"disconnected","connecting","connected","ap_only","ap_sta"};
    cJSON_AddStringToObject(root, "wifi_state",
                             ws_names[(int)ws < 5 ? (int)ws : 0]);

    char ip[20] = "N/A";
    if (!wifi_manager_get_ip(ip, sizeof(ip))) {
        wifi_manager_get_ap_ip(ip, sizeof(ip));
    }
    cJSON_AddStringToObject(root, "ip_address", ip);

    char ssid[33] = {0};
    wifi_manager_get_ssid(ssid, sizeof(ssid));
    cJSON_AddStringToObject(root, "ssid", ssid);

    cJSON_AddNumberToObject(root, "total_lockers", LOCKER_COUNT);
    cJSON_AddNumberToObject(root, "free_lockers", locker_get_empty_count());

    esp_err_t ret = send_json(req, 200, root);
    cJSON_Delete(root);
    return ret;
}

// GET /api/lockers
static esp_err_t handler_get_lockers(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();

    for (int i = 0; i < LOCKER_COUNT; i++) {
        locker_info_t info;
        locker_get_info(i, &info);
        cJSON_AddItemToArray(arr, locker_info_to_json(&info));
    }
    cJSON_AddItemToObject(root, "lockers", arr);

    esp_err_t ret = send_json(req, 200, root);
    cJSON_Delete(root);
    return ret;
}

// GET /api/lockers/{id}
static esp_err_t handler_get_locker(httpd_req_t *req)
{
    int id = extract_locker_id(req->uri);
    if (id < 0) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", "Invalid locker ID");
        esp_err_t ret = send_json(req, 404, err);
        cJSON_Delete(err);
        return ret;
    }

    locker_info_t info;
    locker_get_info(id, &info);
    cJSON *root = locker_info_to_json(&info);
    esp_err_t ret = send_json(req, 200, root);
    cJSON_Delete(root);
    return ret;
}

// POST /api/lockers/{id}/deposit  (id=-1 means auto-select)
static esp_err_t handler_deposit(httpd_req_t *req)
{
    int id = extract_locker_id(req->uri);

    char body[256] = {0};
    read_request_body(req, body, sizeof(body));

    char custom_code[8] = {0};
    if (strlen(body) > 0) {
        cJSON *json = cJSON_Parse(body);
        if (json) {
            cJSON *code_item = cJSON_GetObjectItem(json, "code");
            if (code_item && cJSON_IsString(code_item)) {
                strncpy(custom_code, code_item->valuestring,
                        sizeof(custom_code) - 1);
            }
            cJSON_Delete(json);
        }
    }

    uint8_t out_id = 0;
    char out_code[8] = {0};
    locker_result_t result = locker_deposit(
        (id < 0) ? 0xFF : (uint8_t)id,
        strlen(custom_code) > 0 ? custom_code : NULL,
        &out_id, out_code);

    cJSON *root = cJSON_CreateObject();
    if (result == LOCKER_OK) {
        cJSON_AddStringToObject(root, "status", "ok");
        cJSON_AddNumberToObject(root, "locker_id", out_id);
        cJSON_AddStringToObject(root, "code", out_code);
        cJSON_AddStringToObject(root, "message",
                                 "Deposit successful. Locker is open.");
        esp_err_t ret = send_json(req, 200, root);
        cJSON_Delete(root);
        return ret;
    } else {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message",
            result == LOCKER_ERR_STATE ? "No empty locker available"
                                       : "Invalid request");
        esp_err_t ret = send_json(req, 409, root);
        cJSON_Delete(root);
        return ret;
    }
}

// POST /api/lockers/{id}/pickup
static esp_err_t handler_pickup(httpd_req_t *req)
{
    char body[256] = {0};
    int len = read_request_body(req, body, sizeof(body));
    cJSON *root = cJSON_CreateObject();

    if (len <= 0) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "Request body required");
        esp_err_t ret = send_json(req, 400, root);
        cJSON_Delete(root);
        return ret;
    }

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "Invalid JSON");
        esp_err_t ret = send_json(req, 400, root);
        cJSON_Delete(root);
        return ret;
    }

    cJSON *code_item = cJSON_GetObjectItem(json, "code");
    if (!code_item || !cJSON_IsString(code_item)) {
        cJSON_Delete(json);
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "Missing 'code' field");
        esp_err_t ret = send_json(req, 400, root);
        cJSON_Delete(root);
        return ret;
    }

    const char *code = code_item->valuestring;
    uint8_t out_id = 0;
    locker_result_t result = locker_pickup(code, &out_id);
    cJSON_Delete(json);

    if (result == LOCKER_OK) {
        cJSON_AddStringToObject(root, "status", "ok");
        cJSON_AddNumberToObject(root, "locker_id", out_id);
        cJSON_AddStringToObject(root, "message",
                                 "Pickup successful. Locker is open.");
        esp_err_t ret = send_json(req, 200, root);
        cJSON_Delete(root);
        return ret;
    } else {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message",
            result == LOCKER_ERR_CODE ? "Invalid pickup code"
                                      : "Operation failed");
        esp_err_t ret = send_json(req, 400, root);
        cJSON_Delete(root);
        return ret;
    }
}

// POST /api/lockers/{id}/unlock
static esp_err_t handler_force_unlock(httpd_req_t *req)
{
    int id = extract_locker_id(req->uri);
    cJSON *root = cJSON_CreateObject();

    if (id < 0) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "Invalid locker ID");
        esp_err_t ret = send_json(req, 404, root);
        cJSON_Delete(root);
        return ret;
    }

    locker_result_t result = locker_force_unlock((uint8_t)id);
    if (result == LOCKER_OK) {
        cJSON_AddStringToObject(root, "status", "ok");
        cJSON_AddStringToObject(root, "message", "Locker unlocked");
    } else {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "Failed to unlock");
    }
    esp_err_t ret = send_json(req, result == LOCKER_OK ? 200 : 500, root);
    cJSON_Delete(root);
    return ret;
}

// POST /api/lockers/{id}/lock
static esp_err_t handler_force_lock(httpd_req_t *req)
{
    int id = extract_locker_id(req->uri);
    cJSON *root = cJSON_CreateObject();

    if (id < 0) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "Invalid locker ID");
        esp_err_t ret = send_json(req, 404, root);
        cJSON_Delete(root);
        return ret;
    }

    locker_result_t result = locker_force_lock((uint8_t)id);
    cJSON_AddStringToObject(root, "status", result == LOCKER_OK ? "ok" : "error");
    cJSON_AddStringToObject(root, "message",
        result == LOCKER_OK ? "Locker locked" : "Failed to lock");
    esp_err_t ret = send_json(req, result == LOCKER_OK ? 200 : 500, root);
    cJSON_Delete(root);
    return ret;
}

// POST /api/lockers/{id}/reset
static esp_err_t handler_reset(httpd_req_t *req)
{
    int id = extract_locker_id(req->uri);
    cJSON *root = cJSON_CreateObject();

    if (id < 0) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "Invalid locker ID");
        esp_err_t ret = send_json(req, 404, root);
        cJSON_Delete(root);
        return ret;
    }

    locker_result_t result = locker_reset((uint8_t)id);
    cJSON_AddStringToObject(root, "status", result == LOCKER_OK ? "ok" : "error");
    cJSON_AddStringToObject(root, "message",
        result == LOCKER_OK ? "Locker reset to empty" : "Failed to reset");
    esp_err_t ret = send_json(req, result == LOCKER_OK ? 200 : 500, root);
    cJSON_Delete(root);
    return ret;
}

// POST /api/wifi
static esp_err_t handler_set_wifi(httpd_req_t *req)
{
    char body[256] = {0};
    int len = read_request_body(req, body, sizeof(body));
    cJSON *root = cJSON_CreateObject();

    if (len <= 0) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "Request body required");
        esp_err_t ret = send_json(req, 400, root);
        cJSON_Delete(root);
        return ret;
    }

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "Invalid JSON");
        esp_err_t ret = send_json(req, 400, root);
        cJSON_Delete(root);
        return ret;
    }

    cJSON *ssid_item = cJSON_GetObjectItem(json, "ssid");
    cJSON *pass_item = cJSON_GetObjectItem(json, "password");

    if (!ssid_item || !cJSON_IsString(ssid_item)) {
        cJSON_Delete(json);
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddStringToObject(root, "message", "Missing 'ssid' field");
        esp_err_t ret = send_json(req, 400, root);
        cJSON_Delete(root);
        return ret;
    }

    const char *ssid = ssid_item->valuestring;
    const char *pass = (pass_item && cJSON_IsString(pass_item)) ?
                        pass_item->valuestring : "";

    wifi_manager_connect(ssid, pass);
    cJSON_Delete(json);

    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "message",
                             "WiFi config saved. Reconnecting...");
    esp_err_t ret = send_json(req, 200, root);
    cJSON_Delete(root);
    return ret;
}

// ============================================================
// URI 注册表
// ============================================================

static const httpd_uri_t s_uris[] = {
    // Web 界面
    { .uri = "/",                       .method = HTTP_GET,  .handler = handler_root },
    // 系统状态
    { .uri = "/api/status",             .method = HTTP_GET,  .handler = handler_get_status },
    // 格口列表
    { .uri = "/api/lockers",            .method = HTTP_GET,  .handler = handler_get_lockers },
    // 单个格口 (使用通配符匹配 /api/lockers/0 ~ /api/lockers/3)
    { .uri = "/api/lockers/*",          .method = HTTP_GET,  .handler = handler_get_locker },
    // 存件 (自动选择格口)
    { .uri = "/api/lockers/deposit",    .method = HTTP_POST, .handler = handler_deposit },
    // 存件 (指定格口)
    { .uri = "/api/lockers/*/deposit",  .method = HTTP_POST, .handler = handler_deposit },
    // 取件
    { .uri = "/api/lockers/*/pickup",   .method = HTTP_POST, .handler = handler_pickup },
    // 强制解锁
    { .uri = "/api/lockers/*/unlock",   .method = HTTP_POST, .handler = handler_force_unlock },
    // 强制锁定
    { .uri = "/api/lockers/*/lock",     .method = HTTP_POST, .handler = handler_force_lock },
    // 重置格口
    { .uri = "/api/lockers/*/reset",    .method = HTTP_POST, .handler = handler_reset },
    // WiFi 配置
    { .uri = "/api/wifi",               .method = HTTP_POST, .handler = handler_set_wifi },
};

// ============================================================
// 启动/停止
// ============================================================

esp_err_t http_server_start(void)
{
    if (s_server) {
        ESP_LOGW(TAG, "HTTP服务器已在运行");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = HTTP_SERVER_PORT;
    config.max_uri_handlers = 16;
    config.uri_match_fn     = httpd_uri_match_wildcard;
    config.stack_size       = 8192;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP服务器启动失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 注册所有 URI 处理器
    for (size_t i = 0; i < sizeof(s_uris) / sizeof(s_uris[0]); i++) {
        ret = httpd_register_uri_handler(s_server, &s_uris[i]);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "注册URI '%s' 失败: %s",
                     s_uris[i].uri, esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "HTTP服务器启动成功，端口: %d", HTTP_SERVER_PORT);
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (!s_server) return ESP_OK;
    esp_err_t ret = httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "HTTP服务器已停止");
    return ret;
}