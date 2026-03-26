/**
 * @file wifi_manager.c
 * @brief WiFi 管理模块实现
 */

#include "wifi_manager.h"
#include "config.h"
#include "storage.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_mgr";

// ============================================================
// 内部状态
// ============================================================

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static wifi_state_t       s_wifi_state = WIFI_STATE_DISCONNECTED;
static int                s_retry_count = 0;
static char               s_sta_ip[20] = {0};
static char               s_ap_ip[20]  = "192.168.4.1";
static char               s_ssid[33]   = {0};

// ============================================================
// 事件处理
// ============================================================

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA 启动，开始连接...");
            s_wifi_state = WIFI_STATE_CONNECTING;
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disc =
                (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "WiFi断开 (reason=%d), 重试 %d/%d",
                     disc->reason, s_retry_count + 1, WIFI_STA_MAX_RETRY);

            if (s_retry_count < WIFI_STA_MAX_RETRY) {
                s_retry_count++;
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "WiFi连接失败，切换到AP模式");
                s_wifi_state = WIFI_STATE_AP_ONLY;
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            s_sta_ip[0] = '\0';
            break;
        }

        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event =
                (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "设备连接到AP: " MACSTR, MAC2STR(event->mac));
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *event =
                (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "设备断开AP: " MACSTR, MAC2STR(event->mac));
            break;
        }

        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR,
                     IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "获取IP: %s", s_sta_ip);
            s_retry_count = 0;
            s_wifi_state = WIFI_STATE_CONNECTED;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

// ============================================================
// 初始化
// ============================================================

esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        ESP_LOGE(TAG, "创建事件组失败");
        return ESP_ERR_NO_MEM;
    }

    // 初始化 TCP/IP 栈
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "WiFi管理器初始化完成");
    return ESP_OK;
}

// ============================================================
// 启动 WiFi
// ============================================================

esp_err_t wifi_manager_start(void)
{
    // 创建默认网络接口
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap_netif  = esp_netif_create_default_wifi_ap();
    (void)sta_netif;
    (void)ap_netif;

    // 初始化 WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理器
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // 配置 AP 模式
    wifi_config_t ap_config = {
        .ap = {
            .ssid            = WIFI_AP_SSID,
            .ssid_len        = strlen(WIFI_AP_SSID),
            .channel         = WIFI_AP_CHANNEL,
            .password        = WIFI_AP_PASSWORD,
            .max_connection  = WIFI_AP_MAX_CONN,
            .authmode        = WIFI_AUTH_WPA2_PSK,
        },
    };
    if (strlen(WIFI_AP_PASSWORD) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // 尝试从 NVS 读取 STA 配置
    char sta_ssid[33] = {0};
    char sta_pass[65] = {0};
    bool has_sta_config = false;

    esp_err_t ret = storage_load_wifi(sta_ssid, sizeof(sta_ssid),
                                       sta_pass, sizeof(sta_pass));
    if (ret == ESP_OK && strlen(sta_ssid) > 0) {
        has_sta_config = true;
        strncpy(s_ssid, sta_ssid, sizeof(s_ssid) - 1);
        ESP_LOGI(TAG, "从NVS读取WiFi配置: SSID=%s", sta_ssid);
    }

    if (has_sta_config) {
        // AP+STA 模式
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

        wifi_config_t sta_config = {0};
        strncpy((char *)sta_config.sta.ssid, sta_ssid,
                sizeof(sta_config.sta.ssid) - 1);
        strncpy((char *)sta_config.sta.password, sta_pass,
                sizeof(sta_config.sta.password) - 1);
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        // 等待连接结果 (最多10秒)
        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE,
            pdMS_TO_TICKS(10000));

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi STA 连接成功: %s, IP: %s", sta_ssid, s_sta_ip);
            s_wifi_state = WIFI_STATE_AP_STA;
        } else {
            ESP_LOGW(TAG, "WiFi STA 连接失败，仅使用AP模式");
            s_wifi_state = WIFI_STATE_AP_ONLY;
        }
    } else {
        // 仅 AP 模式
        ESP_LOGI(TAG, "无STA配置，启动AP模式: SSID=%s", WIFI_AP_SSID);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        s_wifi_state = WIFI_STATE_AP_ONLY;
        strncpy(s_ssid, WIFI_AP_SSID, sizeof(s_ssid) - 1);
    }

    ESP_LOGI(TAG, "WiFi启动完成，状态: %d", s_wifi_state);
    return ESP_OK;
}

// ============================================================
// API 实现
// ============================================================

wifi_state_t wifi_manager_get_state(void)
{
    return s_wifi_state;
}

bool wifi_manager_get_ip(char *buf, size_t buf_len)
{
    if (strlen(s_sta_ip) > 0) {
        strncpy(buf, s_sta_ip, buf_len - 1);
        buf[buf_len - 1] = '\0';
        return true;
    }
    return false;
}

void wifi_manager_get_ap_ip(char *buf, size_t buf_len)
{
    strncpy(buf, s_ap_ip, buf_len - 1);
    buf[buf_len - 1] = '\0';
}

void wifi_manager_get_ssid(char *buf, size_t buf_len)
{
    strncpy(buf, s_ssid, buf_len - 1);
    buf[buf_len - 1] = '\0';
}

bool wifi_manager_is_connected(void)
{
    return (s_wifi_state == WIFI_STATE_CONNECTED ||
            s_wifi_state == WIFI_STATE_AP_STA);
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) return ESP_ERR_INVALID_ARG;

    // 保存到 NVS
    storage_save_wifi(ssid, password ? password : "");

    // 更新 STA 配置并重连
    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid,
            sizeof(sta_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)sta_config.sta.password, password,
                sizeof(sta_config.sta.password) - 1);
    }
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    s_retry_count = 0;
    s_wifi_state = WIFI_STATE_CONNECTING;
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);

    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    esp_wifi_connect();

    ESP_LOGI(TAG, "正在连接到 WiFi: %s", ssid);
    return ESP_OK;
}