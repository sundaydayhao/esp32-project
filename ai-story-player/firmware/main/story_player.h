#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * 初始化故事播放器
 * @param msg_queue 主消息队列（用于发送完成通知）
 */
esp_err_t story_player_init(QueueHandle_t msg_queue);

/**
 * 处理来自服务器的 WebSocket 消息
 * @param data      JSON 消息数据
 * @param len       数据长度
 */
void story_player_handle_message(const char *data, size_t len);

/**
 * 停止当前故事播放
 */
void story_player_stop(void);

/**
 * 检查是否正在播放
 */
bool story_player_is_playing(void);

/**
 * 反初始化
 */
void story_player_deinit(void);