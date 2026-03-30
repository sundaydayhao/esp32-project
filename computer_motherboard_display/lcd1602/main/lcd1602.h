/**
 * @file lcd1602.h
 * @brief ESP32 LCD1602（HD44780兼容）驱动头文件
 *
 * 说明：
 * - 适用于标准 LCD1602 字符屏（16列 x 2行）
 * - 支持 8位并行模式
 * - 若你的模块只有 4位接线，也可以在后续扩展为 4位模式
 *
 * 默认引脚（可在 Kconfig 中修改）：
 *   ESP32 GPIO12 -> LCD D0
 *   ESP32 GPIO13 -> LCD D1
 *   ESP32 GPIO14 -> LCD D2
 *   ESP32 GPIO15 -> LCD D3
 *   ESP32 GPIO16 -> LCD D4
 *   ESP32 GPIO17 -> LCD D5
 *   ESP32 GPIO18 -> LCD D6
 *   ESP32 GPIO19 -> LCD D7
 *   ESP32 GPIO21 -> LCD RS
 *   ESP32 GPIO22 -> LCD E
 *   GND          -> LCD RW（始终接 GND，写模式）
 *   GND          -> LCD VSS
 *   5V           -> LCD VDD
 *   V0           -> 对比度电位器中间端
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_LCD_PIN_D0
#define LCD_PIN_D0   CONFIG_LCD_PIN_D0
#else
#define LCD_PIN_D0   12
#endif
#ifdef CONFIG_LCD_PIN_D1
#define LCD_PIN_D1   CONFIG_LCD_PIN_D1
#else
#define LCD_PIN_D1   13
#endif
#ifdef CONFIG_LCD_PIN_D2
#define LCD_PIN_D2   CONFIG_LCD_PIN_D2
#else
#define LCD_PIN_D2   14
#endif
#ifdef CONFIG_LCD_PIN_D3
#define LCD_PIN_D3   CONFIG_LCD_PIN_D3
#else
#define LCD_PIN_D3   15
#endif
#ifdef CONFIG_LCD_PIN_D4
#define LCD_PIN_D4   CONFIG_LCD_PIN_D4
#else
#define LCD_PIN_D4   16
#endif
#ifdef CONFIG_LCD_PIN_D5
#define LCD_PIN_D5   CONFIG_LCD_PIN_D5
#else
#define LCD_PIN_D5   17
#endif
#ifdef CONFIG_LCD_PIN_D6
#define LCD_PIN_D6   CONFIG_LCD_PIN_D6
#else
#define LCD_PIN_D6   18
#endif
#ifdef CONFIG_LCD_PIN_D7
#define LCD_PIN_D7   CONFIG_LCD_PIN_D7
#else
#define LCD_PIN_D7   19
#endif
#ifdef CONFIG_LCD_PIN_RS
#define LCD_PIN_RS   CONFIG_LCD_PIN_RS
#else
#define LCD_PIN_RS   21
#endif
#ifdef CONFIG_LCD_PIN_E
#define LCD_PIN_E    CONFIG_LCD_PIN_E
#else
#define LCD_PIN_E    22
#endif

#define LCD_COLS 16
#define LCD_ROWS 2

esp_err_t lcd1602_init(void);
void lcd1602_clear(void);
void lcd1602_home(void);
void lcd1602_set_cursor(uint8_t row, uint8_t col);
void lcd1602_write_char(char c);
void lcd1602_write_str(const char *str);
void lcd1602_write_at(uint8_t row, uint8_t col, const char *str);
void lcd1602_display_on(bool on);
void lcd1602_cursor_on(bool on);
void lcd1602_blink_on(bool on);

#ifdef __cplusplus
}
#endif