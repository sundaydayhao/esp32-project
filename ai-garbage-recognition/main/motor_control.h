/**
 * motor_control.h
 * 舵机电机控制模块接口 / Motor Control Interface
 *
 * 使用 ESP-IDF LEDC (PWM) 驱动舵机
 * 50Hz PWM, 脉冲宽度 500-2500us 对应 0-180 度
 */

#pragma once

#include "app_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化所有舵机
 * @return ESP_OK 成功
 */
esp_err_t motor_init(void);

/**
 * @brief 所有舵机复位到初始位置
 */
void motor_reset_all(void);

/**
 * @brief 根据垃圾类型执行分拣动作
 *        旋转平台到对应方向，打开挡板，等待，关闭挡板
 * @param type 垃圾类型
 */
void motor_sort_garbage(garbage_type_t type);

/**
 * @brief 设置指定舵机角度 (平滑移动)
 * @param gpio_num 舵机 GPIO 引脚
 * @param angle 目标角度 0-180
 */
void motor_set_angle(int gpio_num, int angle);

/**
 * @brief 立即设置舵机角度 (无平滑)
 * @param gpio_num 舵机 GPIO 引脚
 * @param angle 目标角度 0-180
 */
void motor_set_angle_immediate(int gpio_num, int angle);

#ifdef __cplusplus
}
#endif