/**
 * @file lcd12864.c
 * @brief LCD12864 (ST7920) 8位并行接口驱动实现
 *
 * 并行接口写时序（RW 接 GND，始终写模式）：
 *   1. 设置 RS（0=命令，1=数据）
 *   2. 设置 D0~D7 数据
 *   3. E 拉高，延时 ≥ 450ns
 *   4. E 拉低，延时 ≥ 10ns（数据锁存）
 *   5. 等待 ST7920 执行完成（延时或查忙标志）
 *
 * 注意：
 *   - RW 引脚直接接 GND，不通过 GPIO 控制
 *   - PSB 引脚接 VCC（5V），选择并行模式
 *   - ESP32 3.3V 输出可直接驱动 5V TTL 输入（VIH_min ≈ 2.0V）
 *
 * 文本模式 DDRAM 地址映射（ST7920）：
 *   行1: 0x80~0x87   行2: 0x90~0x97
 *   行3: 0x88~0x8F   行4: 0x98~0x9F
 *
 * 图形模式 GDRAM 地址映射：
 *   上半屏(y=0~31):  行地址=y,    列地址=0~7
 *   下半屏(y=32~63): 行地址=y-32, 列地址=8~15
 */

#include "lcd12864.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "lcd12864";

/* ----------------------------------------------------------------
 * 数据引脚数组（D0~D7）
 * ---------------------------------------------------------------- */
static const int s_data_pins[8] = {
    LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
    LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7
};

/* ----------------------------------------------------------------
 * 控制引脚宏
 * ---------------------------------------------------------------- */
#define RS_CMD()    gpio_set_level(LCD_PIN_RS,  0)  /* 命令模式 */
#define RS_DATA()   gpio_set_level(LCD_PIN_RS,  1)  /* 数据模式 */
#define E_HIGH()    gpio_set_level(LCD_PIN_E,   1)
#define E_LOW()     gpio_set_level(LCD_PIN_E,   0)
#define RST_HIGH()  gpio_set_level(LCD_PIN_RST, 1)
#define RST_LOW()   gpio_set_level(LCD_PIN_RST, 0)

/* ST7920 基本指令集 */
#define CMD_CLEAR           0x01    /* 清屏（执行时间约 1.6ms） */
#define CMD_HOME            0x02    /* 归位 */
#define CMD_ENTRY_MODE      0x06    /* 进入模式：光标右移，不移位 */
#define CMD_DISPLAY_ON      0x0C    /* 显示开，光标关，闪烁关 */
#define CMD_DISPLAY_OFF     0x08    /* 显示关 */
#define CMD_FUNC_BASIC      0x30    /* 基本指令集，8位并行 */
#define CMD_FUNC_EXTEND     0x34    /* 扩展指令集，8位并行 */
#define CMD_FUNC_GRAPHIC    0x36    /* 扩展指令集 + 图形显示开 */

/* DDRAM 行首地址 */
static const uint8_t s_row_addr[4] = {0x80, 0x90, 0x88, 0x98};

/* 图形缓冲区 128x64/8 = 1024 字节 */
static uint8_t s_gram[1024];

/* 当前模式 */
static bool s_graphic_mode = false;

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
        LCD_PIN_RS, LCD_PIN_E, LCD_PIN_RST,
    };
    static const char *names[] = {
        "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
        "RS", "E", "RST",
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

/* ----------------------------------------------------------------
 * 底层并行写操作
 * ---------------------------------------------------------------- */

/**
 * @brief 将一个字节写到 D0~D7
 */
static inline void set_data_bus(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        gpio_set_level(s_data_pins[i], (byte >> i) & 0x01);
    }
}

/**
 * @brief 发送命令（RS=0）
 * @param cmd  命令字节
 */
static void lcd_write_cmd(uint8_t cmd)
{
    RS_CMD();
    set_data_bus(cmd);
    E_HIGH();
    esp_rom_delay_us(1);    /* E 高电平保持 ≥ 450ns */
    E_LOW();
    esp_rom_delay_us(72);   /* 等待 ST7920 执行（大多数命令 < 72us） */
}

/**
 * @brief 发送数据（RS=1）
 * @param data  数据字节
 */
static void lcd_write_data(uint8_t data)
{
    RS_DATA();
    set_data_bus(data);
    E_HIGH();
    esp_rom_delay_us(1);
    E_LOW();
    esp_rom_delay_us(72);
}

/* ----------------------------------------------------------------
 * 公共 API 实现
 * ---------------------------------------------------------------- */

esp_err_t lcd12864_init(void)
{
    esp_err_t ret = validate_pin_config();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Initializing LCD12864 (ST7920, 8-bit parallel)");
    ESP_LOGI(TAG, "Pins: D0-D7=[%d,%d,%d,%d,%d,%d,%d,%d] RS=%d E=%d RST=%d",
             LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
             LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7,
             LCD_PIN_RS, LCD_PIN_E, LCD_PIN_RST);

    /* 配置所有 GPIO 为输出 */
    uint64_t pin_mask = 0;
    for (int i = 0; i < 8; i++) {
        pin_mask |= (1ULL << s_data_pins[i]);
    }
    pin_mask |= (1ULL << LCD_PIN_RS);
    pin_mask |= (1ULL << LCD_PIN_E);
    pin_mask |= (1ULL << LCD_PIN_RST);

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 初始状态：E 低，RS 低，数据总线清零 */
    E_LOW();
    RS_CMD();
    set_data_bus(0x00);

    /* 硬件复位 */
    RST_LOW();
    vTaskDelay(pdMS_TO_TICKS(20));
    RST_HIGH();
    vTaskDelay(pdMS_TO_TICKS(50));

    /* ST7920 初始化序列（8位并行模式）
     * 上电后需要等待 ≥ 40ms，然后发送功能设置命令 */
    lcd_write_cmd(CMD_FUNC_BASIC);      /* 功能设置：8位，基本指令集 */
    vTaskDelay(pdMS_TO_TICKS(2));
    lcd_write_cmd(CMD_FUNC_BASIC);      /* 再次发送确认 */
    vTaskDelay(pdMS_TO_TICKS(2));
    lcd_write_cmd(CMD_DISPLAY_ON);      /* 显示开，光标关，闪烁关 */
    vTaskDelay(pdMS_TO_TICKS(2));
    lcd_write_cmd(CMD_CLEAR);           /* 清屏 */
    vTaskDelay(pdMS_TO_TICKS(10));      /* 清屏命令需要约 1.6ms */
    lcd_write_cmd(CMD_ENTRY_MODE);      /* 进入模式：光标右移 */
    vTaskDelay(pdMS_TO_TICKS(2));

    s_graphic_mode = false;
    memset(s_gram, 0, sizeof(s_gram));

    ESP_LOGI(TAG, "LCD12864 initialized OK");
    return ESP_OK;
}

void lcd12864_clear(void)
{
    if (s_graphic_mode) {
        memset(s_gram, 0, sizeof(s_gram));
        lcd12864_flush_bitmap(s_gram);
    } else {
        lcd_write_cmd(CMD_CLEAR);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void lcd12864_set_cursor(uint8_t row, uint8_t col)
{
    if (row > 3) row = 3;
    if (col > 7) col = 7;
    lcd_write_cmd(s_row_addr[row] + col);
}

void lcd12864_print(const char *str)
{
    if (!str) return;
    while (*str) {
        lcd_write_data((uint8_t)*str);
        str++;
    }
}

void lcd12864_print_at(uint8_t row, uint8_t col, const char *str)
{
    lcd12864_set_cursor(row, col);
    lcd12864_print(str);
}

void lcd12864_display_on(bool on)
{
    lcd_write_cmd(on ? CMD_DISPLAY_ON : CMD_DISPLAY_OFF);
}

void lcd12864_enter_graphic_mode(void)
{
    lcd_write_cmd(CMD_FUNC_EXTEND);     /* 切换到扩展指令集 */
    esp_rom_delay_us(200);
    lcd_write_cmd(CMD_FUNC_GRAPHIC);    /* 开启图形显示 */
    esp_rom_delay_us(200);
    s_graphic_mode = true;
}

void lcd12864_enter_text_mode(void)
{
    lcd_write_cmd(CMD_FUNC_BASIC);      /* 切回基本指令集 */
    esp_rom_delay_us(200);
    lcd_write_cmd(CMD_DISPLAY_ON);
    esp_rom_delay_us(200);
    s_graphic_mode = false;
}

void lcd12864_draw_pixel(uint8_t x, uint8_t y, bool on)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;

    uint16_t byte_idx = (uint16_t)y * 16 + (x / 8);
    uint8_t  bit_mask = 0x80 >> (x % 8);

    if (on) {
        s_gram[byte_idx] |= bit_mask;
    } else {
        s_gram[byte_idx] &= ~bit_mask;
    }
}

void lcd12864_flush_bitmap(const uint8_t *buf)
{
    /*
     * ST7920 GDRAM 地址映射（8位并行模式与串行模式相同）：
     *   上半屏 y=0~31:  行地址=y,      列地址=0~7
     *   下半屏 y=32~63: 行地址=y-32,   列地址=8~15
     *
     * 写入顺序：先设置行地址，再设置列地址，然后连续写 16 字节
     */
    for (uint8_t y = 0; y < 64; y++) {
        uint8_t row_addr, col_start;
        if (y < 32) {
            row_addr  = y;
            col_start = 0;
        } else {
            row_addr  = y - 32;
            col_start = 8;
        }

        lcd_write_cmd(0x80 | row_addr);     /* 设置行地址 */
        lcd_write_cmd(0x80 | col_start);    /* 设置列地址 */

        const uint8_t *row_data = buf + (uint16_t)y * 16;
        for (uint8_t x = 0; x < 16; x++) {
            lcd_write_data(row_data[x]);
        }
    }
}
