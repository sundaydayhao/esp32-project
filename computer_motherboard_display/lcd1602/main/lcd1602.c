/**
 * @file lcd1602.c
 * @brief ESP32 LCD1602（HD44780兼容）8位并行接口驱动实现
 *
 * 这是一个面向标准 LCD1602 字符屏的简化驱动：
 * - 仅支持文本模式
 * - 16列 x 2行
 * - 8位并行总线
 * - RW 固定接地，始终写模式
 *
 * 初始化时序参考 HD44780 兼容器件的常规流程：
 * 1. 上电等待
 * 2. 功能设置：8位、2行、5x8 字符
 * 3. 显示开/关配置
 * 4. 清屏
 * 5. 输入模式设置
 */

#include "lcd1602.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "lcd1602";

/* LCD1602 常用指令 */
#define LCD_CMD_CLEAR           0x01
#define LCD_CMD_HOME            0x02
#define LCD_CMD_ENTRY_MODE      0x06
#define LCD_CMD_DISPLAY_OFF     0x08
#define LCD_CMD_DISPLAY_ON      0x0C
#define LCD_CMD_CURSOR_ON       0x0E
#define LCD_CMD_CURSOR_BLINK    0x0F
#define LCD_CMD_FUNCTION_SET    0x38  /* 8-bit, 2-line, 5x8 dots */
#define LCD_CMD_SET_DDRAM_ADDR  0x80

static const uint8_t s_row_addr[LCD_ROWS] = {
    0x00, /* row 0 */
    0x40, /* row 1 */
};

static esp_err_t validate_output_gpio(int gpio_num, const char *name)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio_num)) {
        ESP_LOGE(TAG, "%s uses invalid output GPIO: %d", name, gpio_num);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t validate_pin_config(void)
{
    static const int pins[] = {
        LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
        LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7,
        LCD_PIN_RS, LCD_PIN_E,
    };
    static const char *names[] = {
        "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
        "RS", "E",
    };

    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        esp_err_t ret = validate_output_gpio(pins[i], names[i]);
        if (ret != ESP_OK) {
            return ret;
        }
        for (size_t j = i + 1; j < sizeof(pins) / sizeof(pins[0]); ++j) {
            if (pins[i] == pins[j]) {
                ESP_LOGE(TAG, "GPIO %d is assigned to both %s and %s",
                         pins[i], names[i], names[j]);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    return ESP_OK;
}

static inline void set_data_bus(uint8_t byte)
{
    gpio_set_level(LCD_PIN_D0, (byte >> 0) & 1U);
    gpio_set_level(LCD_PIN_D1, (byte >> 1) & 1U);
    gpio_set_level(LCD_PIN_D2, (byte >> 2) & 1U);
    gpio_set_level(LCD_PIN_D3, (byte >> 3) & 1U);
    gpio_set_level(LCD_PIN_D4, (byte >> 4) & 1U);
    gpio_set_level(LCD_PIN_D5, (byte >> 5) & 1U);
    gpio_set_level(LCD_PIN_D6, (byte >> 6) & 1U);
    gpio_set_level(LCD_PIN_D7, (byte >> 7) & 1U);
}

static inline void pulse_enable(void)
{
    gpio_set_level(LCD_PIN_E, 1);
    esp_rom_delay_us(1);
    gpio_set_level(LCD_PIN_E, 0);
    esp_rom_delay_us(50);
}

static void lcd_write(uint8_t value, bool is_data)
{
    gpio_set_level(LCD_PIN_RS, is_data ? 1 : 0);
    set_data_bus(value);
    pulse_enable();
}

static void lcd_write_cmd(uint8_t cmd)
{
    lcd_write(cmd, false);
}

static void lcd_write_data(uint8_t data)
{
    lcd_write(data, true);
}

esp_err_t lcd1602_init(void)
{
    esp_err_t ret = validate_pin_config();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Initializing LCD1602 (HD44780 compatible, 8-bit parallel)");

    uint64_t pin_mask = 0;
    for (int i = 0; i < 8; i++) {
        pin_mask |= (1ULL << (uint32_t[]){LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
                                          LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7}[i]);
    }
    pin_mask |= (1ULL << LCD_PIN_RS);
    pin_mask |= (1ULL << LCD_PIN_E);

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    gpio_set_level(LCD_PIN_RS, 0);
    gpio_set_level(LCD_PIN_E, 0);
    set_data_bus(0x00);

    vTaskDelay(pdMS_TO_TICKS(50));

    /* Function set x3 as per HD44780 init sequence */
    lcd_write_cmd(0x38);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_cmd(0x38);
    esp_rom_delay_us(150);
    lcd_write_cmd(0x38);

    /* Display OFF */
    lcd_write_cmd(LCD_CMD_DISPLAY_OFF);

    /* Clear */
    lcd_write_cmd(LCD_CMD_CLEAR);
    vTaskDelay(pdMS_TO_TICKS(2));

    /* Entry mode */
    lcd_write_cmd(LCD_CMD_ENTRY_MODE);

    /* Display ON, cursor OFF, blink OFF */
    lcd_write_cmd(LCD_CMD_DISPLAY_ON);

    ESP_LOGI(TAG, "LCD1602 initialized OK");
    return ESP_OK;
}

void lcd1602_clear(void)
{
    lcd_write_cmd(LCD_CMD_CLEAR);
    vTaskDelay(pdMS_TO_TICKS(2));
}

void lcd1602_home(void)
{
    lcd_write_cmd(LCD_CMD_HOME);
    vTaskDelay(pdMS_TO_TICKS(2));
}

void lcd1602_set_cursor(uint8_t row, uint8_t col)
{
    if (row >= LCD_ROWS) {
        row = LCD_ROWS - 1;
    }
    if (col >= LCD_COLS) {
        col = LCD_COLS - 1;
    }

    lcd_write_cmd(LCD_CMD_SET_DDRAM_ADDR | (s_row_addr[row] + col));
}

void lcd1602_write_char(char c)
{
    lcd_write_data((uint8_t)c);
}

void lcd1602_write_str(const char *str)
{
    if (!str) {
        return;
    }

    while (*str) {
        lcd1602_write_char(*str++);
    }
}

void lcd1602_write_at(uint8_t row, uint8_t col, const char *str)
{
    lcd1602_set_cursor(row, col);
    lcd1602_write_str(str);
}

void lcd1602_display_on(bool on)
{
    lcd_write_cmd(on ? LCD_CMD_DISPLAY_ON : LCD_CMD_DISPLAY_OFF);
}

void lcd1602_cursor_on(bool on)
{
    if (on) {
        lcd_write_cmd(LCD_CMD_CURSOR_ON);
    } else {
        lcd_write_cmd(LCD_CMD_DISPLAY_ON);
    }
}

void lcd1602_blink_on(bool on)
{
    if (on) {
        lcd_write_cmd(LCD_CMD_CURSOR_BLINK);
    } else {
        lcd_write_cmd(LCD_CMD_DISPLAY_ON);
    }
}