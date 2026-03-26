#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief WiFi连接状态枚举
 */
typedef enum {
    WIFI_STATUS_DISCONNECTED = 0,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_FAILED,
} wifi_status_t;

/**
 * @brief WiFi状态变化回调函数类型
 */
typedef void (*wifi_status_cb_t)(wifi_status_t status);

/**
 * @brief 初始化WiFi管理器
 * @param status_cb 状态变化回调 (可为NULL)
 * @return ESP_OK 成功
 */
esp_err_t wifi_manager_init(wifi_status_cb_t status_cb);

/**
 * @brief 连接WiFi
 * @param ssid WiFi名称
 * @param password WiFi密码
 * @return ESP_OK 成功
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief 断开WiFi连接
 */
void wifi_manager_disconnect(void);

/**
 * @brief 获取当前WiFi状态
 * @return WiFi状态
 */
wifi_status_t wifi_manager_get_status(void);

/**
 * @brief 等待WiFi连接完成
 * @param timeout_ms 超时时间 (ms)
 * @return ESP_OK 连接成功, ESP_ERR_TIMEOUT 超时
 */
esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms);

/**
 * @brief 获取本机IP地址字符串
 * @param buf 输出缓冲区
 * @param buf_len 缓冲区长度
 */
void wifi_manager_get_ip(char *buf, size_t buf_len);

/**
 * @brief 反初始化WiFi管理器
 */
void wifi_manager_deinit(void);