/**
 * wifi_manager.h
 * WiFi 连接管理模块 / WiFi Manager Interface
 *
 * 用于在线垃圾类型查询
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化并连接 WiFi
 * @param ssid     WiFi 名称
 * @param password WiFi 密码
 * @param timeout_ms 连接超时 (ms)
 * @return ESP_OK 连接成功
 */
esp_err_t wifi_connect(const char *ssid, const char *password, uint32_t timeout_ms);

/**
 * @brief 断开 WiFi
 */
void wifi_disconnect(void);

/**
 * @brief 检查 WiFi 是否已连接
 */
bool wifi_is_connected(void);

/**
 * @brief 发送 HTTP GET 请求
 * @param url      请求 URL
 * @param response 响应缓冲区
 * @param resp_len 缓冲区大小
 * @return ESP_OK 成功
 */
esp_err_t http_get(const char *url, char *response, size_t resp_len);

#ifdef __cplusplus
}
#endif