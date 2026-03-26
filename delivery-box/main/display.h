/**
 * @file display.h
 * @brief 显示界面管理模块
 *
 * 基于 SSD1306 驱动，实现智能快递柜的 UI 界面
 *
 * 界面状态机:
 *   IDLE       -> 待机界面 (显示格口状态总览)
 *   ENTER_CODE -> 输入取件码界面
 *   DEPOSITING -> 存件界面 (快递员操作)
 *   SUCCESS    -> 操作成功界面
 *   ERROR      -> 错误提示界面
 *   ADMIN      -> 管理员界面
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// 界面状态
// ============================================================

typedef enum {
    DISPLAY_STATE_IDLE       = 0,   ///< 待机界面
    DISPLAY_STATE_ENTER_CODE = 1,   ///< 输入取件码
    DISPLAY_STATE_DEPOSITING = 2,   ///< 存件操作中
    DISPLAY_STATE_SUCCESS    = 3,   ///< 操作成功
    DISPLAY_STATE_ERROR      = 4,   ///< 错误提示
    DISPLAY_STATE_ADMIN      = 5,   ///< 管理员界面
    DISPLAY_STATE_WIFI_INFO  = 6,   ///< WiFi信息界面
} display_state_t;

// ============================================================
// API
// ============================================================

/**
 * @brief 初始化显示模块
 * @return ESP_OK 成功
 */
esp_err_t display_init(void);

/**
 * @brief 显示任务 (在FreeRTOS任务中运行)
 * @param arg 未使用
 */
void display_task(void *arg);

/**
 * @brief 切换到待机界面
 */
void display_show_idle(void);

/**
 * @brief 切换到输入取件码界面
 * @param entered_digits 已输入的数字字符串
 * @param digit_count    已输入位数
 */
void display_show_enter_code(const char *entered_digits, uint8_t digit_count);

/**
 * @brief 切换到存件界面
 * @param locker_id 格口编号
 * @param code      取件码
 */
void display_show_depositing(uint8_t locker_id, const char *code);

/**
 * @brief 切换到成功界面
 * @param message 成功消息
 * @param locker_id 格口编号 (0xFF=不显示)
 */
void display_show_success(const char *message, uint8_t locker_id);

/**
 * @brief 切换到错误界面
 * @param message    错误消息
 * @param retry_left 剩余重试次数 (0xFF=不显示)
 */
void display_show_error(const char *message, uint8_t retry_left);

/**
 * @brief 切换到WiFi信息界面
 * @param ip_addr IP地址字符串
 * @param ssid    WiFi SSID
 */
void display_show_wifi_info(const char *ip_addr, const char *ssid);

/**
 * @brief 获取当前界面状态
 * @return 当前状态
 */
display_state_t display_get_state(void);

/**
 * @brief 强制刷新显示
 */
void display_refresh(void);

#ifdef __cplusplus
}
#endif