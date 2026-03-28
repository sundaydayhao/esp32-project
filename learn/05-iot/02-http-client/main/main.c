/**
 * @file main.c
 * @brief HTTP 客户端示例
 *
 * 功能：发送 HTTP GET/POST 请求，解析响应
 * 测试服务：httpbin.org（免费 HTTP 测试服务）
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"

static const char *TAG = "HTTP_Client";

#define WIFI_SSID           "your_wifi_ssid"
#define WIFI_PASS           "your_wifi_password"
#define HTTP_GET_URL        "http://httpbin.org/get"
#define HTTP_POST_URL       "http://httpbin.org/post"
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static char response_buf[4096];
static int response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
        response_len = 0;
        break;
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client)) {
            int copy_len = evt->data_len;
            if (response_len + copy_len < (int)sizeof(response_buf) - 1) {
                memcpy(response_buf + response_len, evt->data, copy_len);
                response_len += copy_len;
                response_buf[response_len] = '\0';
            }
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void http_get_request(const char *url)
{
    ESP_LOGI(TAG, "GET: %s", url);
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "状态: %d, 响应: %.300s",
                 esp_http_client_get_status_code(client), response_buf);
    } else {
        ESP_LOGE(TAG, "GET 失败: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void http_post_request(const char *url, const char *post_data)
{
    ESP_LOGI(TAG, "POST: %s", url);
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "状态: %d", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "POST 失败: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void http_task(void *pvParameters)
{
    /* 等待 WiFi 连接 */
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(TAG, "WiFi 已连接，开始 HTTP 请求...");

    while (1) {
        /* GET 请求 */
        http_get_request(HTTP_GET_URL);
        vTaskDelay(pdMS_TO_TICKS(2000));

        /* POST 请求 */
        char post_data[128];
        snprintf(post_data, sizeof(post_data),
                 "{\"device\":\"ESP32\",\"temp\":%.1f,\"uptime\":%lld}",
                 25.5f, esp_timer_get_time() / 1000000);
        http_post_request(HTTP_POST_URL, post_data);

        vTaskDelay(pdMS_TO_TICKS(30000));  // 每 30 秒请求一次
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) { esp_wifi_connect(); s_retry_num++; }
        else xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "HTTP 客户端示例启动");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {.ssid = WIFI_SSID, .password = WIFI_PASS},
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    xTaskCreate(http_task, "http_task", 8192, NULL, 5, NULL);
}