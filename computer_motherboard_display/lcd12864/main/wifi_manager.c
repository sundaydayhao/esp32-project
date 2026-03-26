/**
 * @file wifi_manager.c
 * @brief WiFi 管理器实现（STA 模式，断线自动重连）
 */
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi_mgr";

/* Kconfig 中配置的 SSID 和密码 */
#ifndef CONFIG_WIFI_SSID
#define CONFIG_WIFI_SSID        "your_ssid"
#endif
#ifndef CONFIG_WIFI_PASSWORD
#define CONFIG_WIFI_PASSWORD    "your_password"
#endif
#define WIFI_MAX_RETRY          10

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_evt_grp  = NULL;
static wifi_state_t       s_state    = WIFI_STATE_DISCONNECTED;
static int                s_retry    = 0;

/* ----------------------------------------------------------------
 * 事件处理
 * ---------------------------------------------------------------- */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            s_state = WIFI_STATE_CONNECTING;
            esp_wifi_connect();
            ESP_LOGI(TAG, "WiFi STA started, connecting...");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disc =
                (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "Disconnected (reason=%d), retry %d/%d",
                     disc->reason, s_retry + 1, WIFI_MAX_RETRY);
            s_state = WIFI_STATE_DISCONNECTED;
            if (s_retry < WIFI_MAX_RETRY) {
                s_retry++;
                esp_wifi_connect();
            } else {
                xEventGroupSetBits(s_evt_grp, WIFI_FAIL_BIT);
                s_state = WIFI_STATE_FAILED;
                ESP_LOGE(TAG, "WiFi connection failed after %d retries",
                         WIFI_MAX_RETRY);
            }
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry = 0;
        s_state = WIFI_STATE_CONNECTED;
        xEventGroupSetBits(s_evt_grp, WIFI_CONNECTED_BIT);
    }
}

/* ----------------------------------------------------------------
 * 公共 API
 * ---------------------------------------------------------------- */
esp_err_t wifi_manager_init(void)
{
    if (s_evt_grp) {
        return ESP_OK; /* 已初始化 */
    }

    s_evt_grp = xEventGroupCreate();
    if (!s_evt_grp) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 注册事件 */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    /* 配置 STA */
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid,     CONFIG_WIFI_SSID,
            sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, CONFIG_WIFI_PASSWORD,
            sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init done, SSID: %s", CONFIG_WIFI_SSID);
    return ESP_OK;
}

wifi_state_t wifi_manager_get_state(void)
{
    return s_state;
}

bool wifi_manager_is_connected(void)
{
    return s_state == WIFI_STATE_CONNECTED;
}

esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms)
{
    if (!s_evt_grp) return ESP_ERR_INVALID_STATE;

    EventBits_t bits = xEventGroupWaitBits(
        s_evt_grp,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT;
}