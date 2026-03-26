/**
 * @file wifi_manager.cpp
 * @brief WiFi连接管理
 */

#include "wifi_manager.h"
#include "app_config.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include <string.h>

static const char *TAG = "WIFI";

// 事件组标志位
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static wifi_status_t s_wifi_status = WIFI_STATUS_DISCONNECTED;
static wifi_status_cb_t s_status_cb = NULL;
static int s_retry_count = 0;
static esp_netif_t *s_netif = NULL;

/**
 * @brief WiFi事件处理函数
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA 启动，开始连接...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "WiFi断开连接，原因: %d", disc->reason);

            if (s_retry_count < WIFI_MAXIMUM_RETRY) {
                s_retry_count++;
                ESP_LOGI(TAG, "重试连接 (%d/%d)...", s_retry_count, WIFI_MAXIMUM_RETRY);
                esp_wifi_connect();
                s_wifi_status = WIFI_STATUS_CONNECTING;
            } else {
                ESP_LOGE(TAG, "WiFi连接失败，已达最大重试次数");
                s_wifi_status = WIFI_STATUS_FAILED;
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }

            if (s_status_cb) s_status_cb(s_wifi_status);
            break;
        }

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi已连接，等待获取IP...");
            s_retry_count = 0;
            break;

        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "获取IP地址: " IPSTR, IP2STR(&event->ip_info.ip));

            s_wifi_status = WIFI_STATUS_CONNECTED;
            s_retry_count = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

            if (s_status_cb) s_status_cb(s_wifi_status);
        }
    }
}

// ============================================================
// 公共接口实现
// ============================================================

esp_err_t wifi_manager_init(wifi_status_cb_t status_cb)
{
    ESP_LOGI(TAG, "初始化WiFi管理器");

    s_status_cb = status_cb;

    // 初始化NVS (WiFi需要)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS分区已满，擦除重新初始化");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化TCP/IP栈
    ESP_ERROR_CHECK(esp_netif_init());

    // 创建默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 创建默认WiFi STA接口
    s_netif = esp_netif_create_default_wifi_sta();

    // 初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理函数
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler, NULL, NULL));

    // 创建事件组
    s_wifi_event_group = xEventGroupCreate();

    // 设置WiFi模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_LOGI(TAG, "WiFi管理器初始化完成");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "连接WiFi: %s", ssid);

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    s_wifi_status = WIFI_STATUS_CONNECTING;
    s_retry_count = 0;

    // 清除事件标志
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

void wifi_manager_disconnect(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_wifi_status = WIFI_STATUS_DISCONNECTED;
    ESP_LOGI(TAG, "WiFi已断开");
}

wifi_status_t wifi_manager_get_status(void)
{
    return s_wifi_status;
}

esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms)
{
    if (!s_wifi_event_group) return ESP_ERR_INVALID_STATE;

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi连接成功");
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi连接失败");
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "WiFi连接超时");
        return ESP_ERR_TIMEOUT;
    }
}

void wifi_manager_get_ip(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return;

    esp_netif_ip_info_t ip_info;
    if (s_netif && esp_netif_get_ip_info(s_netif, &ip_info) == ESP_OK) {
        snprintf(buf, buf_len, IPSTR, IP2STR(&ip_info.ip));
    } else {
        strncpy(buf, "0.0.0.0", buf_len);
    }
}

void wifi_manager_deinit(void)
{
    wifi_manager_disconnect();
    esp_wifi_deinit();
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    ESP_LOGI(TAG, "WiFi管理器已释放");
}