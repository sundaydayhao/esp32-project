/**
 * @file wifi_manager.h
 * @brief WiFi 管理器（STA 模式，断线自动重连）
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING   = 1,
    WIFI_STATE_CONNECTED    = 2,
    WIFI_STATE_FAILED       = 3,
} wifi_state_t;

/**
 * @brief 初始化并启动 WiFi（STA 模式）
 *        SSID/密码从 Kconfig 读取
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief 获取当前 WiFi 状态
 */
wifi_state_t wifi_manager_get_state(void);

/**
 * @brief 是否已连接
 */
bool wifi_manager_is_connected(void);

/**
 * @brief 阻塞等待连接成功，超时返回 ESP_ERR_TIMEOUT
 * @param timeout_ms 超时毫秒数
 */
esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif