/**
 * @file main.c
 * @brief 综合项目：WiFi 智能 LED 控制器
 *
 * 功能：
 *   - WiFi 连接
 *   - HTTP Web 界面控制 LED 亮度和颜色
 *   - PWM 调光（LEDC 驱动）
 *   - 多种灯效模式（常亮/呼吸/闪烁）
 *   - NVS 保存设置
 *
 * 硬件连接：
 *   LED R → GPIO25（PWM）
 *   LED G → GPIO26（PWM）
 *   LED B → GPIO27（PWM）
 *   或单色 LED → GPIO2
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

static const char *TAG = "SmartLED";

#define WIFI_SSID           "your_wifi_ssid"
#define WIFI_PASS           "your_wifi_password"
#define WIFI_CONNECTED_BIT  BIT0

/* LED PWM 配置 */
#define LED_R_GPIO      GPIO_NUM_25
#define LED_G_GPIO      GPIO_NUM_26
#define LED_B_GPIO      GPIO_NUM_27
#define LEDC_FREQ_HZ    5000
#define LEDC_RESOLUTION LEDC_TIMER_8_BIT  // 0~255

/* 灯效模式 */
typedef enum {
    LED_MODE_STATIC = 0,
    LED_MODE_BREATH,
    LED_MODE_BLINK,
    LED_MODE_RAINBOW,
} led_mode_t;

static struct {
    uint8_t r, g, b;
    uint8_t brightness;
    led_mode_t mode;
    bool on;
} s_led = {255, 255, 255, 100, LED_MODE_STATIC, true};

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

/* PWM 初始化 */
static void ledc_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz         = LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channels[] = {
        {.gpio_num = LED_R_GPIO, .channel = LEDC_CHANNEL_0,
         .speed_mode = LEDC_LOW_SPEED_MODE, .timer_sel = LEDC_TIMER_0, .duty = 0},
        {.gpio_num = LED_G_GPIO, .channel = LEDC_CHANNEL_1,
         .speed_mode = LEDC_LOW_SPEED_MODE, .timer_sel = LEDC_TIMER_0, .duty = 0},
        {.gpio_num = LED_B_GPIO, .channel = LEDC_CHANNEL_2,
         .speed_mode = LEDC_LOW_SPEED_MODE, .timer_sel = LEDC_TIMER_0, .duty = 0},
    };
    for (int i = 0; i < 3; i++) ledc_channel_config(&channels[i]);
}

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    float scale = s_led.on ? (s_led.brightness / 255.0f) : 0;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)(r * scale));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, (uint32_t)(g * scale));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, (uint32_t)(b * scale));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
}

/* 灯效任务 */
static void led_effect_task(void *pvParameters)
{
    float breath_val = 0;
    float breath_dir = 1;
    int rainbow_hue = 0;

    while (1) {
        switch (s_led.mode) {
        case LED_MODE_STATIC:
            set_rgb(s_led.r, s_led.g, s_led.b);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_MODE_BREATH:
            breath_val += breath_dir * 2;
            if (breath_val >= 255) { breath_val = 255; breath_dir = -1; }
            if (breath_val <= 0)   { breath_val = 0;   breath_dir = 1; }
            {
                float scale = breath_val / 255.0f;
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)(s_led.r * scale));
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, (uint32_t)(s_led.g * scale));
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, (uint32_t)(s_led.b * scale));
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            break;

        case LED_MODE_BLINK:
            set_rgb(s_led.r, s_led.g, s_led.b);
            vTaskDelay(pdMS_TO_TICKS(500));
            set_rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case LED_MODE_RAINBOW:
            rainbow_hue = (rainbow_hue + 1) % 360;
            {
                /* HSV to RGB 转换 */
                float h = rainbow_hue, s = 1.0f, v = s_led.brightness / 255.0f;
                float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
                float m = v - c;
                float r1, g1, b1;
                if      (h < 60)  { r1=c; g1=x; b1=0; }
                else if (h < 120) { r1=x; g1=c; b1=0; }
                else if (h < 180) { r1=0; g1=c; b1=x; }
                else if (h < 240) { r1=0; g1=x; b1=c; }
                else if (h < 300) { r1=x; g1=0; b1=c; }
                else              { r1=c; g1=0; b1=x; }
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)((r1+m)*255));
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, (uint32_t)((g1+m)*255));
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, (uint32_t)((b1+m)*255));
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }
    }
}

/* HTTP 控制页面 */
static esp_err_t web_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>ESP32 智能LED</title>"
        "<style>body{font-family:Arial;background:#1a1a2e;color:#eee;text-align:center;}"
        "h1{color:#e94560;}.btn{padding:10px 20px;margin:5px;border:none;"
        "border-radius:5px;cursor:pointer;font-size:16px;}"
        "input[type=range]{width:200px;}</style></head><body>"
        "<h1>💡 ESP32 智能LED控制</h1>"
        "<div><button class='btn' style='background:#4CAF50' "
        "onclick=\"fetch('/led/on')\">开灯</button>"
        "<button class='btn' style='background:#f44336' "
        "onclick=\"fetch('/led/off')\">关灯</button></div>"
        "<div style='margin:20px'>"
        "<p>亮度: <input type='range' min='0' max='255' value='100' "
        "oninput=\"fetch('/led/brightness/'+this.value)\"></p>"
        "<p>颜色: <input type='color' value='#ffffff' "
        "oninput=\"let c=this.value;fetch('/led/color/'+c.slice(1))\"></p>"
        "</div>"
        "<div>"
        "<button class='btn' style='background:#555' onclick=\"fetch('/led/mode/0')\">常亮</button>"
        "<button class='btn' style='background:#2196F3' onclick=\"fetch('/led/mode/1')\">呼吸</button>"
        "<button class='btn' style='background:#FF9800' onclick=\"fetch('/led/mode/2')\">闪烁</button>"
        "<button class='btn' style='background:linear-gradient(90deg,red,yellow,green,blue)' "
        "onclick=\"fetch('/led/mode/3')\">彩虹</button>"
        "</div>"
        "<p style='color:#888'>ESP-IDF Smart LED Demo</p>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

static esp_err_t led_on_handler(httpd_req_t *req)
{
    s_led.on = true;
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t led_off_handler(httpd_req_t *req)
{
    s_led.on = false;
    set_rgb(0, 0, 0);
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t brightness_handler(httpd_req_t *req)
{
    char uri[64];
    strncpy(uri, req->uri, sizeof(uri) - 1);
    char *val = strrchr(uri, '/');
    if (val) s_led.brightness = (uint8_t)atoi(val + 1);
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t color_handler(httpd_req_t *req)
{
    char uri[64];
    strncpy(uri, req->uri, sizeof(uri) - 1);
    char *hex = strrchr(uri, '/');
    if (hex) {
        unsigned int color;
        sscanf(hex + 1, "%x", &color);
        s_led.r = (color >> 16) & 0xFF;
        s_led.g = (color >> 8) & 0xFF;
        s_led.b = color & 0xFF;
    }
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t mode_handler(httpd_req_t *req)
{
    char uri[64];
    strncpy(uri, req->uri, sizeof(uri) - 1);
    char *val = strrchr(uri, '/');
    if (val) s_led.mode = (led_mode_t)atoi(val + 1);
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
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
    ESP_LOGI(TAG, "ESP32 智能LED控制器启动");

    ledc_init();

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
    wifi_config_t wifi_cfg = { .sta = {.ssid = WIFI_SSID, .password = WIFI_PASS} };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    /* HTTP 服务器 */
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &http_cfg) == ESP_OK) {
        httpd_uri_t uris[] = {
            {.uri = "/",                .method = HTTP_GET, .handler = web_handler},
            {.uri = "/led/on",          .method = HTTP_GET, .handler = led_on_handler},
            {.uri = "/led/off",         .method = HTTP_GET, .handler = led_off_handler},
            {.uri = "/led/brightness/*",.method = HTTP_GET, .handler = brightness_handler},
            {.uri = "/led/color/*",     .method = HTTP_GET, .handler = color_handler},
            {.uri = "/led/mode/*",      .method = HTTP_GET, .handler = mode_handler},
        };
        for (int i = 0; i < 6; i++) httpd_register_uri_handler(server, &uris[i]);
        ESP_LOGI(TAG, "HTTP 服务器已启动");
    }

    /* 灯效任务 */
    xTaskCreate(led_effect_task, "led_effect", 4096, NULL, 5, NULL);
}