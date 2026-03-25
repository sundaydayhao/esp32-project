#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi 管理器初始化
 * 初始化 WiFi 驱动和事件处理
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief 连接到配置的 WiFi 网络
 * @return ESP_OK 连接成功，ESP_FAIL 连接失败
 */
esp_err_t wifi_manager_connect(void);

/**
 * @brief 断开 WiFi 连接
 */
void wifi_manager_disconnect(void);

/**
 * @brief 检查 WiFi 是否已连接
 * @return true 已连接，false 未连接
 */
bool wifi_manager_is_connected(void);

/**
 * @brief 获取设备 IP 地址字符串
 * @param ip_buf 存储 IP 地址的缓冲区
 * @param buf_len 缓冲区长度
 * @return ESP_OK 成功
 */
esp_err_t wifi_manager_get_ip(char *ip_buf, size_t buf_len);

/**
 * @brief 等待 WiFi 连接（阻塞）
 * @param timeout_ms 超时时间（毫秒），0 表示永久等待
 * @return ESP_OK 连接成功，ESP_ERR_TIMEOUT 超时
 */
esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif