#pragma once

/**
 * @file ui_manager.h
 * @brief UI管理器接口 (LVGL)
 * 
 * 管理所有UI界面，包括：
 * - 主界面：速度显示 + 情感表达 + 语音状态
 * - 行程统计界面
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "speed_sensor.h"
#include "empathy_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UI界面枚举
 */
typedef enum {
    UI_SCREEN_MAIN = 0,   // 主界面（速度+情感）
    UI_SCREEN_STATS,      // 行程统计界面
    UI_SCREEN_COUNT
} ui_screen_t;

/**
 * @brief 初始化UI管理器
 * @return ESP_OK 成功
 */
esp_err_t ui_manager_init(void);

/**
 * @brief 切换界面
 * @param screen 目标界面
 */
void ui_manager_switch_screen(ui_screen_t screen);

/**
 * @brief 更新速度显示
 * @param data 速度数据
 */
void ui_manager_update_speed(const speed_data_t *data);

/**
 * @brief 更新情感显示
 * @param response 共情响应
 */
void ui_manager_update_empathy(const empathy_response_t *response);

/**
 * @brief 更新语音状态显示
 * @param is_awake  语音模块是否已唤醒
 * @param is_speaking TTS是否正在播报
 */
void ui_manager_update_voice_status(bool is_awake, bool is_speaking);

/**
 * @brief 显示通知弹窗
 * @param title 标题
 * @param message 消息内容
 * @param duration_ms 显示时长（0=永久）
 */
void ui_manager_show_notification(const char *title, const char *message,
                                   uint32_t duration_ms);

/**
 * @brief UI任务（在FreeRTOS任务中运行LVGL）
 * @param arg 未使用
 */
void ui_manager_task(void *arg);

#ifdef __cplusplus
}
#endif