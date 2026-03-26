#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief LED状态枚举
 */
typedef enum {
    LED_STATE_OFF = 0,          // 关闭
    LED_STATE_IDLE,             // 空闲 (绿色常亮)
    LED_STATE_CONNECTING,       // 连接WiFi (蓝色慢闪)
    LED_STATE_CONNECTED,        // 已连接 (绿色常亮)
    LED_STATE_LISTENING,        // 录音中 (红色呼吸)
    LED_STATE_PROCESSING,       // AI处理中 (黄色快闪)
    LED_STATE_SPEAKING,         // TTS播放 (青色呼吸)
    LED_STATE_WAKEUP,           // 唤醒 (紫色闪烁)
    LED_STATE_ERROR,            // 错误 (红色快闪)
} led_state_t;

/**
 * @brief 初始化LED指示灯
 * @return ESP_OK 成功
 */
esp_err_t led_indicator_init(void);

/**
 * @brief 设置LED状态
 * @param state LED状态
 */
void led_indicator_set_state(led_state_t state);

/**
 * @brief 设置LED颜色 (直接控制)
 * @param r 红色分量 (0-255)
 * @param g 绿色分量 (0-255)
 * @param b 蓝色分量 (0-255)
 */
void led_indicator_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 关闭LED
 */
void led_indicator_off(void);

/**
 * @brief 反初始化LED
 */
void led_indicator_deinit(void);