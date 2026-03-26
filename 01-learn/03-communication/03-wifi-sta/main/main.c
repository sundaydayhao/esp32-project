/**
 * @file main.c
 * @brief WiFi Station 模式示例
 *
 * 功能：
 *   - 连接到指定 WiFi 热点
 *   - 处理连接/断开事件
 *   - 自动重连机制
 *   - 获取 IP 地址
 *
 * 使用前修改：
 *   WIFI_SSID - 你的 WiFi 名称
 *   WIFI_PASS - 你的 WiFi 密码
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

static const char *TAG = "WiFi_STA";

/* WiFi 配置 - 修改为你的 WiFi 信息 */
#define WIFI_SSID       "your_wifi_ssid"
#define WIFI_PASS       "your_wifi_password"
#define WIFI_MAX_RETRY  5

/* 事件组标志位 */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

/**
 * @brief WiFi 事件处理函数
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        /* WiFi 启动，开始连接 */
        esp_wifi_connect();
        ESP_LOGI(TAG, "正在连接到 WiFi: %s", WIFI_SSID);

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        /* 连接断开 */
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "WiFi 断开，原因: %d", event->reason);

        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "重试连接 (%d/%d)...", s_retry_num, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "连接失败，已达最大重试次数");
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        /* 获取到 IP 地址 */
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "获取到 IP 地址: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "子网掩码: " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "网关: " IPSTR, IP2STR(&event->ip_info.gw));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief 初始化 WiFi Station 模式
 */
static esp_err_t wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    /* 初始化网络接口 */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    /* 初始化 WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 注册事件处理函数 */
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         &instance_got_ip));

    /* 配置 WiFi */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi 初始化完成，等待连接...");

    /* 等待连接结果 */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE,
                                            pdFALSE,
                                            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "✅ WiFi 连接成功！SSID: %s", WIFI_SSID);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "❌ WiFi 连接失败！SSID: %s", WIFI_SSID);
        return ESP_FAIL;
    }

    return ESP_FAIL;
}

void app_main(void)
{
    ESP_LOGI(TAG, "WiFi Station 示例启动");

    /* 初始化 NVS（WiFi 需要） */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 连接 WiFi */
    if (wifi_init_sta() == ESP_OK) {
        ESP_LOGI(TAG, "WiFi 已连接，可以进行网络通信");

        /* 主循环：每 30 秒打印一次 WiFi 信息 */
        while (1) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                ESP_LOGI(TAG, "SSID: %s, RSSI: %d dBm, 信道: %d",
                         ap_info.ssid, ap_info.rssi, ap_info.primary);
            }
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
    } else {
        ESP_LOGE(TAG, "WiFi 连接失败，请检查 SSID 和密码");
    }
}