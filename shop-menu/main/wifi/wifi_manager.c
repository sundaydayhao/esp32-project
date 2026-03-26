#include "wifi_manager.h"
#include "content_store.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_manager";

// 事件组标志位
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group;
static wifi_status_t s_status = {0};
static int s_retry_count = 0;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

// ============================================================
// 事件处理
// ============================================================
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                s_status.state = WIFI_STATE_CONNECTING;
                ESP_LOGI(TAG, "STA started, connecting...");
                break;

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "Disconnected (reason: %d), retry %d/%d",
                         disc->reason, s_retry_count + 1, WIFI_STA_MAX_RETRY);
                s_status.state = WIFI_STATE_DISCONNECTED;
                memset(s_status.ip_addr, 0, sizeof(s_status.ip_addr));

                if (s_retry_count < WIFI_STA_MAX_RETRY) {
                    s_retry_count++;
                    esp_wifi_connect();
                } else {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                }
                break;
            }

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "Client connected to AP: " MACSTR, MAC2STR(event->mac));
                break;
            }

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_status.ip_addr, sizeof(s_status.ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
        s_status.state = WIFI_STATE_CONNECTED;
        s_retry_count = 0;
        ESP_LOGI(TAG, "Got IP: %s", s_status.ip_addr);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ============================================================
// 启动 AP 模式（配置热点）
// ============================================================
static void start_ap_mode(void) {
    ESP_LOGI(TAG, "Starting AP mode...");

    // 生成热点名称（含 MAC 后4位）
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X", WIFI_AP_SSID_PREFIX, mac[4], mac[5]);

    wifi_config_t ap_config = {
        .ap = {
            .channel = WIFI_AP_CHANNEL,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        }
    };
    strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid));
    strncpy((char *)ap_config.ap.password, WIFI_AP_PASSWORD, sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = strlen(ap_ssid);

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();

    s_status.state = WIFI_STATE_AP_MODE;
    strncpy(s_status.ssid, ap_ssid, sizeof(s_status.ssid));
    snprintf(s_status.ip_addr, sizeof(s_status.ip_addr), WIFI_AP_IP);

    ESP_LOGI(TAG, "AP started: SSID=%s, IP=%s", ap_ssid, WIFI_AP_IP);
}

// ============================================================
// 初始化
// ============================================================
void wifi_manager_init(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // 加载保存的 WiFi 配置
    wifi_config_store_t wifi_cfg;
    content_store_load_wifi(&wifi_cfg);

    if (wifi_cfg.configured && strlen(wifi_cfg.ssid) > 0) {
        // 尝试连接已保存的 WiFi
        ESP_LOGI(TAG, "Connecting to saved WiFi: %s", wifi_cfg.ssid);

        wifi_config_t sta_config = {0};
        strncpy((char *)sta_config.sta.ssid, wifi_cfg.ssid, sizeof(sta_config.sta.ssid));
        strncpy((char *)sta_config.sta.password, wifi_cfg.password, sizeof(sta_config.sta.password));

        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        esp_wifi_start();

        strncpy(s_status.ssid, wifi_cfg.ssid, sizeof(s_status.ssid));

        // 等待连接结果（最多 15 秒）
        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE,
            pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "Connected to %s, IP: %s", wifi_cfg.ssid, s_status.ip_addr);
            wifi_manager_start_mdns();
            return;
        }

        // 连接失败，切换到 AP 模式
        ESP_LOGW(TAG, "Failed to connect to %s, switching to AP mode", wifi_cfg.ssid);
        esp_wifi_stop();
    }

    // 无配置或连接失败 → AP 模式
    start_ap_mode();
}

// ============================================================
// 连接新 WiFi
// ============================================================
bool wifi_manager_connect(const char *ssid, const char *password) {
    ESP_LOGI(TAG, "Connecting to new WiFi: %s", ssid);

    // 停止当前 WiFi
    esp_wifi_stop();
    s_retry_count = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password));

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_start();

    strncpy(s_status.ssid, ssid, sizeof(s_status.ssid));

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        // 保存配置
        wifi_config_store_t cfg;
        strncpy(cfg.ssid, ssid, MAX_WIFI_SSID_LEN - 1);
        strncpy(cfg.password, password, MAX_WIFI_PASS_LEN - 1);
        cfg.configured = true;
        content_store_save_wifi(&cfg);

        wifi_manager_start_mdns();
        return true;
    }

    // 连接失败，回到 AP 模式
    esp_wifi_stop();
    start_ap_mode();
    return false;
}

// ============================================================
// mDNS
// ============================================================
void wifi_manager_start_mdns(void) {
    mdns_init();
    mdns_hostname_set("menu-display");
    mdns_instance_name_set("菜单显示屏");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS started: http://menu-display.local");
}

// ============================================================
// 状态查询
// ============================================================
void wifi_manager_get_status(wifi_status_t *status) {
    memcpy(status, &s_status, sizeof(wifi_status_t));

    // 更新 RSSI
    if (s_status.state == WIFI_STATE_CONNECTED) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_status.rssi = ap_info.rssi;
            status->rssi = ap_info.rssi;
        }
    }
}

bool wifi_manager_is_connected(void) {
    return s_status.state == WIFI_STATE_CONNECTED;
}

bool wifi_manager_is_ap_mode(void) {
    return s_status.state == WIFI_STATE_AP_MODE;
}
