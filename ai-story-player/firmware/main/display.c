/**
 * TFT 显示屏驱动模块
 * ST7789 240x240 SPI 显示屏
 *
 * 使用 ESP-IDF SPI Master 驱动
 * 字体渲染使用内置 8x8 点阵字体（ASCII）
 * 中文显示需要集成 LVGL 或 U8g2 库
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "config.h"
#include "display.h"

static const char *TAG = "DISPLAY";

// ST7789 命令
#define ST7789_NOP      0x00
#define ST7789_SWRESET  0x01
#define ST7789_SLPIN    0x10
#define ST7789_SLPOUT   0x11
#define ST7789_NORON    0x13
#define ST7789_INVOFF   0x20
#define ST7789_INVON    0x21
#define ST7789_DISPOFF  0x28
#define ST7789_DISPON   0x29
#define ST7789_CASET    0x2A
#define ST7789_RASET    0x2B
#define ST7789_RAMWR    0x2C
#define ST7789_MADCTL   0x36
#define ST7789_COLMOD   0x3A

// RGB565 颜色定义
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_YELLOW    0xFFE0
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_ORANGE    0xFD20
#define COLOR_PURPLE    0x8010
#define COLOR_PINK      0xFE19
#define COLOR_NAVY      0x000F
#define COLOR_DARKGREEN 0x03E0
#define COLOR_GOLD      0xFEA0

static spi_device_handle_t s_spi = NULL;

// 文字缓冲区（用于故事文字显示）
#define TEXT_BUF_SIZE   512
static char s_story_text[TEXT_BUF_SIZE] = {0};
static uint16_t s_text_y = 0;

/**
 * 发送命令
 */
static void st7789_cmd(uint8_t cmd) {
    gpio_set_level(TFT_DC_PIN, 0);  // DC=0: 命令
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(s_spi, &t);
}

/**
 * 发送数据
 */
static void st7789_data(const uint8_t *data, size_t len) {
    if (len == 0) return;
    gpio_set_level(TFT_DC_PIN, 1);  // DC=1: 数据
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void st7789_data_byte(uint8_t data) {
    st7789_data(&data, 1);
}

/**
 * 硬件复位
 */
static void st7789_reset(void) {
    gpio_set_level(TFT_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(TFT_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

/**
 * 初始化 ST7789
 */
static void st7789_init_sequence(void) {
    st7789_reset();

    st7789_cmd(ST7789_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    st7789_cmd(ST7789_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 颜色格式：16-bit RGB565
    st7789_cmd(ST7789_COLMOD);
    st7789_data_byte(0x55);

    // 内存访问控制
    st7789_cmd(ST7789_MADCTL);
    st7789_data_byte(0x00);

    // 反色（ST7789 通常需要）
    st7789_cmd(ST7789_INVON);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 正常显示模式
    st7789_cmd(ST7789_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 开启显示
    st7789_cmd(ST7789_DISPON);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "ST7789 初始化完成");
}

/**
 * 设置绘制窗口
 */
static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    uint8_t data[4];

    st7789_cmd(ST7789_CASET);
    data[0] = x0 >> 8; data[1] = x0 & 0xFF;
    data[2] = x1 >> 8; data[3] = x1 & 0xFF;
    st7789_data(data, 4);

    st7789_cmd(ST7789_RASET);
    data[0] = y0 >> 8; data[1] = y0 & 0xFF;
    data[2] = y1 >> 8; data[3] = y1 & 0xFF;
    st7789_data(data, 4);

    st7789_cmd(ST7789_RAMWR);
}

/**
 * 填充矩形区域
 */
static void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    set_window(x, y, x + w - 1, y + h - 1);

    // 准备颜色数据（大端字节序）
    uint8_t color_hi = color >> 8;
    uint8_t color_lo = color & 0xFF;

    // 使用 DMA 传输大块数据
    size_t pixels = (size_t)w * h;
    size_t buf_pixels = (pixels < 256) ? pixels : 256;
    uint8_t *buf = (uint8_t *)malloc(buf_pixels * 2);
    if (!buf) return;

    for (size_t i = 0; i < buf_pixels; i++) {
        buf[i * 2] = color_hi;
        buf[i * 2 + 1] = color_lo;
    }

    gpio_set_level(TFT_DC_PIN, 1);
    size_t remaining = pixels;
    while (remaining > 0) {
        size_t chunk = (remaining < buf_pixels) ? remaining : buf_pixels;
        spi_transaction_t t = {
            .length = chunk * 16,
            .tx_buffer = buf,
        };
        spi_device_polling_transmit(s_spi, &t);
        remaining -= chunk;
    }

    free(buf);
}

/**
 * 绘制像素
 */
static void draw_pixel(uint16_t x, uint16_t y, uint16_t color) {
    set_window(x, y, x, y);
    uint8_t data[2] = {color >> 8, color & 0xFF};
    st7789_data(data, 2);
}

/**
 * 简单的 8x8 ASCII 字体（部分字符）
 * 实际项目中应使用完整字体库
 */
static const uint8_t FONT_8X8[][8] = {
    // 空格 (0x20)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // ! (0x21)
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
    // 0-9 从索引 16 开始 (0x30-0x39)
};

/**
 * 绘制单个 ASCII 字符（8x8 像素）
 */
static void draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale) {
    // 简化实现：绘制字符边框
    // 实际项目中需要完整字体数据
    uint8_t char_w = 8 * scale;
    uint8_t char_h = 8 * scale;

    // 绘制字符背景
    fill_rect(x, y, char_w, char_h, bg);

    // 简单的字符识别（仅用于演示）
    if (c >= '0' && c <= '9') {
        // 绘制数字轮廓
        fill_rect(x + scale, y, char_w - 2 * scale, scale, fg);
        fill_rect(x + scale, y + char_h - scale, char_w - 2 * scale, scale, fg);
        fill_rect(x, y + scale, scale, char_h - 2 * scale, fg);
        fill_rect(x + char_w - scale, y + scale, scale, char_h - 2 * scale, fg);
    }
}

/**
 * 绘制字符串（仅 ASCII）
 */
static void draw_string_ascii(uint16_t x, uint16_t y, const char *str,
                               uint16_t fg, uint16_t bg, uint8_t scale) {
    uint16_t cx = x;
    while (*str) {
        if (cx + 8 * scale > TFT_WIDTH) {
            cx = x;
            y += 10 * scale;
        }
        draw_char(cx, y, *str, fg, bg, scale);
        cx += 8 * scale + scale;
        str++;
    }
}

/**
 * 绘制居中文字横条
 */
static void draw_centered_bar(uint16_t y, uint16_t height, const char *text,
                               uint16_t bg_color, uint16_t text_color) {
    fill_rect(0, y, TFT_WIDTH, height, bg_color);

    // 简单居中（ASCII 字符）
    size_t len = strlen(text);
    uint16_t text_w = len * 9;  // 8px + 1px 间距
    uint16_t x = (text_w < TFT_WIDTH) ? (TFT_WIDTH - text_w) / 2 : 0;
    draw_string_ascii(x, y + (height - 8) / 2, text, text_color, bg_color, 1);
}

/**
 * 绘制动画点（加载指示器）
 */
static void draw_loading_dots(uint16_t cx, uint16_t cy, uint8_t frame, uint16_t color) {
    uint16_t bg = COLOR_BLACK;
    for (int i = 0; i < 3; i++) {
        uint16_t x = cx - 20 + i * 20;
        uint16_t c = (i == frame % 3) ? color : bg;
        fill_rect(x - 5, cy - 5, 10, 10, c);
    }
}

esp_err_t display_init(void) {
    // 配置 GPIO
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TFT_DC_PIN) | (1ULL << TFT_RST_PIN),
    };
    gpio_config(&io_conf);

    // 配置背光（如果有）
    if (TFT_BL_PIN >= 0) {
        gpio_config_t bl_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << TFT_BL_PIN),
        };
        gpio_config(&bl_conf);
        gpio_set_level(TFT_BL_PIN, 1);  // 开启背光
    }

    // 配置 SPI 总线
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = TFT_MOSI_PIN,
        .miso_io_num = -1,
        .sclk_io_num = TFT_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * TFT_HEIGHT * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TFT_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // 添加 SPI 设备
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = TFT_SPI_FREQ,
        .mode = 0,
        .spics_io_num = TFT_CS_PIN,
        .queue_size = 7,
        .pre_cb = NULL,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(TFT_SPI_HOST, &dev_cfg, &s_spi));

    // 初始化 ST7789
    st7789_init_sequence();

    // 清屏为黑色
    display_clear(COLOR_BLACK);

    ESP_LOGI(TAG, "显示屏初始化完成 %dx%d", TFT_WIDTH, TFT_HEIGHT);
    return ESP_OK;
}

void display_clear(uint16_t color) {
    fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, color);
}

void display_show_boot_screen(void) {
    display_clear(COLOR_NAVY);

    // 绘制标题背景
    fill_rect(0, 60, TFT_WIDTH, 50, COLOR_PURPLE);

    // 绘制装饰星星
    for (int i = 0; i < 20; i++) {
        uint16_t sx = (i * 37 + 10) % TFT_WIDTH;
        uint16_t sy = (i * 23 + 5) % 55;
        fill_rect(sx, sy, 3, 3, COLOR_YELLOW);
    }

    // 月亮
    fill_rect(170, 15, 40, 40, COLOR_YELLOW);
    fill_rect(180, 10, 35, 35, COLOR_NAVY);  // 月牙效果

    // 底部信息
    fill_rect(0, TFT_HEIGHT - 30, TFT_WIDTH, 30, COLOR_DARKGREEN);

    ESP_LOGI(TAG, "显示开机画面");
}

void display_show_welcome(void) {
    display_clear(COLOR_NAVY);

    // 顶部装饰
    fill_rect(0, 0, TFT_WIDTH, 5, COLOR_GOLD);

    // 月亮
    fill_rect(90, 20, 60, 60, COLOR_YELLOW);
    fill_rect(100, 15, 55, 55, COLOR_NAVY);

    // 星星
    uint16_t stars[][2] = {{20,15},{50,30},{160,10},{190,25},{30,50},{200,45}};
    for (int i = 0; i < 6; i++) {
        fill_rect(stars[i][0], stars[i][1], 4, 4, COLOR_WHITE);
    }

    // 中间文字区域
    fill_rect(10, 100, TFT_WIDTH - 20, 80, COLOR_PURPLE);
    fill_rect(12, 102, TFT_WIDTH - 24, 76, COLOR_NAVY);

    // 底部按钮提示
    fill_rect(20, 195, TFT_WIDTH - 40, 35, COLOR_DARKGREEN);

    ESP_LOGI(TAG, "显示欢迎界面");
}

void display_show_listening(void) {
    display_clear(COLOR_BLACK);

    // 麦克风图标区域（简化）
    fill_rect(TFT_WIDTH/2 - 20, 60, 40, 60, COLOR_RED);
    fill_rect(TFT_WIDTH/2 - 30, 110, 60, 10, COLOR_RED);
    fill_rect(TFT_WIDTH/2 - 5, 120, 10, 20, COLOR_RED);
    fill_rect(TFT_WIDTH/2 - 20, 140, 40, 5, COLOR_RED);

    // 动画波形（静态显示）
    for (int i = 0; i < 5; i++) {
        uint16_t h = 10 + (i % 3) * 15;
        uint16_t x = TFT_WIDTH/2 - 50 + i * 25;
        fill_rect(x, 170 - h/2, 8, h, COLOR_GREEN);
    }

    // 底部提示
    fill_rect(0, TFT_HEIGHT - 40, TFT_WIDTH, 40, COLOR_DARKGREEN);

    ESP_LOGI(TAG, "显示聆听界面");
}

void display_show_processing(void) {
    display_clear(COLOR_BLACK);

    // 旋转动画（简化为静态）
    for (int i = 0; i < 8; i++) {
        uint16_t angle_x = TFT_WIDTH/2 + (int)(50 * (i % 4 < 2 ? 1 : -1));
        uint16_t angle_y = TFT_HEIGHT/2 + (int)(50 * (i < 4 ? 1 : -1));
        fill_rect(angle_x - 5, angle_y - 5, 10, 10, COLOR_CYAN);
    }

    // 中心
    fill_rect(TFT_WIDTH/2 - 15, TFT_HEIGHT/2 - 15, 30, 30, COLOR_PURPLE);

    // 底部提示
    fill_rect(0, TFT_HEIGHT - 40, TFT_WIDTH, 40, COLOR_NAVY);

    ESP_LOGI(TAG, "显示处理界面");
}

void display_show_status(const char *title, const char *subtitle, uint16_t color) {
    display_clear(COLOR_BLACK);

    // 顶部色条
    fill_rect(0, 0, TFT_WIDTH, 8, color);

    // 图标区域
    fill_rect(TFT_WIDTH/2 - 25, 50, 50, 50, color);

    // 标题区域
    fill_rect(0, 120, TFT_WIDTH, 40, COLOR_NAVY);

    // 副标题区域
    fill_rect(0, 165, TFT_WIDTH, 30, COLOR_BLACK);

    // 底部色条
    fill_rect(0, TFT_HEIGHT - 8, TFT_WIDTH, 8, color);

    ESP_LOGI(TAG, "显示状态: %s / %s", title, subtitle);
}

void display_show_error(const char *title, const char *message) {
    display_clear(COLOR_BLACK);

    // 红色警告条
    fill_rect(0, 0, TFT_WIDTH, 10, COLOR_RED);
    fill_rect(0, TFT_HEIGHT - 10, TFT_WIDTH, 10, COLOR_RED);

    // 错误图标（X）
    fill_rect(TFT_WIDTH/2 - 25, 40, 50, 50, COLOR_RED);
    fill_rect(TFT_WIDTH/2 - 15, 50, 30, 5, COLOR_WHITE);
    fill_rect(TFT_WIDTH/2 - 15, 75, 30, 5, COLOR_WHITE);

    // 错误信息区域
    fill_rect(10, 110, TFT_WIDTH - 20, 40, COLOR_RED);
    fill_rect(10, 155, TFT_WIDTH - 20, 30, COLOR_NAVY);

    ESP_LOGE(TAG, "显示错误: %s / %s", title, message);
}

void display_show_sleeping(void) {
    display_clear(COLOR_BLACK);

    // 渐变夜空效果（简化）
    for (int y = 0; y < TFT_HEIGHT; y += 10) {
        uint16_t b = (y * 3) & 0x1F;
        uint16_t color = b;  // 深蓝色渐变
        fill_rect(0, y, TFT_WIDTH, 10, color);
    }

    // 月亮
    fill_rect(80, 30, 80, 80, COLOR_YELLOW);
    fill_rect(95, 20, 75, 75, COLOR_BLACK);

    // 星星
    uint16_t stars[][2] = {{20,20},{50,10},{160,15},{200,30},{30,60},{210,60},{10,90}};
    for (int i = 0; i < 7; i++) {
        fill_rect(stars[i][0], stars[i][1], 3, 3, COLOR_WHITE);
    }

    // ZZZ 文字区域
    fill_rect(TFT_WIDTH/2 - 30, 140, 60, 30, COLOR_PURPLE);

    // 底部
    fill_rect(0, TFT_HEIGHT - 30, TFT_WIDTH, 30, COLOR_NAVY);

    ESP_LOGI(TAG, "显示睡眠界面");
}

void display_show_image(const uint8_t *rgb565_data, size_t data_len,
                         uint16_t width, uint16_t height) {
    if (!rgb565_data || data_len == 0) return;

    uint16_t x_offset = (TFT_WIDTH > width) ? (TFT_WIDTH - width) / 2 : 0;
    uint16_t y_offset = (TFT_HEIGHT > height) ? (TFT_HEIGHT - height) / 2 : 0;

    set_window(x_offset, y_offset,
               x_offset + width - 1, y_offset + height - 1);

    gpio_set_level(TFT_DC_PIN, 1);

    // 分块传输图像数据
    size_t chunk_size = 512;
    size_t offset = 0;
    while (offset < data_len) {
        size_t chunk = (data_len - offset < chunk_size) ?
                       (data_len - offset) : chunk_size;
        spi_transaction_t t = {
            .length = chunk * 8,
            .tx_buffer = rgb565_data + offset,
        };
        spi_device_polling_transmit(s_spi, &t);
        offset += chunk;
    }

    ESP_LOGI(TAG, "显示图像 %dx%d", width, height);
}

void display_show_story_text(const char *text) {
    if (!text) return;
    strncpy(s_story_text, text, TEXT_BUF_SIZE - 1);
    s_text_y = 0;

    // 清除文字区域（屏幕下半部分）
    fill_rect(0, TFT_HEIGHT / 2, TFT_WIDTH, TFT_HEIGHT / 2, COLOR_BLACK);

    // 绘制文字背景
    fill_rect(0, TFT_HEIGHT - 80, TFT_WIDTH, 80, COLOR_NAVY);

    ESP_LOGI(TAG, "显示故事文字");
}

void display_append_story_text(const char *text) {
    if (!text) return;
    // 追加文字到缓冲区
    size_t current_len = strlen(s_story_text);
    size_t append_len = strlen(text);
    if (current_len + append_len < TEXT_BUF_SIZE - 1) {
        strcat(s_story_text, text);
    }
    // 更新显示（简化：仅更新底部文字区域）
    fill_rect(0, TFT_HEIGHT - 80, TFT_WIDTH, 80, COLOR_NAVY);
}

void display_draw_text(uint16_t x, uint16_t y, const char *text,
                        uint16_t color, uint8_t size) {
    draw_string_ascii(x, y, text, color, COLOR_BLACK, size);
}

void display_draw_progress(uint16_t x, uint16_t y, uint16_t width,
                            uint16_t height, uint8_t percent, uint16_t color) {
    // 背景
    fill_rect(x, y, width, height, COLOR_NAVY);
    // 进度
    uint16_t progress_w = (width * percent) / 100;
    if (progress_w > 0) {
        fill_rect(x, y, progress_w, height, color);
    }
    // 边框
    fill_rect(x, y, width, 1, COLOR_WHITE);
    fill_rect(x, y + height - 1, width, 1, COLOR_WHITE);
    fill_rect(x, y, 1, height, COLOR_WHITE);
    fill_rect(x + width - 1, y, 1, height, COLOR_WHITE);
}

void display_show_keywords(const char **keywords, int count) {
    if (!keywords || count == 0) return;

    // 在屏幕顶部显示关键词标签
    fill_rect(0, 0, TFT_WIDTH, 30, COLOR_PURPLE);

    uint16_t x = 5;
    for (int i = 0; i < count && i < 3; i++) {
        uint16_t tag_w = strlen(keywords[i]) * 9 + 10;
        fill_rect(x, 5, tag_w, 20, COLOR_GOLD);
        x += tag_w + 5;
    }

    ESP_LOGI(TAG, "显示关键词标签，数量: %d", count);
}

void display_deinit(void) {
    if (s_spi) {
        spi_bus_remove_device(s_spi);
        spi_bus_free(TFT_SPI_HOST);
        s_spi = NULL;
    }
}