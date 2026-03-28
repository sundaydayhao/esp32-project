/**
 * @file main.c
 * @brief 综合项目：WiFi 气象站
 *
 * 功能：读取 DHT11 温湿度 + HTTP Web 界面显示数据
 * 硬件：DHT11(GPIO4), LED(GPIO2), WiFi
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"

static const char *TAG = "WeatherStation";

#define WIFI_SSID           "your_wifi_ssid"
#define WIFI_PASS           "your_wifi_password"
#define DHT11_GPIO          GPIO_NUM_4
#define LED_GPIO            GPIO_NUM_2
#define WIFI_CONNECTED_BIT  BIT0

typedef struct {
    float temperature;
    float humidity;
    int64_t timestamp;
    bool valid;
} weather_data_t;

static weather_data_t s_weather = {0};
static SemaphoreHandle_t s_data_mutex = NULL;
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

/* DHT11 读取 */
static int dht11_wait_level(int level, int timeout_us)
{
    int elapsed = 0;
    while (gpio_get_level(DHT11_GPIO) != level) {
        if (elapsed >= timeout_us) return -1;
        ets_delay_us(1);
        elapsed++;
    }
    return elapsed;
}

static esp_err_t dht11_read(float *temp, float *humi)
{
    uint8_t bits[40] = {0}, bytes[5] = {0};
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(DHT11_GPIO, 1);
    ets_delay_us(30);
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_INPUT);
    if (dht11_wait_level(0, 100) < 0) return ESP_FAIL;
    if (dht11_wait_level(1, 100) < 0) return ESP_FAIL;
    if (dht11_wait_level(0, 100) < 0) return ESP_FAIL;
    for (int i = 0; i < 40; i++) {
        if (dht11_wait_level(1, 60) < 0) return ESP_FAIL;
        int t = dht11_wait_level(0, 80);
        if (t < 0) return ESP_FAIL;
        bits[i] = (t > 40) ? 1 : 0;
    }
    for (int i = 0; i < 5; i++) {
        bytes[i] = 0;
        for (int j = 0; j < 8; j++)
            bytes[i] = (bytes[i] << 1) | bits[i * 8 + j];
    }
    if ((uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]) != bytes[4])
        return ESP_FAIL;
    *humi = bytes[0] + bytes[1] * 0.1f;
    *temp = bytes[2] + bytes[3] * 0.1f;
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_GPIO, 1);
    return ESP_OK;
}

/* HTTP 主页 */
static esp_err_t web_handler(httpd_req_t *req)
{
    weather_data_t data = {0};
    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        data = s_weather;
        xSemaphoreGive(s_data_mutex);
    }
    char resp[2048];
    snprintf(resp, sizeof(resp),
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='5'>"
        "<title>ESP32 气象站</title>"
        "<style>body{font-family:Arial;background:#1a1a2e;color:#eee;text-align:center;}"
        "h1{color:#e94560;}.card{background:#16213e;border-radius:10px;"
        "padding:20px;margin:10px auto;max-width:300px;display:inline-block;}"
        ".val{font-size:48px;font-weight:bold;color:#4fc3f7;}"
        ".unit{font-size:20px;color:#e94560;}</style></head><body>"
        "<h1>🌤 ESP32 气象站</h1>"
        "<div class='card'><p>🌡 温度</p>"
        "<span class='val'>%.1f</span><span class='unit'> °C</span></div>"
        "<div class='card'><p>💧 湿度</p>"
        "<span class='val'>%.1f</span><span class='unit'> %%</span></div>"
        "<p style='color:#888'>数据每5秒自动刷新 | 运行时间: %lld 秒</p>"
        "</body></html>",
        data.temperature, data.humidity,
        esp_timer_get_time() / 1000000);
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

/* JSON API */
static esp_err_t api_handler(httpd_req_t *req)
{
    weather_data_t data = {0};
    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        data = s_weather;
        xSemaphoreGive(s_data_mutex);
    }
    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"temperature\":%.1f,\"humidity\":%.1f,\"valid\":%s,\"uptime\":%lld}",
             data.temperature, data.humidity,
             data.valid ? "true" : "false",
             esp_timer_get_time() / 1000000);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

/* 传感器采集任务 */
static void sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "传感器任务启动");
    while (1) {
        float temp = 0, humi = 0;
        esp_err_t ret = dht11_read(&temp, &humi);
        if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (ret == ESP_OK) {
                s_weather.temperature = temp;
                s_weather.humidity = humi;
                s_weather.valid = true;
                s_weather.timestamp = esp_timer_get_time();
                ESP_LOGI(TAG, "温度: %.1f°C, 湿度: %.1f%%", temp, humi);
            } else {
                s_weather.valid = false;
                ESP_LOGW(TAG, "DHT11 读取失败");
            }
            xSemaphoreGive(s_data_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) { esp_wifi_connect(); s_retry_num++; }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "访问: http://" IPSTR "/", IP2STR(&e->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 气象站启动");

    /* 初始化 */
    s_data_mutex = xSemaphoreCreateMutex();
    gpio_config_t io = { .pin_bit_mask = (1ULL << LED_GPIO), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_GPIO, 1);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* WiFi */
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
    wifi_config_t wifi_cfg = { .sta = {.ssid = WIFI_SSID, .password = WIFI_PASS} };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    /* HTTP 服务器 */
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &http_cfg) == ESP_OK) {
        httpd_uri_t root = { .uri = "/",    .method = HTTP_GET, .handler = web_handler };
        httpd_uri_t api  = { .uri = "/api", .method = HTTP_GET, .handler = api_handler };
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &api);
        ESP_LOGI(TAG, "HTTP 服务器已启动");
    }

    /* 传感器任务 */
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 5, NULL);

    /* LED 心跳 */
    while (1) {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(4900));
    }
}