/**
 * @file display.c
 * @brief ILI9341 TFT LCD 显示驱动实现（240x320，SPI接口）
 */

#include "display.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "DISPLAY";

/* ==================== ILI9341 命令定义 ==================== */
#define ILI9341_SWRESET     0x01
#define ILI9341_SLPOUT      0x11
#define ILI9341_NORON       0x13
#define ILI9341_INVOFF      0x20
#define ILI9341_DISPON      0x29
#define ILI9341_CASET       0x2A
#define ILI9341_PASET       0x2B
#define ILI9341_RAMWR       0x2C
#define ILI9341_MADCTL      0x36
#define ILI9341_PIXFMT      0x3A
#define ILI9341_FRMCTR1     0xB1
#define ILI9341_DFUNCTR     0xB6
#define ILI9341_PWCTR1      0xC0
#define ILI9341_PWCTR2      0xC1
#define ILI9341_VMCTR1      0xC5
#define ILI9341_VMCTR2      0xC7
#define ILI9341_GMCTRP1     0xE0
#define ILI9341_GMCTRN1     0xE1
#define MADCTL_MX           0x40
#define MADCTL_BGR          0x08

/* ==================== SPI 配置 ==================== */
#define LCD_SPI_HOST        SPI2_HOST
#define LCD_SPI_FREQ_HZ     (40 * 1000 * 1000)  // 40MHz
#define LCD_DMA_CHAN        1

/* ==================== 背光 PWM 配置 ==================== */
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_8_BIT
#define LEDC_FREQUENCY      5000

/* ==================== 5x7 ASCII 字体数据 ==================== */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 0x20 ' '
    {0x00,0x00,0x5F,0x00,0x00}, // 0x21 '!'
    {0x00,0x07,0x00,0x07,0x00}, // 0x22 '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // 0x23 '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // 0x24 '$'
    {0x23,0x13,0x08,0x64,0x62}, // 0x25 '%'
    {0x36,0x49,0x55,0x22,0x50}, // 0x26 '&'
    {0x00,0x05,0x03,0x00,0x00}, // 0x27 '\''
    {0x00,0x1C,0x22,0x41,0x00}, // 0x28 '('
    {0x00,0x41,0x22,0x1C,0x00}, // 0x29 ')'
    {0x08,0x2A,0x1C,0x2A,0x08}, // 0x2A '*'
    {0x08,0x08,0x3E,0x08,0x08}, // 0x2B '+'
    {0x00,0x50,0x30,0x00,0x00}, // 0x2C ','
    {0x08,0x08,0x08,0x08,0x08}, // 0x2D '-'
    {0x00,0x60,0x60,0x00,0x00}, // 0x2E '.'
    {0x20,0x10,0x08,0x04,0x02}, // 0x2F '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // 0x30 '0'
    {0x00,0x42,0x7F,0x40,0x00}, // 0x31 '1'
    {0x42,0x61,0x51,0x49,0x46}, // 0x32 '2'
    {0x21,0x41,0x45,0x4B,0x31}, // 0x33 '3'
    {0x18,0x14,0x12,0x7F,0x10}, // 0x34 '4'
    {0x27,0x45,0x45,0x45,0x39}, // 0x35 '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // 0x36 '6'
    {0x01,0x71,0x09,0x05,0x03}, // 0x37 '7'
    {0x36,0x49,0x49,0x49,0x36}, // 0x38 '8'
    {0x06,0x49,0x49,0x29,0x1E}, // 0x39 '9'
    {0x00,0x36,0x36,0x00,0x00}, // 0x3A ':'
    {0x00,0x56,0x36,0x00,0x00}, // 0x3B ';'
    {0x00,0x08,0x14,0x22,0x41}, // 0x3C '<'
    {0x14,0x14,0x14,0x14,0x14}, // 0x3D '='
    {0x41,0x22,0x14,0x08,0x00}, // 0x3E '>'
    {0x02,0x01,0x51,0x09,0x06}, // 0x3F '?'
    {0x32,0x49,0x79,0x41,0x3E}, // 0x40 '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 0x41 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 0x42 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 0x43 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 0x44 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 0x45 'E'
    {0x7F,0x09,0x09,0x01,0x01}, // 0x46 'F'
    {0x3E,0x41,0x41,0x51,0x32}, // 0x47 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 0x48 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 0x49 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 0x4A 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 0x4B 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 0x4C 'L'
    {0x7F,0x02,0x04,0x02,0x7F}, // 0x4D 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 0x4E 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 0x4F 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 0x50 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 0x51 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 0x52 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 0x53 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 0x54 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 0x55 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 0x56 'V'
    {0x7F,0x20,0x18,0x20,0x7F}, // 0x57 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 0x58 'X'
    {0x03,0x04,0x78,0x04,0x03}, // 0x59 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 0x5A 'Z'
    {0x00,0x00,0x7F,0x41,0x41}, // 0x5B '['
    {0x02,0x04,0x08,0x10,0x20}, // 0x5C '\'
    {0x41,0x41,0x7F,0x00,0x00}, // 0x5D ']'
    {0x04,0x02,0x01,0x02,0x04}, // 0x5E '^'
    {0x40,0x40,0x40,0x40,0x40}, // 0x5F '_'
    {0x00,0x01,0x02,0x04,0x00}, // 0x60 '`'
    {0x20,0x54,0x54,0x54,0x78}, // 0x61 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 0x62 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 0x63 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 0x64 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 0x65 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 0x66 'f'
    {0x08,0x14,0x54,0x54,0x3C}, // 0x67 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 0x68 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 0x69 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 0x6A 'j'
    {0x00,0x7F,0x10,0x28,0x44}, // 0x6B 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 0x6C 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 0x6D 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 0x6E 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 0x6F 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 0x70 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 0x71 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 0x72 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 0x73 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 0x74 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 0x75 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 0x76 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 0x77 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 0x78 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 0x79 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 0x7A 'z'
    {0x00,0x08,0x36,0x41,0x00}, // 0x7B '{'
    {0x00,0x00,0x7F,0x00,0x00}, // 0x7C '|'
    {0x00,0x41,0x36,0x08,0x00}, // 0x7D '}'
    {0x08,0x08,0x2A,0x1C,0x08}, // 0x7E '~'
    {0x08,0x1C,0x2A,0x08,0x08}, // 0x7F DEL
};

/* ==================== 全局变量 ==================== */
static spi_device_handle_t s_spi = NULL;
static bool s_initialized = false;

/* ==================== SPI 底层操作 ==================== */

static void lcd_cmd(uint8_t cmd)
{
    gpio_set_level(CONFIG_LCD_DC_PIN, 0); /* 命令模式 */
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void lcd_data(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    gpio_set_level(CONFIG_LCD_DC_PIN, 1); /* 数据模式 */
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void lcd_data_byte(uint8_t data)
{
    lcd_data(&data, 1);
}

static void lcd_set_window(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    uint8_t buf[4];

    lcd_cmd(ILI9341_CASET);
    buf[0] = x0 >> 8; buf[1] = x0 & 0xFF;
    buf[2] = x1 >> 8; buf[3] = x1 & 0xFF;
    lcd_data(buf, 4);

    lcd_cmd(ILI9341_PASET);
    buf[0] = y0 >> 8; buf[1] = y0 & 0xFF;
    buf[2] = y1 >> 8; buf[3] = y1 & 0xFF;
    lcd_data(buf, 4);

    lcd_cmd(ILI9341_RAMWR);
}

/* ==================== 初始化 ==================== */

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "初始化 ILI9341 TFT LCD");

    /* 配置 GPIO */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_LCD_DC_PIN) |
                        (1ULL << CONFIG_LCD_RST_PIN) |
                        (1ULL << CONFIG_LCD_CS_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* 配置 SPI 总线 */
    spi_bus_config_t buscfg = {
        .mosi_io_num = CONFIG_LCD_SPI_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = CONFIG_LCD_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2,
    };
    esp_err_t ret = spi_bus_initialize(LCD_SPI_HOST, &buscfg, LCD_DMA_CHAN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI 总线初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 添加 SPI 设备 */
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LCD_SPI_FREQ_HZ,
        .mode = 0,
        .spics_io_num = CONFIG_LCD_CS_PIN,
        .queue_size = 7,
    };
    ret = spi_bus_add_device(LCD_SPI_HOST, &devcfg, &s_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI 设备添加失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 配置背光 PWM */
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = CONFIG_LCD_BL_PIN,
        .duty = 200,
        .hpoint = 0,
    };
    ledc_channel_config(&ledc_channel);

    /* 硬件复位 */
    gpio_set_level(CONFIG_LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(CONFIG_LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* ILI9341 初始化序列 */
    lcd_cmd(ILI9341_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    lcd_cmd(0xCF);
    lcd_data_byte(0x00); lcd_data_byte(0xC1); lcd_data_byte(0x30);

    lcd_cmd(0xED);
    lcd_data_byte(0x64); lcd_data_byte(0x03);
    lcd_data_byte(0x12); lcd_data_byte(0x81);

    lcd_cmd(0xE8);
    lcd_data_byte(0x85); lcd_data_byte(0x00); lcd_data_byte(0x78);

    lcd_cmd(0xCB);
    lcd_data_byte(0x39); lcd_data_byte(0x2C); lcd_data_byte(0x00);
    lcd_data_byte(0x34); lcd_data_byte(0x02);

    lcd_cmd(0xF7); lcd_data_byte(0x20);
    lcd_cmd(0xEA); lcd_data_byte(0x00); lcd_data_byte(0x00);

    lcd_cmd(ILI9341_PWCTR1); lcd_data_byte(0x23);
    lcd_cmd(ILI9341_PWCTR2); lcd_data_byte(0x10);

    lcd_cmd(ILI9341_VMCTR1);
    lcd_data_byte(0x3E); lcd_data_byte(0x28);
    lcd_cmd(ILI9341_VMCTR2); lcd_data_byte(0x86);

    lcd_cmd(ILI9341_MADCTL);
    lcd_data_byte(MADCTL_MX | MADCTL_BGR); /* 横屏，BGR颜色顺序 */

    lcd_cmd(ILI9341_PIXFMT); lcd_data_byte(0x55); /* 16位色 */

    lcd_cmd(ILI9341_FRMCTR1);
    lcd_data_byte(0x00); lcd_data_byte(0x18);

    lcd_cmd(ILI9341_DFUNCTR);
    lcd_data_byte(0x08); lcd_data_byte(0x82); lcd_data_byte(0x27);

    lcd_cmd(0xF2); lcd_data_byte(0x00);
    lcd_cmd(0x26); lcd_data_byte(0x01);

    lcd_cmd(ILI9341_GMCTRP1);
    lcd_data_byte(0x0F); lcd_data_byte(0x31); lcd_data_byte(0x2B);
    lcd_data_byte(0x0C); lcd_data_byte(0x0E); lcd_data_byte(0x08);
    lcd_data_byte(0x4E); lcd_data_byte(0xF1); lcd_data_byte(0x37);
    lcd_data_byte(0x07); lcd_data_byte(0x10); lcd_data_byte(0x03);
    lcd_data_byte(0x0E); lcd_data_byte(0x09); lcd_data_byte(0x00);

    lcd_cmd(ILI9341_GMCTRN1);
    lcd_data_byte(0x00); lcd_data_byte(0x0E); lcd_data_byte(0x14);
    lcd_data_byte(0x03); lcd_data_byte(0x11); lcd_data_byte(0x07);
    lcd_data_byte(0x31); lcd_data_byte(0xC1); lcd_data_byte(0x48);
    lcd_data_byte(0x08); lcd_data_byte(0x0F); lcd_data_byte(0x0C);
    lcd_data_byte(0x31); lcd_data_byte(0x36); lcd_data_byte(0x0F);

    lcd_cmd(ILI9341_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(ILI9341_DISPON);

    s_initialized = true;
    ESP_LOGI(TAG, "ILI9341 初始化完成");

    display_clear(COLOR_BLACK);
    return ESP_OK;
}

/* ==================== 基础绘图函数 ==================== */

void display_clear(uint16_t color)
{
    display_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (!s_initialized) return;
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (x + w > LCD_WIDTH)  w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

    lcd_set_window(x, y, x + w - 1, y + h - 1);

    /* 准备颜色数据（大端序） */
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;

    /* 分批发送，避免栈溢出 */
    const int CHUNK = 128;
    uint8_t buf[CHUNK * 2];
    for (int i = 0; i < CHUNK; i++) {
        buf[i * 2]     = hi;
        buf[i * 2 + 1] = lo;
    }

    int total = w * h;
    gpio_set_level(CONFIG_LCD_DC_PIN, 1);
    while (total > 0) {
        int send = (total > CHUNK) ? CHUNK : total;
        spi_transaction_t t = {
            .length = send * 16,
            .tx_buffer = buf,
        };
        spi_device_polling_transmit(s_spi, &t);
        total -= send;
    }
}

void display_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    display_fill_rect(x, y, w, 1, color);
}

void display_draw_char(int16_t x, int16_t y, char c,
                       uint16_t fg, uint16_t bg, uint8_t size)
{
    if (!s_initialized) return;
    if (c < 0x20 || c > 0x7F) c = '?';

    const uint8_t *glyph = font5x7[c - 0x20];
    int char_w = 5 * size + size; /* 5列 + 1列间距 */
    int char_h = 7 * size + size; /* 7行 + 1行间距 */

    for (int col = 0; col < 5; col++) {
        uint8_t line = glyph[col];
        for (int row = 0; row < 7; row++) {
            uint16_t color = (line & (1 << row)) ? fg : bg;
            display_fill_rect(x + col * size, y + row * size, size, size, color);
        }
    }
    /* 右侧间距列 */
    display_fill_rect(x + 5 * size, y, size, char_h, bg);
    (void)char_w;
}

void display_draw_string(int16_t x, int16_t y, const char *str,
                         uint16_t fg, uint16_t bg, uint8_t size)
{
    if (!str) return;
    int char_w = (5 + 1) * size;
    while (*str) {
        display_draw_char(x, y, *str, fg, bg, size);
        x += char_w;
        str++;
        if (x + char_w > LCD_WIDTH) break;
    }
}

void display_draw_string_centered(int16_t y, const char *str,
                                  uint16_t fg, uint16_t bg, uint8_t size)
{
    if (!str) return;
    int len = strlen(str);
    int char_w = (5 + 1) * size;
    int total_w = len * char_w;
    int16_t x = (LCD_WIDTH - total_w) / 2;
    if (x < 0) x = 0;
    display_draw_string(x, y, str, fg, bg, size);
}

/* ==================== 界面显示函数 ==================== */

void display_show_splash(void)
{
    display_clear(COLOR_NAVY);

    /* 标题 */
    display_draw_string_centered(60,  "Medication",  COLOR_WHITE, COLOR_NAVY, 3);
    display_draw_string_centered(100, "Reminder",    COLOR_WHITE, COLOR_NAVY, 3);
    display_draw_string_centered(150, "v1.0",        COLOR_YELLOW, COLOR_NAVY, 2);
    display_draw_string_centered(200, "Starting...", COLOR_LIGHT_GRAY, COLOR_NAVY, 1);
}

void display_show_wifi_status(bool connecting, const char *ssid)
{
    display_clear(COLOR_NAVY);
    display_draw_string_centered(80,  "WiFi",       COLOR_WHITE, COLOR_NAVY, 3);
    if (connecting) {
        display_draw_string_centered(130, "Connecting", COLOR_YELLOW, COLOR_NAVY, 2);
        if (ssid) {
            display_draw_string_centered(170, ssid, COLOR_CYAN, COLOR_NAVY, 1);
        }
    } else {
        display_draw_string_centered(130, "Connected!", COLOR_GREEN, COLOR_NAVY, 2);
    }
}

void display_show_standby(const char *time_str, const char *date_str,
                          const char *weekday_str,
                          const reminder_t *next_reminder)
{
    /* 背景 */
    display_clear(COLOR_BG_STANDBY);

    /* 日期行 */
    char date_line[48];
    snprintf(date_line, sizeof(date_line), "%s %s", date_str, weekday_str);
    display_draw_string_centered(10, date_line, COLOR_CYAN, COLOR_BG_STANDBY, 2);

    /* 大字时间 */
    display_draw_string_centered(55, time_str, COLOR_WHITE, COLOR_BG_STANDBY, 4);

    /* 分隔线 */
    display_draw_hline(10, 120, LCD_WIDTH - 20, COLOR_DARK_GRAY);

    /* 下次提醒信息 */
    if (next_reminder) {
        char next_time[16];
        snprintf(next_time, sizeof(next_time), "Next: %02d:%02d",
                 next_reminder->config.hour, next_reminder->config.minute);
        display_draw_string_centered(135, next_time, COLOR_YELLOW, COLOR_BG_STANDBY, 2);
        display_draw_string_centered(170, next_reminder->config.medication_name,
                                     COLOR_WHITE, COLOR_BG_STANDBY, 2);
        display_draw_string_centered(200, next_reminder->config.dosage,
                                     COLOR_LIGHT_GRAY, COLOR_BG_STANDBY, 2);
    } else {
        display_draw_string_centered(150, "No Reminder", COLOR_DARK_GRAY, COLOR_BG_STANDBY, 2);
        display_draw_string_centered(185, "Today", COLOR_DARK_GRAY, COLOR_BG_STANDBY, 2);
    }

    /* 底部提示 */
    display_draw_string_centered(290, "Hold BTN1 3s = Config",
                                 COLOR_DARK_GRAY, COLOR_BG_STANDBY, 1);
}

void display_show_alert(const reminder_t *reminder, bool escalated)
{
    uint16_t bg = escalated ? COLOR_RED : COLOR_BG_ALERT;
    display_clear(bg);

    /* 警告标题 */
    display_draw_string_centered(15, "! TAKE MEDICINE !", COLOR_YELLOW, bg, 2);
    display_draw_hline(5, 45, LCD_WIDTH - 10, COLOR_YELLOW);

    /* 药品名称（大字） */
    display_draw_string_centered(60, reminder->config.medication_name,
                                 COLOR_WHITE, bg, 3);

    /* 剂量 */
    display_draw_string_centered(110, reminder->config.dosage,
                                 COLOR_YELLOW, bg, 3);

    /* 提醒时间 */
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d",
             reminder->config.hour, reminder->config.minute);
    display_draw_string_centered(160, time_str, COLOR_CYAN, bg, 3);

    display_draw_hline(5, 210, LCD_WIDTH - 10, COLOR_YELLOW);

    /* 操作提示 */
    if (escalated) {
        display_draw_string_centered(225, "!! URGENT !!", COLOR_WHITE, bg, 2);
    }
    display_draw_string_centered(255, "Press BTN1", COLOR_WHITE, bg, 2);
    display_draw_string_centered(280, "to Confirm", COLOR_WHITE, bg, 2);
}

void display_show_confirmed(const char *medication_name)
{
    display_clear(COLOR_BG_CONFIRM);

    /* 大勾 */
    display_draw_string_centered(60, "OK", COLOR_WHITE, COLOR_BG_CONFIRM, 5);

    display_draw_hline(20, 130, LCD_WIDTH - 40, COLOR_GREEN);

    /* 确认信息 */
    display_draw_string_centered(150, "Taken!", COLOR_WHITE, COLOR_BG_CONFIRM, 3);
    if (medication_name) {
        display_draw_string_centered(195, medication_name,
                                     COLOR_YELLOW, COLOR_BG_CONFIRM, 2);
    }
    display_draw_string_centered(240, "Take care!", COLOR_CYAN, COLOR_BG_CONFIRM, 2);
    display_draw_string_centered(270, "Stay healthy!", COLOR_CYAN, COLOR_BG_CONFIRM, 1);
}

void display_show_history(const char *records, int page)
{
    display_clear(COLOR_BLACK);
    display_draw_string_centered(5, "History", COLOR_CYAN, COLOR_BLACK, 2);
    display_draw_hline(5, 25, LCD_WIDTH - 10, COLOR_DARK_GRAY);

    if (!records || strlen(records) == 0) {
        display_draw_string_centered(140, "No records", COLOR_DARK_GRAY, COLOR_BLACK, 2);
        return;
    }

    /* 简单显示记录文本 */
    int y = 35;
    const char *p = records;
    char line[40];
    int line_idx = 0;
    int skip = page * 8; /* 每页8行 */

    while (*p && y < 300) {
        /* 找到换行符或字符串结束 */
        const char *end = p;
        while (*end && *end != '\n') end++;

        int len = end - p;
        if (len >= (int)sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';

        if (line_idx >= skip) {
            display_draw_string(5, y, line, COLOR_WHITE, COLOR_BLACK, 1);
            y += 12;
        }
        line_idx++;

        p = (*end == '\n') ? end + 1 : end;
    }

    /* 页码 */
    char page_str[16];
    snprintf(page_str, sizeof(page_str), "Page %d", page + 1);
    display_draw_string_centered(305, page_str, COLOR_DARK_GRAY, COLOR_BLACK, 1);
}

void display_set_backlight(uint8_t brightness)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, brightness);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}