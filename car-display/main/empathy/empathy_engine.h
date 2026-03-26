#pragma once

/**
 * @file empathy_engine.h
 * @brief 共情引擎接口
 * 
 * 根据驾驶状态（速度、转向、加速度）生成共情响应
 * 提供驾驶陪伴功能，包括情感表达和语音提示
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "speed_sensor.h"
#include "imu_sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 情感状态枚举
 */
typedef enum {
    MOOD_HAPPY = 0,      // 开心 😊 - 正常驾驶
    MOOD_EXCITED,        // 兴奋 🤩 - 加速/高速
    MOOD_WORRIED,        // 担心 😟 - 超速/急刹
    MOOD_SCARED,         // 害怕 😱 - 危险驾驶
    MOOD_SLEEPY,         // 困倦 😴 - 长时间低速
    MOOD_CURIOUS,        // 好奇 🤔 - 频繁转向
    MOOD_RELAXED,        // 放松 😌 - 平稳低速
    MOOD_CHEERING,       // 加油 💪 - 堵车/低速
    MOOD_COUNT
} empathy_mood_t;

/**
 * @brief 驾驶事件枚举
 */
typedef enum {
    EVENT_NONE = 0,
    EVENT_SPEED_UP,          // 加速
    EVENT_SPEED_DOWN,        // 减速
    EVENT_HARD_BRAKE,        // 急刹车
    EVENT_SHARP_TURN,        // 急转弯
    EVENT_OVERSPEED,         // 超速
    EVENT_TRAFFIC_JAM,       // 堵车
    EVENT_SMOOTH_DRIVE,      // 平稳驾驶
    EVENT_TRIP_START,        // 开始行程
    EVENT_LONG_DRIVE,        // 长途驾驶
    EVENT_BUMPY_ROAD,        // 颠簸路面
} empathy_event_t;

/**
 * @brief 共情消息结构
 */
typedef struct {
    empathy_mood_t mood;         // 当前情感状态
    empathy_event_t event;       // 触发事件
    const char *message;         // 共情消息（中文）
    const char *emoji;           // 表情符号
    uint32_t display_duration_ms; // 显示时长
} empathy_response_t;

/**
 * @brief 共情响应回调函数类型
 */
typedef void (*empathy_callback_t)(const empathy_response_t *response);

/**
 * @brief 初始化共情引擎
 * @return ESP_OK 成功
 */
esp_err_t empathy_engine_init(void);

/**
 * @brief 注册共情响应回调
 * @param cb 回调函数
 */
void empathy_engine_register_callback(empathy_callback_t cb);

/**
 * @brief 更新驾驶数据（由传感器回调调用）
 * @param speed 速度数据
 * @param behavior 驾驶行为数据
 */
void empathy_engine_update(const speed_data_t *speed, 
                            const driving_behavior_t *behavior);

/**
 * @brief 获取当前情感状态
 * @return 当前情感状态
 */
empathy_mood_t empathy_engine_get_mood(void);

/**
 * @brief 获取当前共情消息
 * @param response 输出共情响应
 */
void empathy_engine_get_response(empathy_response_t *response);

/**
 * @brief 处理用户聊天输入，生成回复
 * @param input 用户输入文本
 * @param output 输出回复文本
 * @param output_len 输出缓冲区长度
 */
void empathy_engine_chat(const char *input, char *output, size_t output_len);

/**
 * @brief 获取情感状态对应的表情字符串
 * @param mood 情感状态
 * @return 表情字符串
 */
const char *empathy_mood_to_emoji(empathy_mood_t mood);

/**
 * @brief 获取情感状态对应的名称
 * @param mood 情感状态
 * @return 状态名称（中文）
 */
const char *empathy_mood_to_name(empathy_mood_t mood);

#ifdef __cplusplus
}
#endif