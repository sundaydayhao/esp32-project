#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief WebSocket连接状态
 */
typedef enum {
    WS_STATUS_DISCONNECTED = 0,
    WS_STATUS_CONNECTING,
    WS_STATUS_CONNECTED,
    WS_STATUS_ERROR,
} ws_status_t;

/**
 * @brief WebSocket消息类型
 */
typedef enum {
    WS_MSG_TEXT = 0,    // JSON文本消息
    WS_MSG_BINARY,      // 二进制音频数据
} ws_msg_type_t;

/**
 * @brief WebSocket接收消息结构
 */
typedef struct {
    ws_msg_type_t type;
    uint8_t *data;
    size_t   size;
} ws_message_t;

/**
 * @brief WebSocket事件回调类型
 */
typedef void (*ws_event_cb_t)(ws_status_t status, const ws_message_t *msg);

/**
 * @brief 初始化WebSocket客户端
 * @param uri WebSocket服务器URI (ws://host:port/path)
 * @param event_cb 事件回调函数
 * @return ESP_OK 成功
 */
esp_err_t ws_client_init(const char *uri, ws_event_cb_t event_cb);

/**
 * @brief 连接到WebSocket服务器
 * @return ESP_OK 成功
 */
esp_err_t ws_client_connect(void);

/**
 * @brief 断开WebSocket连接
 */
void ws_client_disconnect(void);

/**
 * @brief 发送文本消息 (JSON)
 * @param text JSON字符串
 * @return ESP_OK 成功
 */
esp_err_t ws_client_send_text(const char *text);

/**
 * @brief 发送二进制数据 (音频PCM)
 * @param data 数据指针
 * @param size 数据大小
 * @return ESP_OK 成功
 */
esp_err_t ws_client_send_binary(const uint8_t *data, size_t size);

/**
 * @brief 获取当前连接状态
 * @return WebSocket状态
 */
ws_status_t ws_client_get_status(void);

/**
 * @brief 反初始化WebSocket客户端
 */
void ws_client_deinit(void);