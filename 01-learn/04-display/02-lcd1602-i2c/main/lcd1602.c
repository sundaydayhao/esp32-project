/**
 * @file lcd1602.c
 * @brief LCD1602 液晶屏驱动实现（PCF8574 I2C 扩展器）
 *
 * PCF8574 引脚与 LCD1602 连接：
 *   P0 → RS（寄存器选择）
 *   P1 → RW（读写选择，接 GND 只写）
 *   P2 → EN（使能）
 *   P3 → BL（背光）
 *   P4 → D4
 *   P5 → D5
 *   P6 → D6
 *   P7 → D7
 */

#include "lcd1602.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

static const char *TAG = "LCD1602";

/* PCF8574 引脚定义 */
#define LCD_RS  0x01
#define LCD_RW  0x02
#define LCD_EN  0x04
#define LCD_BL  0x08
#define LCD_D4  0x10
#define LCD_D5  0x20
#define LCD_D6  0x40
#define LCD_D7  0x80

/* LCD 命令 */
#define LCD_CMD_CLEAR       0x01
#define LCD_CMD_HOME        0x02
#define LCD_CMD_ENTRY_MODE  0x06
#define LCD_CMD_DISPLAY_ON  0x0C
#define LCD_CMD_FUNCTION_4BIT 0x28

static int s_i2c_num;
static uint8_t s_i2c_addr;
static uint8_t s_backlight = LCD_BL;

/**
 * @brief 通过 PCF8574 写一字节到 I2C
 */
static esp_err_t pcf8574_write(uint8_t data)
{
    return i2c_master_write_to_device(s_i2c_num, s_i2c_addr, &data, 1,
                                       pdMS_TO_TICKS(100));
}

/**
 * @brief 产生 EN 脉冲（锁存数据）
 */
static void lcd_pulse_enable(uint8_t data)
{
    pcf8574_write(data | LCD_EN | s_backlight);
    ets_delay_us(1);
    pcf8574_write((data & ~LCD_EN) | s_backlight);
    ets_delay_us(50);
}

/**
 * @brief 发送 4 位数据
 */
static void lcd_write_nibble(uint8_t nibble, uint8_t rs)
{
    uint8_t data = (nibble << 4) | rs | s_backlight;
    lcd_pulse_enable(data);
}

/**
 * @brief 发送一字节（命令或数据）
 */
static void lcd_write_byte(uint8_t byte, uint8_t rs)
{
    lcd_write_nibble(byte >> 4, rs);   // 高 4 位
    lcd_write_nibble(byte & 0x0F, rs); // 低 4 位
}

esp_err_t lcd1602_init(int i2c_num, uint8_t addr)
{
    s_i2c_num = i2c_num;
    s_i2c_addr = addr;

    vTaskDelay(pdMS_TO_TICKS(50));  // 等待电源稳定

    /* 初始化为 4 位模式（需要发送 3 次 0x03） */
    lcd_write_nibble(0x03, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(0x03, 0);
    ets_delay_us(150);
    lcd_write_nibble(0x03, 0);
    ets_delay_us(150);
    lcd_write_nibble(0x02, 0);  // 切换到 4 位模式

    /* 发送初始化命令 */
    lcd_write_byte(LCD_CMD_FUNCTION_4BIT, 0);  // 4位，2行，5x8字体
    lcd_write_byte(LCD_CMD_DISPLAY_ON, 0);     // 开显示，关光标
    lcd_write_byte(LCD_CMD_CLEAR, 0);          // 清屏
    vTaskDelay(pdMS_TO_TICKS(2));
    lcd_write_byte(LCD_CMD_ENTRY_MODE, 0);     // 光标右移

    ESP_LOGI(TAG, "LCD1602 初始化完成，地址: 0x%02X", addr);
    return ESP_OK;
}

void lcd1602_clear(void)
{
    lcd_write_byte(LCD_CMD_CLEAR, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
}

void lcd1602_set_cursor(uint8_t col, uint8_t row)
{
    uint8_t row_offsets[] = {0x00, 0x40};
    lcd_write_byte(0x80 | (col + row_offsets[row & 0x01]), 0);
}

void lcd1602_write_char(char c)
{
    lcd_write_byte(c, LCD_RS);
}

void lcd1602_print(const char *str)
{
    while (*str) {
        lcd1602_write_char(*str++);
    }
}

void lcd1602_backlight(bool on)
{
    s_backlight = on ? LCD_BL : 0;
    pcf8574_write(s_backlight);
}

void lcd1602_cursor(bool on)
{
    if (on) {
        lcd_write_byte(0x0E, 0);  // 显示开，光标开
    } else {
        lcd_write_byte(0x0C, 0);  // 显示开，光标关
    }
}

void lcd1602_blink(bool on)
{
    if (on) {
        lcd_write_byte(0x0F, 0);  // 显示开，光标开，闪烁开
    } else {
        lcd_write_byte(0x0C, 0);  // 显示开，光标关，闪烁关
    }
}