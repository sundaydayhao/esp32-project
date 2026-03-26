#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief AI助手工作状态
 */
typedef enum {
    AI_STATE_IDLE = 0,          // 空闲，等待唤醒
    AI_STATE_WAKEUP,            // 已唤醒，准备录音
    AI_STATE_LISTENING,         // 录音中
    AI_STATE_PROCESSING,        // 发送数据，等待AI回复
    AI_STATE_SPEAKING,          // 播放TTS回复
    AI_STATE_ERROR,             // 错误状态
} ai_state_t;

/**
 * @brief AI助手事件类型
 */
typedef enum {
    AI_EVENT_WAKEUP = 0,        // 唤醒事件 (唤醒词或按键)
    AI_EVENT_SPEECH_START,      // 检测到语音开始
    AI_EVENT_SPEECH_END,        // 检测到语音结束 (静音超时)
    AI_EVENT_INTERRUPT,         // 打断当前播放
    AI_EVENT_RESET,             // 重置到空闲状态
} ai_event_t;

/**
 * @brief 初始化AI助手
 * @return ESP_OK 成功
 */
esp_err_t ai_assistant_init(void);

/**
 * @brief 发送事件给AI助手状态机
 * @param event 事件类型
 */
void ai_assistant_send_event(ai_event_t event);

/**
 * @brief 获取当前AI助手状态
 * @return 当前状态
 */
ai_state_t ai_assistant_get_state(void);

/**
 * @brief 反初始化AI助手
 */
void ai_assistant_deinit(void);