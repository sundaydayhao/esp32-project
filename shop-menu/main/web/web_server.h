#pragma once

#include "content_store.h"
#include <stdbool.h>

/**
 * @brief 启动 HTTP Web 服务器
 *        提供菜单管理页面和 REST API
 */
void web_server_start(void);

/**
 * @brief 停止 Web 服务器
 */
void web_server_stop(void);

/**
 * @brief 注册内容更新回调
 *        当商家通过 Web 界面更新菜单时触发
 */
typedef void (*content_update_cb_t)(const menu_content_t *new_content);
void web_server_set_content_callback(content_update_cb_t cb);

/**
 * @brief 注册 WiFi 配置回调
 *        当商家通过 Web 界面提交 WiFi 配置时触发
 */
typedef void (*wifi_config_cb_t)(const char *ssid, const char *password);
void web_server_set_wifi_callback(wifi_config_cb_t cb);