/**
 * @file lcd1602_gfx.h
 * @brief LCD1602 图形层：5x7 字体渲染 + 天气 Emoji 图标
 *
 * 本模块工作在图形模式（GDRAM）下，维护一个 128x64 帧缓冲区，
 * 所有绘制操作在缓冲区中完成，调用 lcd1602_gfx_flush() 一次性刷新到屏幕。
 *
 * 天气图标（16x16 单色位图）：
 *   ☀  晴天（太阳 + 射线）
 *   ⛅  多云（云朵）
 *   🌧  雨天（云 + 雨滴）
 *   ❄  雪天（雪花）
 *   ⚡  雷电（闪电）
 *   🌫  雾/霾（水平线条）
 *   ?   未知
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
 * 天气图标枚举
 * ---------------------------------------------------------------- */
typedef enum {
    WEATHER_ICON_SUNNY    = 0,  /* 晴 */
    WEATHER_ICON_CLOUDY   = 1,  /* 多云 */
    WEATHER_ICON_OVERCAST = 2,  /* 阴 */
    WEATHER_ICON_RAIN     = 3,  /* 雨 */
    WEATHER_ICON_SNOW     = 4,  /* 雪 */
    WEATHER_ICON_THUNDER  = 5,  /* 雷电 */
    WEATHER_ICON_FOG      = 6,  /* 雾/霾 */
    WEATHER_ICON_UNKNOWN  = 7,  /* 未知 */
    WEATHER_ICON_COUNT    = 8,
} weather_icon_t;

/* ----------------------------------------------------------------
 * 初始化与刷新
 * ---------------------------------------------------------------- */

/**
 * @brief 初始化 GFX 层（进入图形模式，清空帧缓冲区）
 */
void lcd1602_gfx_init(void);

/**
 * @brief 清空帧缓冲区（不立即刷新屏幕）
 */
void lcd1602_gfx_clear(void);

/**
 * @brief 将帧缓冲区内容刷新到 LCD 屏幕
 */
void lcd1602_gfx_flush(void);

/* ----------------------------------------------------------------
 * 基本绘制
 * ---------------------------------------------------------------- */

/**
 * @brief 绘制单个像素
 * @param x    列 0~127
 * @param y    行 0~63
 * @param on   true=点亮，false=熄灭
 */
void lcd1602_gfx_pixel(uint8_t x, uint8_t y, bool on);

/**
 * @brief 绘制水平线
 */
void lcd1602_gfx_hline(uint8_t x, uint8_t y, uint8_t w, bool on);

/**
 * @brief 绘制矩形边框
 */
void lcd1602_gfx_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on);

/**
 * @brief 绘制填充矩形
 */
void lcd1602_gfx_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on);

/* ----------------------------------------------------------------
 * 文字渲染（5x7 ASCII 字体，字符间距 6x8）
 * ---------------------------------------------------------------- */

/**
 * @brief 绘制单个 ASCII 字符
 * @param x    左上角列坐标
 * @param y    左上角行坐标
 * @param c    ASCII 字符（32~126）
 * @param on   true=亮色，false=暗色
 */
void lcd1602_gfx_char(uint8_t x, uint8_t y, char c, bool on);

/**
 * @brief Draw one ASCII character with integer scaling
 * @param x      Top-left x
 * @param y      Top-left y
 * @param c      ASCII character, 32~126
 * @param scale  Pixel scale factor, >= 1
 * @param on     true=lit, false=dark
 */
void lcd1602_gfx_char_scaled(uint8_t x, uint8_t y, char c, uint8_t scale, bool on);

/**
 * @brief 绘制 ASCII 字符串
 * @param x    起始列坐标
 * @param y    起始行坐标
 * @param str  字符串（仅支持 ASCII 32~126）
 * @param on   true=亮色，false=暗色
 */
void lcd1602_gfx_str(uint8_t x, uint8_t y, const char *str, bool on);

/**
 * @brief Draw ASCII string with integer scaling
 * @param x      Start x
 * @param y      Start y
 * @param str    ASCII string
 * @param scale  Pixel scale factor, >= 1
 * @param on     true=lit, false=dark
 */
void lcd1602_gfx_str_scaled(uint8_t x, uint8_t y, const char *str, uint8_t scale, bool on);

/* ----------------------------------------------------------------
 * 天气图标
 * ---------------------------------------------------------------- */

/**
 * @brief 绘制 16x16 天气图标
 * @param x     左上角列坐标（0~112）
 * @param y     左上角行坐标（0~48）
 * @param icon  图标类型
 */
void lcd1602_gfx_weather_icon(uint8_t x, uint8_t y, weather_icon_t icon);

/**
 * @brief 根据天气文字（UTF-8）自动选择图标
 * @param text  和风天气 API 返回的天气文字，如 "晴"、"多云"
 * @return 对应的图标枚举值
 */
weather_icon_t lcd1602_gfx_text_to_icon(const char *text);

#ifdef __cplusplus
}
#endif
