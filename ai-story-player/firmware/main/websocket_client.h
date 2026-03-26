#pragma once

#include <stdint.h>
#include <stddef.h>

typedef void (*ws_connected_cb_t)(void);
typedef void (*ws_disconnected_cb_t)(void);
typedef void (*ws_data_cb_t)(const char *data, size_t len);

/**
 * 初始化 WebSocket 客户端
 */
esp_err_t ws_client_init(const char *uri,
                          ws_connected_cb_t on_connected,
                          ws_disconnected_cb_t on_disconnected,
                          ws_data_cb_t on_data);

/**
 * 启动 WebSocket 客户端
 */
esp_err_t ws_client_start(void);

/**
 * 停止 WebSocket 客户端
 */
void ws_client_stop(void);

/**
 * 发送文本消息
 */
esp_err_t ws_send_text(const char *text);

/**
 * 发送 JSON 消息
 */
esp_err_t ws_send_json(const char *json_str);

/**
 * 发送音频数据（Base64 编码后发送）
 */
esp_err_t ws_send_audio(const uint8_t *audio_data, size_t audio_len, int sample_rate);

/**
 * 发送关键词
 */
esp_err_t ws_send_keywords(const char **keywords, int count);

/**
 * 发送 ping
 */
esp_err_t ws_send_ping(void);

/**
 * 检查连接状态
 */
bool ws_is_connected(void);