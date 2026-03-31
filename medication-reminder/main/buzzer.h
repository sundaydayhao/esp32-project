#pragma once

/**
 * @file buzzer.h
 * @brief 蜂鸣器 + LED 控制模块
 *
 * 功能：
 * - 有源蜂鸣器控制（GPIO 开关）
 * - 红色 LED 控制
 * - 多种提醒模式（正常/升级/确认）
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 枚举定义 ==================== */

typedef enum {
    BUZZER_MODE_OFF = 0,        // 关闭
    BUZZER_MODE_NORMAL,         // 正常提醒（每2秒响一次）
    BUZZER_MODE_ESCALATED,      // 升级提醒（持续蜂鸣）
    BUZZER_MODE_CONFIRM,        // 确认音（短促两声）
    BUZZER_MODE_STARTUP,        // 启动音
} buzzer_mode_t;

/* ==================== 函数声明 ==================== */

/**
 * @brief 初始化蜂鸣器和 LED
 *
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t buzzer_init(void);

/**
 * @brief 设置蜂鸣器模式
 *
 * @param mode 蜂鸣器模式
 */
void buzzer_set_mode(buzzer_mode_t mode);

/**
 * @brief 获取当前蜂鸣器模式
 *
 * @return buzzer_mode_t 当前模式
 */
buzzer_mode_t buzzer_get_mode(void);

/**
 * @brief 蜂鸣器任务（在 FreeRTOS 任务中调用）
 *
 * 根据当前模式控制蜂鸣器和 LED 的节奏
 * 应在独立任务中以 100ms 间隔调用
 */
void buzzer_tick(void);

/**
 * @brief 直接控制蜂鸣器开关
 *
 * @param on true=开, false=关
 */
void buzzer_set(bool on);

/**
 * @brief 直接控制 LED 开关
 *
 * @param on true=开, false=关
 */
void led_set(bool on);

/**
 * @brief 播放确认音效（阻塞）
 */
void buzzer_play_confirm(void);

/**
 * @brief 播放启动音效（阻塞）
 */
void buzzer_play_startup(void);

#ifdef __cplusplus
}
#endif