#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 连接到 WiFi 网络
 *
 * 使用 Kconfig 中配置的 SSID 和密码连接 WiFi。
 * 此函数会阻塞直到连接成功或超过最大重试次数。
 *
 * @return ESP_OK 连接成功，ESP_FAIL 连接失败
 */
esp_err_t wifi_connect(void);

/**
 * @brief 断开 WiFi 连接
 */
void wifi_disconnect(void);

/**
 * @brief 检查 WiFi 是否已连接
 *
 * @return true 已连接，false 未连接
 */
bool wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
