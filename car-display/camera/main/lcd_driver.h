#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RGB565 颜色定义 */
#define LCD_COLOR_BLACK     0x0000
#define LCD_COLOR_WHITE     0xFFFF
#define LCD_COLOR_RED       0xF800
#define LCD_COLOR_GREEN     0x07E0
#define LCD_COLOR_BLUE      0x001F
#define LCD_COLOR_YELLOW    0xFFE0
#define LCD_COLOR_ORANGE    0xFD20
#define LCD_COLOR_CYAN      0x07FF
#define LCD_COLOR_MAGENTA   0xF81F
#define LCD_COLOR_GRAY      0x8410
#define LCD_COLOR_DARKGRAY  0x4208
#define LCD_COLOR_LIGHTGRAY 0xC618

/* 半透明警告颜色 (用于叠加层) */
#define LCD_COLOR_WARN_GREEN  0x07E0  /* 安全 - 绿色 */
#define LCD_COLOR_WARN_YELLOW 0xFFE0  /* 注意 - 黄色 */
#define LCD_COLOR_WARN_RED    0xF800  /* 危险 - 红色 */

/**
 * @brief LCD 驱动配置
 */
typedef struct {
    int  pin_mosi;      /*!< SPI MOSI */
    int  pin_sclk;      /*!< SPI SCLK */
    int  pin_cs;        /*!< SPI CS */
    int  pin_dc;        /*!< 数据/命令选择 */
    int  pin_rst;       /*!< 复位 */
    int  pin_bl;        /*!< 背光控制 */
    int  width;         /*!< 屏幕宽度 */
    int  height;        /*!< 屏幕高度 */
    int  spi_clock_hz;  /*!< SPI 时钟频率 */
} lcd_config_t;

/**
 * @brief 矩形区域
 */
typedef struct {
    int x;
    int y;
    int w;
    int h;
} lcd_rect_t;

/**
 * @brief 初始化 LCD 驱动
 *
 * @param config LCD 配置
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t lcd_driver_init(const lcd_config_t *config);

/**
 * @brief 反初始化 LCD 驱动
 */
void lcd_driver_deinit(void);

/**
 * @brief 设置背光亮度
 *
 * @param brightness 亮度 0-255
 */
void lcd_set_backlight(uint8_t brightness);

/**
 * @brief 清屏
 *
 * @param color 填充颜色 (RGB565)
 */
void lcd_clear(uint16_t color);

/**
 * @brief 在指定区域绘制 RGB565 图像
 *
 * @param x      起始 X 坐标
 * @param y      起始 Y 坐标
 * @param w      宽度
 * @param h      高度
 * @param pixels RGB565 像素数据
 */
void lcd_draw_bitmap(int x, int y, int w, int h, const uint16_t *pixels);

/**
 * @brief 绘制缩放图像 (将 src_w x src_h 的图像缩放到 dst_w x dst_h)
 *
 * @param x      目标起始 X
 * @param y      目标起始 Y
 * @param dst_w  目标宽度
 * @param dst_h  目标高度
 * @param pixels 源图像像素 (RGB565)
 * @param src_w  源图像宽度
 * @param src_h  源图像高度
 */
void lcd_draw_bitmap_scaled(int x, int y, int dst_w, int dst_h,
                             const uint16_t *pixels, int src_w, int src_h);

/**
 * @brief 填充矩形
 *
 * @param x     起始 X
 * @param y     起始 Y
 * @param w     宽度
 * @param h     高度
 * @param color 颜色 (RGB565)
 */
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);

/**
 * @brief 绘制矩形边框
 *
 * @param x         起始 X
 * @param y         起始 Y
 * @param w         宽度
 * @param h         高度
 * @param thickness 边框厚度
 * @param color     颜色 (RGB565)
 */
void lcd_draw_rect(int x, int y, int w, int h, int thickness, uint16_t color);

/**
 * @brief 绘制水平线
 */
void lcd_draw_hline(int x, int y, int len, uint16_t color);

/**
 * @brief 绘制垂直线
 */
void lcd_draw_vline(int x, int y, int len, uint16_t color);

/**
 * @brief 绘制 ASCII 字符 (8x16 点阵)
 *
 * @param x    起始 X
 * @param y    起始 Y
 * @param ch   ASCII 字符
 * @param fg   前景色
 * @param bg   背景色
 * @param scale 缩放倍数 (1=8x16, 2=16x32)
 */
void lcd_draw_char(int x, int y, char ch, uint16_t fg, uint16_t bg, int scale);

/**
 * @brief 绘制 ASCII 字符串
 *
 * @param x     起始 X
 * @param y     起始 Y
 * @param str   字符串
 * @param fg    前景色
 * @param bg    背景色
 * @param scale 缩放倍数
 */
void lcd_draw_string(int x, int y, const char *str, uint16_t fg, uint16_t bg, int scale);

/**
 * @brief 绘制半透明颜色叠加层
 *
 * @param x      起始 X
 * @param y      起始 Y
 * @param w      宽度
 * @param h      高度
 * @param color  叠加颜色 (RGB565)
 * @param alpha  透明度 0-255 (0=完全透明, 255=完全不透明)
 */
void lcd_draw_overlay(int x, int y, int w, int h, uint16_t color, uint8_t alpha);

/**
 * @brief 获取屏幕宽度
 */
int lcd_get_width(void);

/**
 * @brief 获取屏幕高度
 */
int lcd_get_height(void);

#ifdef __cplusplus
}
#endif