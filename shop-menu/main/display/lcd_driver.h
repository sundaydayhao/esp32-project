#pragma once

#include <stdint.h>
#include <stddef.h>
#include "config.h"

// ============================================================
// ILI9488 命令定义
// ============================================================
#define ILI9488_CMD_SWRESET         0x01
#define ILI9488_CMD_SLPOUT          0x11
#define ILI9488_CMD_DISPON          0x29
#define ILI9488_CMD_CASET           0x2A
#define ILI9488_CMD_PASET           0x2B
#define ILI9488_CMD_RAMWR           0x2C
#define ILI9488_CMD_MADCTL          0x36
#define ILI9488_CMD_COLMOD          0x3A
#define ILI9488_CMD_PGAMCTRL        0xE0
#define ILI9488_CMD_NGAMCTRL        0xE1
#define ILI9488_CMD_PWCTRL1         0xC0
#define ILI9488_CMD_PWCTRL2         0xC1
#define ILI9488_CMD_VMCTRL1         0xC5

// MADCTL 方向标志
#define MADCTL_MY   0x80
#define MADCTL_MX   0x40
#define MADCTL_MV   0x20
#define MADCTL_ML   0x10
#define MADCTL_BGR  0x08

// ============================================================
// 颜色宏（RGB565）
// ============================================================
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_ORANGE    0xFD20
#define COLOR_GRAY      0x8410
#define COLOR_LGRAY     0xC618
#define COLOR_DGRAY     0x4208

// RGB 转 RGB565
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

// ============================================================
// 函数声明
// ============================================================

/**
 * @brief 初始化 LCD（SPI + ILI9488 初始化序列）
 */
void lcd_init(void);

/**
 * @brief 设置绘制窗口
 */
void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief 填充矩形区域（单色）
 */
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief 绘制单个像素
 */
void lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief 绘制水平线
 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color);

/**
 * @brief 绘制垂直线
 */
void lcd_draw_vline(uint16_t x, uint16_t y, uint16_t len, uint16_t color);

/**
 * @brief 绘制矩形边框
 */
void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief 将像素缓冲区写入屏幕（DMA 传输）
 * @param x, y 起始坐标
 * @param w, h 宽高
 * @param data RGB565 像素数据
 */
void lcd_draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data);

/**
 * @brief 绘制 ASCII 字符（8×16 点阵）
 */
void lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);

/**
 * @brief 绘制 ASCII 字符串
 */
void lcd_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t scale);

/**
 * @brief 绘制 UTF-8 中文字符（16×16 点阵，需要字库）
 * @note 使用内嵌的 GB2312 常用汉字字库
 */
void lcd_draw_utf8(uint16_t x, uint16_t y, const char *utf8_str,
                   uint16_t fg, uint16_t bg, uint8_t scale);

/**
 * @brief 设置背光亮度（0~255）
 */
void lcd_set_backlight(uint8_t brightness);

/**
 * @brief 清屏
 */
void lcd_clear(uint16_t color);