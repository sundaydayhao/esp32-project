/**
 * @file main.c
 * @brief HTTP Web 服务器示例
 *
 * 功能：
 *   - 连接 WiFi
 *   - 启动 HTTP 服务器
 *   - 提供网页控制 LED
 *   - REST API 接口
 *
 * 使用方法：
 *   1. 修改 WIFI_SSID 和 WIFI_PASS
 *   2. 烧录并运行
 *   3. 查看串口输出获取 IP 地址
 *   4. 浏览器访问 http://<IP地址>/
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
#include "esp_http_server.h"
#include "driver/gpio.h"

static const char *TAG = "HTTP_Server";

#define WIFI_SSID       "your_wifi_ssid"
#define WIFI_PASS       "your_wifi_password"
#define LED_GPIO        GPIO_NUM_2

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static int s_led_state = 0;

/* HTML 主页 */
static const char *html_page =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<title>ESP32 控制面板</title>"
    "<style>"
    "body{font-family:Arial;text-align:center;background:#1a1a2e;color:#eee;}"
    "h1{color:#e94560;}"
    ".btn{padding:15px 30px;margin:10px;font-size:18px;border:none;"
    "border-radius:8px;cursor:pointer;}"
    ".on{background:#4CAF50;color:white;}"
    ".off{background:#f44336;color:white;}"
    ".status{font-size:24px;margin:20px;}"
    "</style></head><body>"
    "<h1>ESP32 LED 控制</h1>"
    "<div class='status'>LED 状态: <span id='s'>%s</span></div>"
    "<button class='btn on' onclick=\"fetch('/led/on').then(()=>location.reload())\">开灯</button>"
    "<button class='btn off' onclick=\"fetch('/led/off').then(()=>location.reload())\">关灯</button>"
    "<p>ESP-IDF HTTP Server Demo</p>"
    "</body></html>";

/**
 * @brief 主页处理函数
 */
static esp_err_t root_handler(httpd_req_t *req)
{
    char response[1024];
    snprintf(response, sizeof(response), html_page,
             s_led_state ? "🟢 ON" : "🔴 OFF");
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

/**
 * @brief LED 开灯处理函数
 */
static esp_err_t led_on_handler(httpd_req_t *req)
{
    s_led_state = 1;
    gpio_set_level(LED_GPIO, 1);
    ESP_LOGI(TAG, "LED ON");
    httpd_resp_send(req, "{\"status\":\"on\"}", -1);
    return ESP_OK;
}

/**
 * @brief LED 关灯处理函数
 */
static esp_err_t led_off_handler(httpd_req_t *req)
{
    s_led_state = 0;
    gpio_set_level(LED_GPIO, 0);
    ESP_LOGI(TAG, "LED OFF");
    httpd_resp_send(req, "{\"status\":\"off\"}", -1);
    return ESP_OK;
}

/**
 * @brief 状态 API 处理函数
 */
static esp_err_t status_handler(httpd_req_t *req)
{
    char response[128];
    snprintf(response, sizeof(response),
             "{\"led\":%s,\"uptime\":%lld}",
             s_led_state ? "true" : "false",
             esp_timer_get_time() / 1000000);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

/**
 * @brief 启动 HTTP 服务器
 */
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务器启动失败");
        return NULL;
    }

    /* 注册 URI 处理函数 */
    httpd_uri_t root_uri = {
        .uri = "/", .method = HTTP_GET, .handler = root_handler
    };
    httpd_uri_t led_on_uri = {
        .uri = "/led/on", .method = HTTP_GET, .handler = led_on_handler
    };
    httpd_uri_t led_off_uri = {
        .uri = "/led/off", .method = HTTP_GET, .handler = led_off_handler
    };
    httpd_uri_t status_uri = {
        .uri = "/api/status", .method = HTTP_GET, .handler = status_handler
    };

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &led_on_uri);
    httpd_register_uri_handler(server, &led_off_uri);
    httpd_register_uri_handler(server, &status_uri);

    ESP_LOGI(TAG, "HTTP 服务器已启动");
    return server;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP 地址: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "请访问: http://" IPSTR "/", IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "HTTP 服务器示例启动");

    /* 配置 LED */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    /* 初始化 NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 连接 WiFi */
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {.ssid = WIFI_SSID, .password = WIFI_PASS},
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        /* 启动 HTTP 服务器 */
        start_webserver();
    } else {
        ESP_LOGE(TAG, "WiFi 连接失败");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}