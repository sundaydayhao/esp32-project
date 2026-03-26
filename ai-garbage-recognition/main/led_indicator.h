/**
 * led_indicator.h
 * LED 指示灯模块接口 / LED Indicator Interface
 */

#pragma once

#include "app_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_STATUS_OFF        = 0,
    LED_STATUS_READY      = 1,  // 慢闪 - 就绪
    LED_STATUS_DETECTING  = 2,  // 快闪 - 检测中
    LED_STATUS_CLASSIFYING= 3,  // 常亮 - 分类中
    LED_STATUS_SORTING    = 4,  // 常亮 - 分拣中
    LED_STATUS_ERROR      = 5,  // 双闪 - 错误
} led_status_t;

/**
 * @brief 初始化 LED 指示灯
 * @return ESP_OK 成功
 */
esp_err_t led_init(void);

/**
 * @brief 设置系统状态 LED 模式
 * @param status 状态模式
 */
void led_set_status(led_status_t status);

/**
 * @brief 点亮对应垃圾类型的 LED
 * @param type 垃圾类型
 */
void led_set_garbage_type(garbage_type_t type);

/**
 * @brief 关闭所有垃圾类型 LED
 */
void led_clear_garbage(void);

/**
 * @brief 开机自检 - 依次点亮所有 LED
 */
void led_self_test(void);

/**
 * @brief LED 更新任务 (需在主循环或定时器中调用)
 */
void led_update(void);

#ifdef __cplusplus
}
#endif