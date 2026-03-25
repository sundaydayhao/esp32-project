#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 运动事件类型
 */
typedef enum {
    MOTION_EVENT_PERSON,    /*!< 检测到人员活动 */
    MOTION_EVENT_PACKAGE,   /*!< 检测到快递包裹 */
    MOTION_EVENT_CLEAR,     /*!< 门口恢复正常 */
} motion_event_type_t;

/**
 * @brief 运动事件数据
 */
typedef struct {
    motion_event_type_t type;       /*!< 事件类型 */
    uint32_t            timestamp;  /*!< 事件时间戳（毫秒） */
    float               diff_ratio; /*!< 帧差比例 (0.0-1.0) */
} motion_event_t;

/**
 * @brief 运动检测回调函数类型
 * @param event 运动事件数据
 * @param user_data 用户自定义数据
 */
typedef void (*motion_callback_t)(const motion_event_t *event, void *user_data);

/**
 * @brief 初始化运动检测器
 * @return ESP_OK 成功
 */
esp_err_t motion_detector_init(void);

/**
 * @brief 注册运动检测回调函数
 * @param callback 回调函数
 * @param user_data 传递给回调的用户数据
 */
void motion_detector_register_callback(motion_callback_t callback, void *user_data);

/**
 * @brief 启动运动检测任务
 * @return ESP_OK 成功
 */
esp_err_t motion_detector_start(void);

/**
 * @brief 停止运动检测任务
 */
void motion_detector_stop(void);

/**
 * @brief 手动触发运动检测（用于测试）
 */
void motion_detector_trigger(void);

/**
 * @brief 获取最后一次运动检测时间戳
 * @return 时间戳（毫秒），0 表示从未检测到
 */
uint32_t motion_detector_get_last_motion_time(void);

/**
 * @brief 检查当前是否处于冷却期
 * @return true 处于冷却期，false 可以触发
 */
bool motion_detector_is_in_cooldown(void);

#ifdef __cplusplus
}
#endif