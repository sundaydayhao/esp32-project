/*
 * 事件检测器模块
 * 负责跌倒事件的识别和确认
 */

#ifndef EVENT_DETECTOR_H
#define EVENT_DETECTOR_H

#include "esp_err.h"
#include "motion_gate.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 事件状态枚举
 */
typedef enum {
    EVENT_STATE_NORMAL = 0,         // 正常状态
    EVENT_STATE_SUSPECT_FALL = 1,   // 疑似跌倒
    EVENT_STATE_CONFIRMED_FALL = 2, // 确认跌倒
} event_state_t;

/**
 * @brief 事件检测器配置
 */
typedef struct {
    float fall_threshold;           // 跌倒检测阈值 (0.0 - 1.0)
    uint32_t confirm_duration_ms;   // 确认持续时间 (毫秒)
} event_detector_config_t;

/**
 * @brief 跌倒事件信息
 */
typedef struct {
    uint64_t timestamp_us;          // 事件时间戳
    event_state_t state;            // 事件状态
    float confidence;               // 置信度 (0.0 - 1.0)
    motion_state_t motion_before;   // 事件前运动状态
    motion_state_t motion_after;    // 事件后运动状态
} fall_event_t;

/**
 * @brief 跌倒事件回调函数类型
 * 
 * @param event 跌倒事件信息
 * @param user_ctx 用户上下文
 */
typedef void (*fall_event_callback_t)(const fall_event_t *event, void *user_ctx);

/**
 * @brief 初始化事件检测器
 * 
 * @param config 配置参数
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t event_detector_init(const event_detector_config_t *config);

/**
 * @brief 运动状态变化通知
 * 
 * @param state 新的运动状态
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t event_detector_on_motion_change(motion_state_t state);

/**
 * @brief 获取当前事件状态
 * 
 * @return event_state_t 当前事件状态
 */
event_state_t event_detector_get_state(void);

/**
 * @brief 注册跌倒事件回调
 * 
 * @param callback 回调函数
 * @param user_ctx 用户上下文
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t event_detector_register_callback(fall_event_callback_t callback, void *user_ctx);

/**
 * @brief 获取最近的跌倒事件
 * 
 * @param event 输出事件信息
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t event_detector_get_last_event(fall_event_t *event);

/**
 * @brief 重置事件检测器状态
 * 
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t event_detector_reset(void);

#ifdef __cplusplus
}
#endif

#endif // EVENT_DETECTOR_H