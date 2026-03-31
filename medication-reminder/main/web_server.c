#include "web_server.h"
#include "reminder.h"
#include "storage.h"
#include "wifi_manager.h"
#include "time_manager.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server = NULL;

/* ---- GET /api/reminders ---- */
static esp_err_t get_reminders_handler(httpd_req_t *req)
{
    uint8_t count = 0;
    reminder_t *reminders = reminder_get_all(&count);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "id", reminders[i].config.id);
        cJSON_AddNumberToObject(obj, "hour", reminders[i].config.hour);
        cJSON_AddNumberToObject(obj, "minute", reminders[i].config.minute);
        cJSON_AddStringToObject(obj, "medication_name", reminders[i].config.medication_name);
        cJSON_AddStringToObject(obj, "dosage", reminders[i].config.dosage);
        cJSON_AddBoolToObject(obj, "enabled", reminders[i].config.enabled);
        cJSON_AddItemToArray(arr, obj);
    }
    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json ? json : "[]");
    if (json) free(json);
    return ESP_OK;
}

/* ---- POST /api/reminders ---- */
static esp_err_t post_reminders_handler(httpd_req_t *req)
{
    char buf[1024] = {0};
    int total = req->content_len;
    if (total >= (int)sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large");
        return ESP_FAIL;
    }
    int received = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recv error");
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = '\0';

    cJSON *arr = cJSON_Parse(buf);
    if (!arr || !cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    uint8_t rem_count = 0;
    reminder_t *reminders = reminder_get_all(&rem_count);
    int n = cJSON_GetArraySize(arr);
    if (n > (int)rem_count) n = (int)rem_count;
    for (int i = 0; i < n; i++) {
        cJSON *obj = cJSON_GetArrayItem(arr, i);
        cJSON *hour = cJSON_GetObjectItem(obj, "hour");
        cJSON *minute = cJSON_GetObjectItem(obj, "minute");
        cJSON *name = cJSON_GetObjectItem(obj, "medication_name");
        cJSON *dosage = cJSON_GetObjectItem(obj, "dosage");
        cJSON *enabled = cJSON_GetObjectItem(obj, "enabled");
        if (hour) reminders[i].config.hour = (uint8_t)hour->valueint;
        if (minute) reminders[i].config.minute = (uint8_t)minute->valueint;
        if (name) strncpy(reminders[i].config.medication_name, name->valuestring,
                          sizeof(reminders[i].config.medication_name) - 1);
        if (dosage) strncpy(reminders[i].config.dosage, dosage->valuestring,
                            sizeof(reminders[i].config.dosage) - 1);
        if (enabled) reminders[i].config.enabled = cJSON_IsTrue(enabled);
    }
    cJSON_Delete(arr);

    storage_save_reminders(reminders, rem_count);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"result\":\"ok\"}");
    return ESP_OK;
}

/* ---- GET /api/log ---- */
static esp_err_t get_log_handler(httpd_req_t *req)
{
    char *buf = malloc(8192);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    storage_read_log(buf, 8192, 0);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, buf);
    free(buf);
    return ESP_OK;
}

/* ---- DELETE /api/log ---- */
static esp_err_t delete_log_handler(httpd_req_t *req)
{
    storage_clear_log();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"result\":\"ok\"}");
    return ESP_OK;
}

/* ---- POST /api/wifi ---- */
static esp_err_t post_wifi_handler(httpd_req_t *req)
{
    char buf[256] = {0};
    int total = req->content_len;
    if (total >= (int)sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large");
        return ESP_FAIL;
    }
    int received = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0) break;
        received += ret;
    }
    buf[received] = '\0';

    cJSON *obj = cJSON_Parse(buf);
    if (!obj) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    cJSON *ssid = cJSON_GetObjectItem(obj, "ssid");
    cJSON *pass = cJSON_GetObjectItem(obj, "password");
    if (ssid && pass) {
        storage_save_wifi(ssid->valuestring, pass->valuestring);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_sendstr(req, "{\"result\":\"ok\",\"msg\":\"Saved, restarting WiFi\"}");
        cJSON_Delete(obj);
        vTaskDelay(pdMS_TO_TICKS(500));
        wifi_manager_start_sta(ssid->valuestring, pass->valuestring);
    } else {
        cJSON_Delete(obj);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid/password");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ---- GET /api/status ---- */
static esp_err_t get_status_handler(httpd_req_t *req)
{
    char time_str[32], date_str[32];
    time_manager_get_time_str(time_str, sizeof(time_str));
    time_manager_get_date_str(date_str, sizeof(date_str));

    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "time", time_str);
    cJSON_AddStringToObject(obj, "date", date_str);
    cJSON_AddStringToObject(obj, "ip", wifi_manager_get_ip());
    cJSON_AddBoolToObject(obj, "wifi_connected", wifi_manager_is_connected());
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json ? json : "{}");
    if (json) free(json);
    return ESP_OK;
}

/* ---- GET / (serve index.html from SPIFFS) ---- */
static esp_err_t root_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/www/index.html", "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "index.html not found");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        httpd_resp_send_chunk(req, chunk, n);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* ---- OPTIONS (CORS preflight) ---- */
static esp_err_t options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 8192;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    httpd_uri_t uris[] = {
        { .uri = "/",                .method = HTTP_GET,    .handler = root_handler },
        { .uri = "/api/reminders",   .method = HTTP_GET,    .handler = get_reminders_handler },
        { .uri = "/api/reminders",   .method = HTTP_POST,   .handler = post_reminders_handler },
        { .uri = "/api/log",         .method = HTTP_GET,    .handler = get_log_handler },
        { .uri = "/api/log",         .method = HTTP_DELETE, .handler = delete_log_handler },
        { .uri = "/api/wifi",        .method = HTTP_POST,   .handler = post_wifi_handler },
        { .uri = "/api/status",      .method = HTTP_GET,    .handler = get_status_handler },
        { .uri = "/api/reminders",   .method = HTTP_OPTIONS,.handler = options_handler },
        { .uri = "/api/log",         .method = HTTP_OPTIONS,.handler = options_handler },
        { .uri = "/api/wifi",        .method = HTTP_OPTIONS,.handler = options_handler },
    };

    for (int i = 0; i < (int)(sizeof(uris) / sizeof(uris[0])); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }

    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}