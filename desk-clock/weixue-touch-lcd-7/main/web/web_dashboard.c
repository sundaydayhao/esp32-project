/**
 * @file web_dashboard.c
 * @brief Lightweight HTTP portal for WiFi provisioning
 */
#include "web/web_dashboard.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/ntp_client.h"
#include "net/weather_client.h"
#include "net/wifi_manager.h"

#define WEB_APPLY_TASK_STACK   (4 * 1024)
#define WEB_APPLY_TASK_PRIO    4
#define WEB_APPLY_DELAY_MS     300

typedef enum {
    WIFI_APPLY_MODE_CONNECT = 0,
    WIFI_APPLY_MODE_RESET = 1,
} wifi_apply_mode_t;

typedef struct {
    wifi_apply_mode_t mode;
    char ssid[WIFI_SSID_MAX_LEN];
    char password[WIFI_PASS_MAX_LEN];
} wifi_apply_ctx_t;

static httpd_handle_t s_server = NULL;

extern const char web_dashboard_html_start[] asm("_binary_dashboard_html_start");
extern const char web_dashboard_html_end[] asm("_binary_dashboard_html_end");
extern const char web_provision_html_start[] asm("_binary_provision_html_start");
extern const char web_provision_html_end[] asm("_binary_provision_html_end");

static void wifi_apply_task(void *arg)
{
    wifi_apply_ctx_t *ctx = (wifi_apply_ctx_t *)arg;
    esp_err_t ret = ESP_OK;

    vTaskDelay(pdMS_TO_TICKS(WEB_APPLY_DELAY_MS));

    if (ctx->mode == WIFI_APPLY_MODE_RESET) {
        ret = wifi_manager_reset_credentials_and_start_provisioning();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_WEB, "wifi reset failed: %s", esp_err_to_name(ret));
        }
    } else {
        ret = wifi_manager_connect_with_credentials(ctx->ssid, ctx->password);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_WEB, "wifi connect apply failed: %s", esp_err_to_name(ret));
        }
    }

    free(ctx);
    vTaskDelete(NULL);
}

static esp_err_t schedule_wifi_apply(wifi_apply_mode_t mode, const char *ssid, const char *password)
{
    wifi_apply_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }

    ctx->mode = mode;
    strlcpy(ctx->ssid, ssid ? ssid : "", sizeof(ctx->ssid));
    strlcpy(ctx->password, password ? password : "", sizeof(ctx->password));

    if (xTaskCreate(wifi_apply_task, "wifi_apply",
                    WEB_APPLY_TASK_STACK, ctx,
                    WEB_APPLY_TASK_PRIO, NULL) != pdPASS) {
        free(ctx);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t send_embedded_page(httpd_req_t *req, const char *html_start, const char *html_end)
{
    size_t html_len = (size_t)(html_end - html_start);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, html_start, html_len);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    if (wifi_manager_is_connected()) {
        return send_embedded_page(req, web_dashboard_html_start, web_dashboard_html_end);
    }
    return send_embedded_page(req, web_provision_html_start, web_provision_html_end);
}

static esp_err_t redirect_to_root_handler(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;

    if (req->method != HTTP_GET && req->method != HTTP_HEAD) {
        return ESP_FAIL;
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");

    if (req->method == HTTP_HEAD) {
        return httpd_resp_send(req, NULL, 0);
    }

    return httpd_resp_sendstr(req, "Redirect to provisioning page");
}

static const char *wifi_state_text(wifi_state_t state)
{
    switch (state) {
    case WIFI_STATE_DISCONNECTED:
        return "disconnected";
    case WIFI_STATE_CONNECTING:
        return "connecting";
    case WIFI_STATE_CONNECTED:
        return "connected";
    case WIFI_STATE_CONFIGURING:
        return "configuring";
    case WIFI_STATE_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

static esp_err_t api_wifi_status_get_handler(httpd_req_t *req)
{
    char body[512];
    char ip[20] = "0.0.0.0";
    char ap_ssid[33] = "";
    char config_url[32] = "";
    char saved_ssid[WIFI_SSID_MAX_LEN] = "";

    wifi_manager_get_ip(ip, sizeof(ip));
    wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
    wifi_manager_get_config_url(config_url, sizeof(config_url));
    wifi_manager_get_saved_ssid(saved_ssid, sizeof(saved_ssid));

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    snprintf(
        body, sizeof(body),
        "{\"state\":\"%s\",\"connected\":%s,\"config_mode\":%s,"
        "\"has_credentials\":%s,\"ip\":\"%s\",\"ap_ssid\":\"%s\","
        "\"config_url\":\"%s\",\"saved_ssid\":\"%s\"}",
        wifi_state_text(wifi_manager_get_state()),
        wifi_manager_is_connected() ? "true" : "false",
        wifi_manager_is_config_mode() ? "true" : "false",
        wifi_manager_has_credentials() ? "true" : "false",
        ip,
        ap_ssid,
        config_url,
        saved_ssid);
    return httpd_resp_sendstr(req, body);
}

static esp_err_t api_dashboard_get_handler(httpd_req_t *req)
{
    weather_data_t weather = {0};
    char ip[20] = "0.0.0.0";
    char saved_ssid[WIFI_SSID_MAX_LEN] = "";
    char ap_ssid[33] = "";
    char config_url[32] = "";
    char time_buf[32] = "--";
    int64_t weather_age_s = -1;
    int64_t uptime_s = esp_timer_get_time() / 1000000;
    time_t now = ntp_client_get_time();
    struct tm tm_local = {0};
    cJSON *root = NULL;
    cJSON *wifi = NULL;
    cJSON *weather_obj = NULL;
    cJSON *forecast = NULL;
    char *body = NULL;

    weather_client_get_data(&weather);
    wifi_manager_get_ip(ip, sizeof(ip));
    wifi_manager_get_saved_ssid(saved_ssid, sizeof(saved_ssid));
    wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
    wifi_manager_get_config_url(config_url, sizeof(config_url));

    if (now > 0) {
        localtime_r(&now, &tm_local);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_local);
    }
    if (weather.update_time > 0) {
        weather_age_s = uptime_s - weather.update_time;
        if (weather_age_s < 0) {
            weather_age_s = 0;
        }
    }

    root = cJSON_CreateObject();
    wifi = cJSON_CreateObject();
    weather_obj = cJSON_CreateObject();
    forecast = cJSON_CreateArray();
    if (!root || !wifi || !weather_obj || !forecast) {
        cJSON_Delete(root);
        cJSON_Delete(wifi);
        cJSON_Delete(weather_obj);
        cJSON_Delete(forecast);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "device", DEVICE_NAME);
    cJSON_AddStringToObject(root, "fw_version", FW_VERSION_STR);
    cJSON_AddStringToObject(root, "time", time_buf);
    cJSON_AddBoolToObject(root, "ntp_synced", ntp_client_is_synced());

    cJSON_AddStringToObject(wifi, "state", wifi_state_text(wifi_manager_get_state()));
    cJSON_AddBoolToObject(wifi, "connected", wifi_manager_is_connected());
    cJSON_AddStringToObject(wifi, "ip", ip);
    cJSON_AddStringToObject(wifi, "saved_ssid", saved_ssid);
    cJSON_AddStringToObject(wifi, "ap_ssid", ap_ssid);
    cJSON_AddStringToObject(wifi, "config_url", config_url);
    cJSON_AddNumberToObject(wifi, "rssi", wifi_manager_is_connected() ? wifi_manager_get_rssi() : 0);
    cJSON_AddItemToObject(root, "wifi", wifi);

    cJSON_AddBoolToObject(weather_obj, "available", weather.valid);
    cJSON_AddNumberToObject(weather_obj, "age_s", weather_age_s);
    cJSON_AddNumberToObject(weather_obj, "temp_now", weather.temp_now);
    cJSON_AddNumberToObject(weather_obj, "temp_feels", weather.temp_feels);
    cJSON_AddNumberToObject(weather_obj, "temp_high", weather.temp_high);
    cJSON_AddNumberToObject(weather_obj, "temp_low", weather.temp_low);
    cJSON_AddNumberToObject(weather_obj, "humidity", weather.humidity);
    cJSON_AddNumberToObject(weather_obj, "wind_speed", weather.wind_speed);
    cJSON_AddNumberToObject(weather_obj, "visibility", weather.visibility);

    for (size_t i = 0; i < 3; i++) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddStringToObject(item, "date", weather.forecast[i].date);
        cJSON_AddNumberToObject(item, "high", weather.forecast[i].high);
        cJSON_AddNumberToObject(item, "low", weather.forecast[i].low);
        cJSON_AddItemToArray(forecast, item);
    }

    cJSON_AddItemToObject(weather_obj, "forecast", forecast);
    cJSON_AddItemToObject(root, "weather", weather_obj);

    body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json encode failed");
        return ESP_ERR_NO_MEM;
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t ret = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return ret;
}

static esp_err_t read_request_body(httpd_req_t *req, char *buf, size_t buf_len)
{
    int total = req->content_len;
    int received = 0;

    if (!buf || buf_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (total <= 0 || (size_t)total >= buf_len) {
        return ESP_ERR_INVALID_SIZE;
    }

    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }

    buf[received] = '\0';
    return ESP_OK;
}

static esp_err_t api_wifi_config_post_handler(httpd_req_t *req)
{
    char body[256] = "";
    esp_err_t ret = read_request_body(req, body, sizeof(body));
    cJSON *root = NULL;
    cJSON *ssid = NULL;
    cJSON *password = NULL;

    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body");
        return ret;
    }

    root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }

    ssid = cJSON_GetObjectItem(root, "ssid");
    password = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(ssid) || !ssid->valuestring[0]) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid required");
        return ESP_ERR_INVALID_ARG;
    }

    ret = schedule_wifi_apply(
        WIFI_APPLY_MODE_CONNECT,
        ssid->valuestring,
        cJSON_IsString(password) ? password->valuestring : "");
    cJSON_Delete(root);

    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi apply failed");
        return ret;
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t api_wifi_reset_post_handler(httpd_req_t *req)
{
    esp_err_t ret = schedule_wifi_apply(WIFI_APPLY_MODE_RESET, "", "");

    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi reset failed");
        return ret;
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t start_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t api_wifi_status = {
        .uri = "/api/wifi/status",
        .method = HTTP_GET,
        .handler = api_wifi_status_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t api_dashboard = {
        .uri = "/api/dashboard",
        .method = HTTP_GET,
        .handler = api_dashboard_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t api_wifi_config = {
        .uri = "/api/wifi/config",
        .method = HTTP_POST,
        .handler = api_wifi_config_post_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t api_wifi_reset = {
        .uri = "/api/wifi/reset",
        .method = HTTP_POST,
        .handler = api_wifi_reset_post_handler,
        .user_ctx = NULL,
    };

    if (s_server) {
        return ESP_OK;
    }

    config.server_port = WEB_SERVER_PORT;
    config.max_uri_handlers = 7;
    config.lru_purge_enable = true;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        s_server = NULL;
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &api_wifi_status));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &api_dashboard));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &api_wifi_config));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &api_wifi_reset));
    ESP_ERROR_CHECK(httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND,
                                               redirect_to_root_handler));
    return ESP_OK;
}

esp_err_t web_dashboard_init(void)
{
    esp_err_t ret = start_server();

    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG_WEB, "Provisioning portal started on port %u", (unsigned)WEB_SERVER_PORT);
    return ESP_OK;
}
