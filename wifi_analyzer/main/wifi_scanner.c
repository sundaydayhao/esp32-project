#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "wifi_scanner.h"

static const char *TAG = "WIFI";

/* AP 网络接口句柄 */
static esp_netif_t *s_ap_netif = NULL;

/* AP IP 地址缓冲区 */
static char s_ap_ip_str[16] = "192.168.4.1";

/* ------------------------------------------------------------------ */
/*  内部辅助：按 RSSI 降序排序（冒泡排序，结果数量少，够用）           */
/* ------------------------------------------------------------------ */
static void sort_by_rssi(wifi_scan_result_t *result)
{
    for (int i = 0; i < result->count - 1; i++) {
        for (int j = 0; j < result->count - 1 - i; j++) {
            if (result->networks[j].rssi < result->networks[j + 1].rssi) {
                wifi_network_t tmp = result->networks[j];
                result->networks[j]     = result->networks[j + 1];
                result->networks[j + 1] = tmp;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  wifi_scanner_init                                                   */
/* ------------------------------------------------------------------ */
esp_err_t wifi_scanner_init(void)
{
    /* 创建默认 AP 网络接口 */
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        ESP_LOGE(TAG, "创建 AP netif 失败");
        return ESP_FAIL;
    }

    /* 同时创建 STA 接口（用于扫描，不连接） */
    esp_netif_create_default_wifi_sta();

    /* WiFi 初始化 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* APSTA 模式：AP 提供热点，STA 用于扫描 */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    /* 配置 AP */
    wifi_config_t ap_config = {
        .ap = {
            .ssid           = WIFI_AP_SSID,
            .ssid_len       = strlen(WIFI_AP_SSID),
            .channel        = WIFI_AP_CHANNEL,
            .password       = WIFI_AP_PASSWORD,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    /* 启动 WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());

    /* 获取 AP IP 地址 */
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
        snprintf(s_ap_ip_str, sizeof(s_ap_ip_str), IPSTR, IP2STR(&ip_info.ip));
    }

    ESP_LOGI(TAG, "AP 热点已启动: SSID=%s  密码=%s  IP=%s",
             WIFI_AP_SSID, WIFI_AP_PASSWORD, s_ap_ip_str);

    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  wifi_scanner_scan                                                   */
/* ------------------------------------------------------------------ */
esp_err_t wifi_scanner_scan(wifi_scan_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(wifi_scan_result_t));

    ESP_LOGI(TAG, "[SCAN] 开始扫描...");

    /* 扫描配置：扫描所有信道，包含隐藏网络 */
    wifi_scan_config_t scan_cfg = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,           /* 0 = 扫描所有信道 */
        .show_hidden = true,        /* 显示隐藏网络 */
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    /* 同步扫描（阻塞直到完成） */
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "扫描启动失败: %s", esp_err_to_name(err));
        return err;
    }

    /* 获取扫描到的 AP 数量 */
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        ESP_LOGW(TAG, "[SCAN] 未发现任何网络");
        esp_wifi_scan_stop();
        return ESP_OK;
    }

    /* 限制最大结果数 */
    uint16_t fetch_count = (ap_count < WIFI_SCAN_MAX_RESULTS)
                           ? ap_count : WIFI_SCAN_MAX_RESULTS;

    /* 分配临时缓冲区获取原始扫描结果 */
    wifi_ap_record_t *ap_records = calloc(fetch_count, sizeof(wifi_ap_record_t));
    if (ap_records == NULL) {
        ESP_LOGE(TAG, "内存分配失败");
        esp_wifi_scan_stop();
        return ESP_ERR_NO_MEM;
    }

    err = esp_wifi_scan_get_ap_records(&fetch_count, ap_records);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "获取扫描结果失败: %s", esp_err_to_name(err));
        free(ap_records);
        return err;
    }

    /* 转换为内部结构 */
    result->count = fetch_count;
    for (int i = 0; i < fetch_count; i++) {
        wifi_network_t *net = &result->networks[i];
        memcpy(net->ssid, ap_records[i].ssid, sizeof(net->ssid));
        net->ssid[32]    = '\0';
        net->rssi        = ap_records[i].rssi;
        net->channel     = ap_records[i].primary;
        net->auth_mode   = ap_records[i].authmode;
        memcpy(net->bssid, ap_records[i].bssid, 6);

        ESP_LOGI(TAG, "[%2d] SSID: %-24s RSSI: %4d dBm  CH: %2d  %s",
                 i + 1,
                 strlen((char *)net->ssid) ? (char *)net->ssid : "<hidden>",
                 net->rssi,
                 net->channel,
                 wifi_auth_mode_str(net->auth_mode));
    }

    free(ap_records);

    /* 按信号强度排序 */
    sort_by_rssi(result);

    ESP_LOGI(TAG, "[SCAN] 完成，共 %d 个网络", result->count);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  wifi_scanner_get_ap_ip                                              */
/* ------------------------------------------------------------------ */
const char *wifi_scanner_get_ap_ip(void)
{
    return s_ap_ip_str;
}

/* ------------------------------------------------------------------ */
/*  wifi_rssi_to_bars                                                   */
/* ------------------------------------------------------------------ */
int wifi_rssi_to_bars(int8_t rssi)
{
    if (rssi >= -60) return 4;
    if (rssi >= -70) return 3;
    if (rssi >= -80) return 2;
    return 1;
}

/* ------------------------------------------------------------------ */
/*  wifi_auth_mode_str                                                  */
/* ------------------------------------------------------------------ */
const char *wifi_auth_mode_str(wifi_auth_mode_t auth_mode)
{
    switch (auth_mode) {
        case WIFI_AUTH_OPEN:          return "OPEN";
        case WIFI_AUTH_WEP:           return "WEP";
        case WIFI_AUTH_WPA_PSK:       return "WPA";
        case WIFI_AUTH_WPA2_PSK:      return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:  return "WPA/2";
        case WIFI_AUTH_WPA3_PSK:      return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
        default:                      return "UNKN";
    }
}