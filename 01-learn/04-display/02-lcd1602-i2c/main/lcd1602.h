/**
 * @file lcd1602.h
 * @brief LCD1602 液晶屏驱动（通过 PCF8574 I2C 扩展器）
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PCF8574 I2C 地址（A0/A1/A2 全接 GND 时为 0x27） */
#define LCD1602_I2C_ADDR    0x27

/**
 * @brief 初始化 LCD1602
 * @param i2c_num I2C 端口号
 * @param addr PCF8574 I2C 地址
 * @return ESP_OK 成功
 */
esp_err_t lcd1602_init(int i2c_num, uint8_t addr);

/**
 * @brief 清屏
 */
void lcd1602_clear(void);

/**
 * @brief 设置光标位置
 * @param col 列（0~15）
 * @param row 行（0~1）
 */
void lcd1602_set_cursor(uint8_t col, uint8_t row);

/**
 * @brief 打印字符串
 */
void lcd1602_print(const char *str);

/**
 * @brief 打印单个字符
 */
void lcd1602_write_char(char c);

/**
 * @brief 开关背光
 */
void lcd1602_backlight(bool on);

/**
 * @brief 开关光标显示
 */
void lcd1602_cursor(bool on);

/**
 * @brief 开关光标闪烁
 */
void lcd1602_blink(bool on);

#ifdef __cplusplus
}
#endif