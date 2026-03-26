#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WIFI_STATE_INIT = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_AP_MODE,         // 配置模式（热点）
} wifi_state_t;

typedef struct {
    wifi_state_t state;
    char ip_addr[16];           // 连接后的 IP 地址
    char ssid[32];              // 当前连接的 SSID
    int8_t rssi;                // 信号强度
} wifi_status_t;

/**
 * @brief 初始化 WiFi 管理器
 *        - 如果有保存的 WiFi 配置，尝试连接
 *        - 连接失败或无配置，开启 AP 热点模式
 */
void wifi_manager_init(void);

/**
 * @brief 获取当前 WiFi 状态
 */
void wifi_manager_get_status(wifi_status_t *status);

/**
 * @brief 使用新的 SSID/密码连接 WiFi
 * @return true 连接成功
 */
bool wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief 是否已连接到 WiFi（STA 模式）
 */
bool wifi_manager_is_connected(void);

/**
 * @brief 是否处于 AP 配置模式
 */
bool wifi_manager_is_ap_mode(void);

/**
 * @brief 启动 mDNS 服务（menu-display.local）
 */
void wifi_manager_start_mdns(void);