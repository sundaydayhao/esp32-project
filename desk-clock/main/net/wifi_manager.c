/**
 * @file wifi_manager.c
 * @brief WiFi 管理器实现
 */
#include "wifi_manager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_evt_grp  = NULL;
static wifi_state_t       s_state    = WIFI_STATE_DISCONNECTED;
static int                s_retry    = 0;
static char               s_ip[20]  = "0.0.0.0";

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        s_state = WIFI_STATE_CONNECTING;
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_state = WIFI_STATE_DISCONNECTED;
        if (s_retry < WIFI_MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG_WIFI, "断开，第 %d 次重连...", s_retry);
            vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_evt_grp, WIFI_FAIL_BIT);
            s_state = WIFI_STATE_FAILED;
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        ESP_LOGI(TAG_WIFI, "已连接，IP: %s", s_ip);
        s_retry = 0;
        s_state = WIFI_STATE_CONNECTED;
        xEventGroupSetBits(s_evt_grp, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(void)
{
    /* 从 NVS 读取 WiFi 配置 */
    char ssid[WIFI_SSID_MAX_LEN] = CONFIG_WIFI_SSID_DEFAULT;
    char pass[WIFI_PASS_MAX_LEN] = CONFIG_WIFI_PASS_DEFAULT;

    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len;
        len = sizeof(ssid); nvs_get_str(h, "ssid", ssid, &len);
        len = sizeof(pass); nvs_get_str(h, "pass", pass, &len);
        nvs_close(h);
    }

    s_evt_grp = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any, inst_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &inst_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &inst_ip));

    wifi_config_t wifi_cfg = { 0 };
    strlcpy((char *)wifi_cfg.sta.ssid,     ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WIFI, "WiFi 初始化完成，连接 SSID: %s", ssid);
    return ESP_OK;
}

wifi_state_t wifi_manager_get_state(void) { return s_state; }

bool wifi_manager_is_connected(void) { return s_state == WIFI_STATE_CONNECTED; }

esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(s_evt_grp,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (bits & WIFI_CONNECTED_BIT) return ESP_OK;
    return ESP_FAIL;
}

void wifi_manager_get_ip(char *buf, size_t len)
{
    if (buf) strlcpy(buf, s_ip, len);
}

int8_t wifi_manager_get_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
    return 0;
}