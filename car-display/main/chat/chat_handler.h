#pragma once

/**
 * @file chat_handler.h
 * @brief 蓝牙聊天接口 (BLE NimBLE)
 * 
 * 通过BLE提供聊天功能
 * 手机APP可以连接并发送消息，设备回复共情响应
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 聊天消息方向
 */
typedef enum {
    CHAT_MSG_FROM_USER = 0,  // 用户发来的消息
    CHAT_MSG_FROM_CAR,       // 小摆件回复的消息
} chat_msg_direction_t;

/**
 * @brief 聊天消息结构
 */
typedef struct {
    chat_msg_direction_t direction;
    char text[512];
    uint32_t timestamp;
} chat_message_t;

/**
 * @brief 消息接收回调函数类型
 */
typedef void (*chat_receive_callback_t)(const chat_message_t *msg);

/**
 * @brief 初始化蓝牙聊天
 * @return ESP_OK 成功
 */
esp_err_t chat_handler_init(void);

/**
 * @brief 注册消息接收回调
 * @param cb 回调函数
 */
void chat_handler_register_callback(chat_receive_callback_t cb);

/**
 * @brief 发送消息给已连接的手机
 * @param message 消息文本
 * @return ESP_OK 成功
 */
esp_err_t chat_handler_send(const char *message);

/**
 * @brief 获取BLE连接状态
 * @return true 已连接
 */
bool chat_handler_is_connected(void);

/**
 * @brief 获取已连接设备名称
 * @param name 输出设备名称
 * @param len 缓冲区长度
 */
void chat_handler_get_peer_name(char *name, size_t len);

/**
 * @brief 广播共情状态通知
 * @param mood_name 情感状态名称
 * @param message 共情消息
 */
void chat_handler_notify_mood(const char *mood_name, const char *message);

#ifdef __cplusplus
}
#endif