/**
 * @file ssd1306.h
 * @brief SSD1306 OLED 显示屏驱动（I2C 接口，128x64）
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64

/**
 * @brief 初始化 SSD1306
 * @param i2c_num I2C 端口号
 * @param addr I2C 地址（0x3C 或 0x3D）
 * @return ESP_OK 成功
 */
esp_err_t ssd1306_init(int i2c_num, uint8_t addr);

/**
 * @brief 清屏
 */
void ssd1306_clear(void);

/**
 * @brief 刷新显示（将缓冲区写入屏幕）
 */
esp_err_t ssd1306_refresh(void);

/**
 * @brief 在指定位置绘制像素
 */
void ssd1306_draw_pixel(int x, int y, bool color);

/**
 * @brief 绘制水平线
 */
void ssd1306_draw_hline(int x, int y, int width, bool color);

/**
 * @brief 绘制垂直线
 */
void ssd1306_draw_vline(int x, int y, int height, bool color);

/**
 * @brief 绘制矩形（空心）
 */
void ssd1306_draw_rect(int x, int y, int width, int height, bool color);

/**
 * @brief 绘制填充矩形
 */
void ssd1306_fill_rect(int x, int y, int width, int height, bool color);

/**
 * @brief 在指定位置显示字符（8x8 字体）
 */
void ssd1306_draw_char(int x, int y, char c, bool color);

/**
 * @brief 在指定位置显示字符串
 */
void ssd1306_draw_string(int x, int y, const char *str, bool color);

/**
 * @brief 设置对比度（亮度）
 * @param contrast 0~255
 */
esp_err_t ssd1306_set_contrast(uint8_t contrast);

#ifdef __cplusplus
}
#endif