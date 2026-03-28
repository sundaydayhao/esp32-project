/**
 * wifi_manager.cpp
 * WiFi Manager Implementation
 */

#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "WIFI";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAX_RETRY      5

static EventGroupHandle_t s_wifi_event_group = NULL;
static bool s_initialized = false;
static bool s_connected   = false;
static int  s_retry_count = 0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "WiFi reconnecting... (%d/%d)", s_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "WiFi connection failed");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        s_connected   = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_connect(const char *ssid, const char *password, uint32_t timeout_ms)
{
    if (s_connected) return ESP_OK;

    if (!s_initialized) {
        s_wifi_event_group = xEventGroupCreate();

        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

        s_initialized = true;
    }

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting WiFi: %s", ssid);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected: %s", ssid);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "WiFi connect timeout or failure");
    return ESP_FAIL;
}

void wifi_disconnect(void)
{
    if (s_connected) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        s_connected = false;
        ESP_LOGI(TAG, "WiFi disconnected");
    }
}

bool wifi_is_connected(void)
{
    return s_connected;
}

static char*  s_http_buf     = NULL;
static size_t s_http_buf_len = 0;
static size_t s_http_written = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (s_http_buf && s_http_written + evt->data_len < s_http_buf_len) {
                memcpy(s_http_buf + s_http_written, evt->data, evt->data_len);
                s_http_written += evt->data_len;
                s_http_buf[s_http_written] = '\0';
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

static esp_err_t http_request(esp_http_client_method_t method,
                              const char *url,
                              const void *body,
                              size_t body_len,
                              const char *content_type,
                              const char *auth_token,
                              char *response,
                              size_t resp_len)
{
    if (!s_connected) {
        ESP_LOGW(TAG, "WiFi not connected, HTTP request skipped");
        return ESP_ERR_INVALID_STATE;
    }

    s_http_buf     = response;
    s_http_buf_len = resp_len;
    s_http_written = 0;
    if (response) response[0] = '\0';

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = method;
    config.event_handler = http_event_handler;
    config.timeout_ms = 8000;
    config.buffer_size = 1024;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Accept", "application/json");

    if (content_type) {
        esp_http_client_set_header(client, "Content-Type", content_type);
    }

    if (auth_token && auth_token[0] != '\0') {
        char auth_header[192];
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", auth_token);
        esp_http_client_set_header(client, "Authorization", auth_header);
    }

    if (body && body_len > 0) {
        esp_http_client_set_post_field(client, (const char *)body, (int)body_len);
    }

    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        const int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP %s complete: status=%d length=%d",
                 method == HTTP_METHOD_GET ? "GET" : "POST",
                 status,
                 (int)s_http_written);
        if (status < 200 || status >= 300) {
            ret = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %d", ret);
    }

    esp_http_client_cleanup(client);
    s_http_buf = NULL;
    return ret;
}

esp_err_t http_get(const char *url, char *response, size_t resp_len)
{
    return http_request(HTTP_METHOD_GET, url, NULL, 0, NULL, NULL, response, resp_len);
}

esp_err_t http_post(const char *url,
                    const void *body,
                    size_t body_len,
                    const char *content_type,
                    const char *auth_token,
                    char *response,
                    size_t resp_len)
{
    return http_request(HTTP_METHOD_POST,
                        url,
                        body,
                        body_len,
                        content_type,
                        auth_token,
                        response,
                        resp_len);
}

esp_err_t http_post_json(const char *url,
                         const char *json_body,
                         const char *auth_token,
                         char *response,
                         size_t resp_len)
{
    return http_post(url,
                     json_body,
                     strlen(json_body),
                     "application/json",
                     auth_token,
                     response,
                     resp_len);
}
