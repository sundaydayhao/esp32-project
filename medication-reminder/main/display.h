#pragma once

/**
 * @file display.h
 * @brief ILI9341 TFT LCD 显示驱动（240x320，SPI接口）
 *
 * 专为老年人设计：大字体、高对比度、清晰界面
 * 支持三种显示状态：待机、提醒、确认
 */

#include <stdint.h>
#include <stdbool.h>
#include "reminder.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 颜色定义（RGB565） ==================== */
#define COLOR_BLACK         0x0000
#define COLOR_WHITE         0xFFFF
#define COLOR_RED           0xF800
#define COLOR_GREEN         0x07E0
#define COLOR_BLUE          0x001F
#define COLOR_YELLOW        0xFFE0
#define COLOR_CYAN          0x07FF
#define COLOR_MAGENTA       0xF81F
#define COLOR_ORANGE        0xFD20
#define COLOR_DARK_GRAY     0x4208
#define COLOR_LIGHT_GRAY    0xC618
#define COLOR_DARK_GREEN    0x03E0
#define COLOR_DARK_RED      0x7800
#define COLOR_NAVY          0x000F
#define COLOR_BG_STANDBY    0x0010  // 深蓝色背景（待机）
#define COLOR_BG_ALERT      0x4000  // 深红色背景（提醒）
#define COLOR_BG_CONFIRM    0x0200  // 深绿色背景（确认）

/* ==================== 屏幕尺寸 ==================== */
#define LCD_WIDTH           240
#define LCD_HEIGHT          320

/* ==================== 字体大小 ==================== */
typedef enum {
    FONT_SMALL  = 1,    // 6x8  像素（状态信息）
    FONT_MEDIUM = 2,    // 12x16 像素（普通文字）
    FONT_LARGE  = 3,    // 18x24 像素（药品名称）
    FONT_XLARGE = 4,    // 24x32 像素（时间显示）
} font_size_t;

/* ==================== 显示状态 ==================== */
typedef enum {
    DISPLAY_STATE_STANDBY = 0,  // 待机界面
    DISPLAY_STATE_ALERT,        // 提醒界面
    DISPLAY_STATE_CONFIRMED,    // 确认界面
    DISPLAY_STATE_HISTORY,      // 历史记录界面
} display_state_t;

/* ==================== 函数声明 ==================== */

/**
 * @brief 初始化 ILI9341 显示屏
 *
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t display_init(void);

/**
 * @brief 清屏（填充指定颜色）
 *
 * @param color 填充颜色（RGB565）
 */
void display_clear(uint16_t color);

/**
 * @brief 填充矩形区域
 *
 * @param x 起始X坐标
 * @param y 起始Y坐标
 * @param w 宽度
 * @param h 高度
 * @param color 颜色
 */
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/**
 * @brief 绘制水平线
 */
void display_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);

/**
 * @brief 在指定位置绘制字符（ASCII）
 *
 * @param x X坐标
 * @param y Y坐标
 * @param c 字符
 * @param fg 前景色
 * @param bg 背景色
 * @param size 字体大小倍数（1-4）
 */
void display_draw_char(int16_t x, int16_t y, char c,
                       uint16_t fg, uint16_t bg, uint8_t size);

/**
 * @brief 在指定位置绘制字符串（ASCII）
 *
 * @param x X坐标
 * @param y Y坐标
 * @param str 字符串
 * @param fg 前景色
 * @param bg 背景色
 * @param size 字体大小倍数（1-4）
 */
void display_draw_string(int16_t x, int16_t y, const char *str,
                         uint16_t fg, uint16_t bg, uint8_t size);

/**
 * @brief 居中绘制字符串
 *
 * @param y Y坐标
 * @param str 字符串
 * @param fg 前景色
 * @param bg 背景色
 * @param size 字体大小倍数
 */
void display_draw_string_centered(int16_t y, const char *str,
                                  uint16_t fg, uint16_t bg, uint8_t size);

/**
 * @brief 显示待机界面
 *
 * 显示当前时间、日期、下次提醒信息
 *
 * @param time_str 时间字符串 "HH:MM:SS"
 * @param date_str 日期字符串 "YYYY-MM-DD"
 * @param weekday_str 星期字符串 "周X"
 * @param next_reminder 下一个提醒（NULL 表示无）
 */
void display_show_standby(const char *time_str, const char *date_str,
                          const char *weekday_str,
                          const reminder_t *next_reminder);

/**
 * @brief 显示提醒界面
 *
 * 显示药品名称、剂量、提醒时间、操作提示
 *
 * @param reminder 当前触发的提醒
 * @param escalated 是否已升级（持续蜂鸣状态）
 */
void display_show_alert(const reminder_t *reminder, bool escalated);

/**
 * @brief 显示确认界面
 *
 * 显示"已服药"确认信息
 *
 * @param medication_name 药品名称
 */
void display_show_confirmed(const char *medication_name);

/**
 * @brief 显示历史记录界面
 *
 * @param records JSON 格式的历史记录字符串
 * @param page 页码（从0开始）
 */
void display_show_history(const char *records, int page);

/**
 * @brief 显示启动画面
 */
void display_show_splash(void);

/**
 * @brief 显示 WiFi 连接状态
 *
 * @param connecting true=连接中, false=已连接/失败
 * @param ssid WiFi SSID
 */
void display_show_wifi_status(bool connecting, const char *ssid);

/**
 * @brief 设置背光亮度
 *
 * @param brightness 亮度（0-255）
 */
void display_set_backlight(uint8_t brightness);

#ifdef __cplusplus
}
#endif