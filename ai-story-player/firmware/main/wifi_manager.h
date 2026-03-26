#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef void (*wifi_connected_cb_t)(void);
typedef void (*wifi_disconnected_cb_t)(void);

/**
 * 初始化 WiFi 管理器
 */
void wifi_manager_init(wifi_connected_cb_t on_connected, wifi_disconnected_cb_t on_disconnected);

/**
 * 连接到 WiFi 网络
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * 断开 WiFi 连接
 */
void wifi_manager_disconnect(void);

/**
 * 检查 WiFi 是否已连接
 */
bool wifi_manager_is_connected(void);

/**
 * 获取 IP 地址字符串
 */
void wifi_manager_get_ip(char *ip_buf, size_t buf_len);