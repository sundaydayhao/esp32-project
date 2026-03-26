/**
 * @file buzzer.h
 * @brief 蜂鸣器驱动模块
 *
 * 使用 LEDC PWM 驱动有源/无源蜂鸣器，提供多种提示音效
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化蜂鸣器
 * @return ESP_OK 成功
 */
esp_err_t buzzer_init(void);

/**
 * @brief 播放成功提示音 (短促双响)
 */
void buzzer_beep_success(void);

/**
 * @brief 播放错误提示音 (长鸣)
 */
void buzzer_beep_error(void);

/**
 * @brief 播放按键音 (短促单响)
 */
void buzzer_beep_key(void);

/**
 * @brief 播放警告音 (三短响)
 */
void buzzer_beep_warning(void);

/**
 * @brief 播放开门音 (上升音调)
 */
void buzzer_beep_open(void);

/**
 * @brief 自定义蜂鸣
 * @param freq_hz   频率 (Hz)
 * @param duration_ms 持续时间 (ms)
 */
void buzzer_beep(uint32_t freq_hz, uint32_t duration_ms);

/**
 * @brief 停止蜂鸣
 */
void buzzer_stop(void);

#ifdef __cplusplus
}
#endif