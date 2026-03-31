#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 转向灯状态枚举
 */
typedef enum {
    TURN_SIGNAL_NONE  = 0,  /*!< 无转向 */
    TURN_SIGNAL_LEFT  = 1,  /*!< 左转 */
    TURN_SIGNAL_RIGHT = 2,  /*!< 右转 */
    TURN_SIGNAL_BOTH  = 3,  /*!< 双闪 */
} turn_signal_state_t;

/**
 * @brief 转向灯变化回调函数类型
 *
 * @param state 新的转向灯状态
 * @param user_data 用户数据指针
 */
typedef void (*turn_signal_callback_t)(turn_signal_state_t state, void *user_data);

/**
 * @brief 转向灯检测配置
 */
typedef struct {
    int  pin_left;          /*!< 左转向灯 GPIO (高电平有效) */
    int  pin_right;         /*!< 右转向灯 GPIO (高电平有效) */
    int  debounce_ms;       /*!< 防抖时间 (ms) */
    turn_signal_callback_t callback;    /*!< 状态变化回调 */
    void                  *user_data;   /*!< 回调用户数据 */
} turn_signal_config_t;

/**
 * @brief 初始化转向灯检测
 *
 * @param config 配置参数
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t turn_signal_init(const turn_signal_config_t *config);

/**
 * @brief 反初始化转向灯检测
 */
void turn_signal_deinit(void);

/**
 * @brief 获取当前转向灯状态
 *
 * @return turn_signal_state_t 当前状态
 */
turn_signal_state_t turn_signal_get_state(void);

/**
 * @brief 检查是否正在左转
 */
bool turn_signal_is_left(void);

/**
 * @brief 检查是否正在右转
 */
bool turn_signal_is_right(void);

/**
 * @brief 获取转向灯状态名称字符串
 */
const char *turn_signal_state_name(turn_signal_state_t state);

#ifdef __cplusplus
}
#endif