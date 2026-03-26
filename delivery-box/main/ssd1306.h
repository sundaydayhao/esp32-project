/**
 * @file ssd1306.h
 * @brief SSD1306 OLED 显示屏驱动 (128x64, I2C)
 *
 * 提供基础绘图功能:
 *   - 像素操作
 *   - 文字绘制 (5x7 ASCII 字体)
 *   - 矩形/线条绘制
 *   - 帧缓冲刷新
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 显示屏尺寸
#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_PAGES   (SSD1306_HEIGHT / 8)

// 字体尺寸 (5x7 像素, 含1像素间距 = 6x8)
#define FONT_WIDTH      6
#define FONT_HEIGHT     8

// 每行最多字符数
#define CHARS_PER_LINE  (SSD1306_WIDTH / FONT_WIDTH)  // 21
// 最多行数
#define LINES_PER_SCREEN (SSD1306_HEIGHT / FONT_HEIGHT) // 8

/**
 * @brief 初始化 SSD1306
 * @return ESP_OK 成功
 */
esp_err_t ssd1306_init(void);

/**
 * @brief 清空帧缓冲 (不立即刷新到屏幕)
 */
void ssd1306_clear(void);

/**
 * @brief 将帧缓冲内容刷新到 OLED 屏幕
 */
void ssd1306_flush(void);

/**
 * @brief 清空并立即刷新 (清屏)
 */
void ssd1306_clear_screen(void);

/**
 * @brief 设置单个像素
 * @param x     列坐标 (0~127)
 * @param y     行坐标 (0~63)
 * @param color true=亮, false=灭
 */
void ssd1306_draw_pixel(uint8_t x, uint8_t y, bool color);

/**
 * @brief 绘制水平线
 * @param x     起始列
 * @param y     行坐标
 * @param width 宽度
 * @param color 颜色
 */
void ssd1306_draw_hline(uint8_t x, uint8_t y, uint8_t width, bool color);

/**
 * @brief 绘制垂直线
 * @param x      列坐标
 * @param y      起始行
 * @param height 高度
 * @param color  颜色
 */
void ssd1306_draw_vline(uint8_t x, uint8_t y, uint8_t height, bool color);

/**
 * @brief 绘制矩形边框
 * @param x      左上角列
 * @param y      左上角行
 * @param width  宽度
 * @param height 高度
 * @param color  颜色
 */
void ssd1306_draw_rect(uint8_t x, uint8_t y,
                        uint8_t width, uint8_t height, bool color);

/**
 * @brief 填充矩形
 * @param x      左上角列
 * @param y      左上角行
 * @param width  宽度
 * @param height 高度
 * @param color  颜色
 */
void ssd1306_fill_rect(uint8_t x, uint8_t y,
                        uint8_t width, uint8_t height, bool color);

/**
 * @brief 在指定位置绘制单个字符
 * @param x    列坐标 (像素)
 * @param y    行坐标 (像素)
 * @param ch   ASCII 字符
 * @param color 颜色
 */
void ssd1306_draw_char(uint8_t x, uint8_t y, char ch, bool color);

/**
 * @brief 在指定位置绘制字符串
 * @param x    列坐标 (像素)
 * @param y    行坐标 (像素)
 * @param str  字符串
 * @param color 颜色
 */
void ssd1306_draw_string(uint8_t x, uint8_t y, const char *str, bool color);

/**
 * @brief 在指定行绘制居中字符串
 * @param row   行号 (0~7)
 * @param str   字符串
 * @param color 颜色
 */
void ssd1306_draw_string_centered(uint8_t row, const char *str, bool color);

/**
 * @brief 设置显示对比度
 * @param contrast 对比度 (0~255)
 */
void ssd1306_set_contrast(uint8_t contrast);

/**
 * @brief 开关显示
 * @param on true=开, false=关
 */
void ssd1306_display_on(bool on);

#ifdef __cplusplus
}
#endif