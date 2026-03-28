#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  常量定义                                                            */
/* ------------------------------------------------------------------ */
#define WIFI_SCAN_MAX_RESULTS   10      /* 最多保存的扫描结果数量 */
#define WIFI_AP_SSID            "WiFi-Analyzer"
#define WIFI_AP_PASSWORD        "12345678"
#define WIFI_AP_CHANNEL         6
#define WIFI_AP_MAX_CONN        4

/* ------------------------------------------------------------------ */
/*  数据结构                                                            */
/* ------------------------------------------------------------------ */

/** 单个 WiFi 网络信息 */
typedef struct {
    char     ssid[33];          /* SSID，最长 32 字节 + '\0' */
    int8_t   rssi;              /* 信号强度，单位 dBm，范围约 -100 ~ 0 */
    uint8_t  channel;           /* 信道号 */
    wifi_auth_mode_t auth_mode; /* 加密方式 */
    uint8_t  bssid[6];          /* MAC 地址 */
} wifi_network_t;

/** 一次扫描的全部结果 */
typedef struct {
    wifi_network_t networks[WIFI_SCAN_MAX_RESULTS];
    uint16_t       count;       /* 实际发现的网络数量 */
} wifi_scan_result_t;

/* ------------------------------------------------------------------ */
/*  接口函数                                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief 初始化 WiFi（APSTA 模式）
 *        - STA 模式用于扫描周边网络
 *        - AP  模式提供 Web 服务热点
 * @return ESP_OK 成功
 */
esp_err_t wifi_scanner_init(void);

/**
 * @brief 执行一次同步 WiFi 扫描（阻塞，约 1-3 秒）
 * @param[out] result  扫描结果，按 RSSI 从强到弱排序
 * @return ESP_OK 成功
 */
esp_err_t wifi_scanner_scan(wifi_scan_result_t *result);

/**
 * @brief 获取 AP 热点的 IP 地址字符串（如 "192.168.4.1"）
 * @return IP 字符串指针（静态缓冲区，无需释放）
 */
const char *wifi_scanner_get_ap_ip(void);

/**
 * @brief 将 RSSI 值转换为信号格数（1-4）
 * @param rssi  RSSI 值（dBm）
 * @return 1~4
 */
int wifi_rssi_to_bars(int8_t rssi);

/**
 * @brief 将加密方式枚举转换为简短字符串
 * @param auth_mode  加密方式
 * @return 如 "OPEN"、"WPA2"、"WPA3" 等
 */
const char *wifi_auth_mode_str(wifi_auth_mode_t auth_mode);

#ifdef __cplusplus
}
#endif