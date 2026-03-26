/**
 * @file wifi_manager.h
 * @brief WiFi 管理模块
 *
 * 支持两种工作模式:
 *   1. STA 模式: 连接到现有 WiFi 网络
 *   2. AP 模式:  创建热点供直接访问 (STA连接失败时自动启用)
 *
 * 也支持 AP+STA 同时工作模式
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// WiFi 状态
// ============================================================

typedef enum {
    WIFI_STATE_DISCONNECTED = 0,    ///< 未连接
    WIFI_STATE_CONNECTING   = 1,    ///< 正在连接
    WIFI_STATE_CONNECTED    = 2,    ///< STA 已连接
    WIFI_STATE_AP_ONLY      = 3,    ///< 仅 AP 模式
    WIFI_STATE_AP_STA       = 4,    ///< AP+STA 模式
} wifi_state_t;

// ============================================================
// API
// ============================================================

/**
 * @brief 初始化 WiFi 管理器
 * @return ESP_OK 成功
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief 启动 WiFi (先尝试STA，失败则启用AP)
 * @return ESP_OK 成功
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief 获取当前 WiFi 状态
 * @return WiFi 状态
 */
wifi_state_t wifi_manager_get_state(void);

/**
 * @brief 获取 STA 模式 IP 地址字符串
 * @param buf     输出缓冲区
 * @param buf_len 缓冲区长度
 * @return true=有IP, false=无IP
 */
bool wifi_manager_get_ip(char *buf, size_t buf_len);

/**
 * @brief 获取 AP 模式 IP 地址字符串
 * @param buf     输出缓冲区
 * @param buf_len 缓冲区长度
 */
void wifi_manager_get_ap_ip(char *buf, size_t buf_len);

/**
 * @brief 获取连接的 WiFi SSID
 * @param buf     输出缓冲区
 * @param buf_len 缓冲区长度
 */
void wifi_manager_get_ssid(char *buf, size_t buf_len);

/**
 * @brief 配置并连接到新的 WiFi 网络
 * @param ssid     WiFi SSID
 * @param password WiFi 密码
 * @return ESP_OK 成功
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief 检查是否已连接到 STA 网络
 * @return true=已连接
 */
bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif