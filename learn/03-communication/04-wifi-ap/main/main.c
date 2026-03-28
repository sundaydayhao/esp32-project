/**
 * @file main.c
 * @brief WiFi Access Point (热点) 模式示例
 *
 * 功能：
 *   - 创建 WiFi 热点
 *   - 处理设备连接/断开事件
 *   - 显示连接设备信息
 *
 * 运行后，手机或电脑可以搜索到名为 "ESP32-AP" 的 WiFi 热点
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"

static const char *TAG = "WiFi_AP";

/* AP 配置 */
#define AP_SSID         "ESP32-AP"
#define AP_PASS         "12345678"   // 最少 8 位，留空则为开放网络
#define AP_CHANNEL      1
#define AP_MAX_CONN     4            // 最大连接设备数

/**
 * @brief WiFi AP 事件处理函数
 */
static void wifi_ap_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "设备连接: MAC=" MACSTR ", AID=%d",
                 MAC2STR(event->mac), event->aid);

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "设备断开: MAC=" MACSTR ", AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

/**
 * @brief 初始化 WiFi AP 模式
 */
static void wifi_init_ap(void)
{
    /* 初始化网络接口 */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    /* 初始化 WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 注册事件处理函数 */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_ap_event_handler,
                                                         NULL, NULL));

    /* 配置 AP */
    wifi_config_t wifi_config = {
        .ap = {
            .ssid           = AP_SSID,
            .ssid_len       = strlen(AP_SSID),
            .channel        = AP_CHANNEL,
            .password       = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode       = strlen(AP_PASS) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP 已启动");
    ESP_LOGI(TAG, "SSID: %s", AP_SSID);
    ESP_LOGI(TAG, "密码: %s", strlen(AP_PASS) > 0 ? AP_PASS : "(开放网络)");
    ESP_LOGI(TAG, "信道: %d", AP_CHANNEL);
    ESP_LOGI(TAG, "最大连接数: %d", AP_MAX_CONN);
    ESP_LOGI(TAG, "AP IP 地址: 192.168.4.1");
}

void app_main(void)
{
    ESP_LOGI(TAG, "WiFi AP 示例启动");

    /* 初始化 NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 启动 WiFi AP */
    wifi_init_ap();

    /* 主循环：每 10 秒打印连接设备数 */
    while (1) {
        wifi_sta_list_t sta_list;
        esp_wifi_ap_get_sta_list(&sta_list);
        ESP_LOGI(TAG, "当前连接设备数: %d / %d", sta_list.num, AP_MAX_CONN);

        for (int i = 0; i < sta_list.num; i++) {
            ESP_LOGI(TAG, "  设备 %d: MAC=" MACSTR ", RSSI=%d",
                     i + 1, MAC2STR(sta_list.sta[i].mac), sta_list.sta[i].rssi);
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}