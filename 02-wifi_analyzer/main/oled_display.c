/**
 * oled_display.c
 *
 * SSD1306 OLED 驱动（I2C，ESP-IDF 原生 driver/i2c.h）
 * 分辨率：128×64，页寻址模式
 *
 * 内置 5×7 ASCII 字体（字符 0x20~0x7E）
 * 提供：清屏、画点、画线、填充矩形、绘制文本、绘制信号条
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"

#include "oled_display.h"

static const char *TAG = "OLED";

/* ------------------------------------------------------------------ */
/*  5×7 ASCII 字体（0x20 空格 ~ 0x7E 波浪号）                          */
/*  每个字符 5 字节，每字节代表一列（bit0=顶行，bit6=底行）             */
/* ------------------------------------------------------------------ */
static const uint8_t FONT5X7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 0x20   */
    {0x00,0x00,0x5F,0x00,0x00}, /* 0x21 ! */
    {0x00,0x07,0x00,0x07,0x00}, /* 0x22 " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* 0x23 # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 0x24 $ */
    {0x23,0x13,0x08,0x64,0x62}, /* 0x25 % */
    {0x36,0x49,0x55,0x22,0x50}, /* 0x26 & */
    {0x00,0x05,0x03,0x00,0x00}, /* 0x27 ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* 0x28 ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* 0x29 ) */
    {0x14,0x08,0x3E,0x08,0x14}, /* 0x2A * */
    {0x08,0x08,0x3E,0x08,0x08}, /* 0x2B + */
    {0x00,0x50,0x30,0x00,0x00}, /* 0x2C , */
    {0x08,0x08,0x08,0x08,0x08}, /* 0x2D - */
    {0x00,0x60,0x60,0x00,0x00}, /* 0x2E . */
    {0x20,0x10,0x08,0x04,0x02}, /* 0x2F / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0x30 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 0x31 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 0x32 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 0x33 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 0x34 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 0x35 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 0x36 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 0x37 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 0x38 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 0x39 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* 0x3A : */
    {0x00,0x56,0x36,0x00,0x00}, /* 0x3B ; */
    {0x08,0x14,0x22,0x41,0x00}, /* 0x3C < */
    {0x14,0x14,0x14,0x14,0x14}, /* 0x3D = */
    {0x00,0x41,0x22,0x14,0x08}, /* 0x3E > */
    {0x02,0x01,0x51,0x09,0x06}, /* 0x3F ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* 0x40 @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 0x41 A */
    {0x7F,0x49,0x49,0x49,0x36}, /* 0x42 B */
    {0x3E,0x41,0x41,0x41,0x22}, /* 0x43 C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 0x44 D */
    {0x7F,0x49,0x49,0x49,0x41}, /* 0x45 E */
    {0x7F,0x09,0x09,0x09,0x01}, /* 0x46 F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 0x47 G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 0x48 H */
    {0x00,0x41,0x7F,0x41,0x00}, /* 0x49 I */
    {0x20,0x40,0x41,0x3F,0x01}, /* 0x4A J */
    {0x7F,0x08,0x14,0x22,0x41}, /* 0x4B K */
    {0x7F,0x40,0x40,0x40,0x40}, /* 0x4C L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 0x4D M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 0x4E N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 0x4F O */
    {0x7F,0x09,0x09,0x09,0x06}, /* 0x50 P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 0x51 Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* 0x52 R */
    {0x46,0x49,0x49,0x49,0x31}, /* 0x53 S */
    {0x01,0x01,0x7F,0x01,0x01}, /* 0x54 T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 0x55 U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 0x56 V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 0x57 W */
    {0x63,0x14,0x08,0x14,0x63}, /* 0x58 X */
    {0x07,0x08,0x70,0x08,0x07}, /* 0x59 Y */
    {0x61,0x51,0x49,0x45,0x43}, /* 0x5A Z */
    {0x00,0x7F,0x41,0x41,0x00}, /* 0x5B [ */
    {0x02,0x04,0x08,0x10,0x20}, /* 0x5C \ */
    {0x00,0x41,0x41,0x7F,0x00}, /* 0x5D ] */
    {0x04,0x02,0x01,0x02,0x04}, /* 0x5E ^ */
    {0x40,0x40,0x40,0x40,0x40}, /* 0x5F _ */
    {0x00,0x01,0x02,0x04,0x00}, /* 0x60 ` */
    {0x20,0x54,0x54,0x54,0x78}, /* 0x61 a */
    {0x7F,0x48,0x44,0x44,0x38}, /* 0x62 b */
    {0x38,0x44,0x44,0x44,0x20}, /* 0x63 c */
    {0x38,0x44,0x44,0x48,0x7F}, /* 0x64 d */
    {0x38,0x54,0x54,0x54,0x18}, /* 0x65 e */
    {0x08,0x7E,0x09,0x01,0x02}, /* 0x66 f */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 0x67 g */
    {0x7F,0x08,0x04,0x04,0x78}, /* 0x68 h */
    {0x00,0x44,0x7D,0x40,0x00}, /* 0x69 i */
    {0x20,0x40,0x44,0x3D,0x00}, /* 0x6A j */
    {0x7F,0x10,0x28,0x44,0x00}, /* 0x6B k */
    {0x00,0x41,0x7F,0x40,0x00}, /* 0x6C l */
    {0x7C,0x04,0x18,0x04,0x78}, /* 0x6D m */
    {0x7C,0x08,0x04,0x04,0x78}, /* 0x6E n */
    {0x38,0x44,0x44,0x44,0x38}, /* 0x6F o */
    {0x7C,0x14,0x14,0x14,0x08}, /* 0x70 p */
    {0x08,0x14,0x14,0x18,0x7C}, /* 0x71 q */
    {0x7C,0x08,0x04,0x04,0x08}, /* 0x72 r */
    {0x48,0x54,0x54,0x54,0x20}, /* 0x73 s */
    {0x04,0x3F,0x44,0x40,0x20}, /* 0x74 t */
    {0x3C,0x40,0x40,0x20,0x7C}, /* 0x75 u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 0x76 v */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 0x77 w */
    {0x44,0x28,0x10,0x28,0x44}, /* 0x78 x */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 0x79 y */
    {0x44,0x64,0x54,0x4C,0x44}, /* 0x7A z */
    {0x00,0x08,0x36,0x41,0x00}, /* 0x7B { */
    {0x00,0x00,0x7F,0x00,0x00}, /* 0x7C | */
    {0x00,0x41,0x36,0x08,0x00}, /* 0x7D } */
    {0x10,0x08,0x08,0x10,0x08}, /* 0x7E ~ */
};

/* ------------------------------------------------------------------ */
/*  帧缓冲区（128×64 / 8 = 1024 字节）                                 */
/* ------------------------------------------------------------------ */
static uint8_t s_framebuf[OLED_WIDTH * OLED_PAGES];

/* 当前显示页（每页4个网络） */
static bool s_oled_ready = false;
static int s_display_page = 0;

/* 最近一次扫描结果（用于翻页时重绘） */
static wifi_scan_result_t s_last_result;
static bool s_has_result = false;

/* ------------------------------------------------------------------ */
/*  SSD1306 底层 I2C 写函数                                             */
/* ------------------------------------------------------------------ */

/** 发送单条命令 */
static esp_err_t ssd1306_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};  /* 0x00 = Co=0, D/C#=0 (命令) */
    return i2c_master_write_to_device(OLED_I2C_PORT, OLED_I2C_ADDR,
                                      buf, sizeof(buf),
                                      pdMS_TO_TICKS(10));
}

/** 发送数据块（帧缓冲区内容） */
static esp_err_t ssd1306_data(const uint8_t *data, size_t len)
{
    /* ESP-IDF i2c_master_write_to_device 不支持前缀字节+大块数据的一次发送，
     * 需要手动构造 I2C 事务 */
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (OLED_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x40, true);  /* 0x40 = Co=0, D/C#=1 (数据) */
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(OLED_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

/* ------------------------------------------------------------------ */
/*  SSD1306 初始化序列                                                  */
/* ------------------------------------------------------------------ */
static esp_err_t ssd1306_init_sequence(void)
{
    const uint8_t cmds[] = {
        0xAE,       /* 关闭显示 */
        0xD5, 0x80, /* 设置时钟分频 */
        0xA8, 0x3F, /* 多路复用比：63（128×64） */
        0xD3, 0x00, /* 显示偏移：0 */
        0x40,       /* 起始行：0 */
        0x8D, 0x14, /* 开启电荷泵 */
        0x20, 0x00, /* 水平寻址模式 */
        0xA1,       /* 列地址映射：反转（SEG127→COL0） */
        0xC8,       /* COM 扫描方向：反向 */
        0xDA, 0x12, /* COM 引脚配置 */
        0x81, 0xCF, /* 对比度：207 */
        0xD9, 0xF1, /* 预充电周期 */
        0xDB, 0x40, /* VCOMH 电压 */
        0xA4,       /* 全局显示开启（跟随 RAM） */
        0xA6,       /* 正常显示（非反色） */
        0xAF,       /* 开启显示 */
    };
    for (size_t i = 0; i < sizeof(cmds); i++) {
        esp_err_t err = ssd1306_cmd(cmds[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  将帧缓冲区刷新到 OLED                                               */
/* ------------------------------------------------------------------ */
static void ssd1306_flush(void)
{
    /* 设置列地址范围：0~127 */
    ssd1306_cmd(0x21);
    ssd1306_cmd(0x00);
    ssd1306_cmd(0x7F);
    /* 设置页地址范围：0~7 */
    ssd1306_cmd(0x22);
    ssd1306_cmd(0x00);
    ssd1306_cmd(0x07);
    /* 发送全部帧缓冲区 */
    ssd1306_data(s_framebuf, sizeof(s_framebuf));
}

/* ------------------------------------------------------------------ */
/*  帧缓冲区操作函数                                                    */
/* ------------------------------------------------------------------ */

static void fb_clear(void)
{
    memset(s_framebuf, 0, sizeof(s_framebuf));
}

/** 设置单个像素（x: 0~127, y: 0~63） */
static void fb_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) return;
    int page = y / 8;
    int bit  = y % 8;
    int idx  = page * OLED_WIDTH + x;
    if (on) {
        s_framebuf[idx] |=  (1 << bit);
    } else {
        s_framebuf[idx] &= ~(1 << bit);
    }
}

/** 画水平线 */
static void fb_hline(int x, int y, int w)
{
    for (int i = 0; i < w; i++) fb_set_pixel(x + i, y, true);
}

/** 填充矩形 */
static void fb_fill_rect(int x, int y, int w, int h)
{
    for (int row = y; row < y + h; row++)
        for (int col = x; col < x + w; col++)
            fb_set_pixel(col, row, true);
}

/** 空心矩形 */
static void fb_draw_rect(int x, int y, int w, int h)
{
    fb_hline(x,         y,         w);
    fb_hline(x,         y + h - 1, w);
    for (int row = y; row < y + h; row++) {
        fb_set_pixel(x,         row, true);
        fb_set_pixel(x + w - 1, row, true);
    }
}

/** 绘制单个字符（5×7），返回下一个字符的 x 坐标 */
static int fb_draw_char(int x, int y, char c)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *glyph = FONT5X7[c - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                fb_set_pixel(x + col, y + row, true);
            }
        }
    }
    return x + 6;  /* 5像素字符 + 1像素间距 */
}

/** 绘制字符串，超出屏幕宽度自动截断 */
static void fb_draw_str(int x, int y, const char *str)
{
    while (*str && x < OLED_WIDTH) {
        x = fb_draw_char(x, y, *str++);
    }
}

/** 格式化字符串后绘制（类似 printf） */
static void fb_draw_printf(int x, int y, const char *fmt, ...)
{
    char buf[32];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fb_draw_str(x, y, buf);
}

/* ------------------------------------------------------------------ */
/*  绘制信号强度条（x,y 为左下角，bars: 1~4）                          */
/* ------------------------------------------------------------------ */
static void fb_draw_signal_bars(int x, int y, int bars)
{
    for (int i = 0; i < 4; i++) {
        int bw = 4;
        int bh = (i + 1) * 3;
        int bx = x + i * 6;
        int by = y - bh + 1;
        if (i < bars) {
            fb_fill_rect(bx, by, bw, bh);
        } else {
            fb_draw_rect(bx, by, bw, bh);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  oled_display_init                                                   */
/* ------------------------------------------------------------------ */
esp_err_t oled_display_init(void)
{
    s_oled_ready = false;

    if (!GPIO_IS_VALID_OUTPUT_GPIO(OLED_SDA_GPIO) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(OLED_SCL_GPIO)) {
        ESP_LOGE(TAG,
                 "Invalid I2C pins for ESP32-S3: SDA=%d SCL=%d. Update OLED_SDA_GPIO/OLED_SCL_GPIO in oled_display.h.",
                 OLED_SDA_GPIO, OLED_SCL_GPIO);
        return ESP_ERR_INVALID_ARG;
    }

    /* 配置 I2C 主机 */
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = OLED_SDA_GPIO,
        .scl_io_num       = OLED_SCL_GPIO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(OLED_I2C_PORT, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s (SDA=%d SCL=%d)",
                 esp_err_to_name(err), OLED_SDA_GPIO, OLED_SCL_GPIO);
        return err;
    }

    err = i2c_driver_install(OLED_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 等待 OLED 上电稳定 */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 发送初始化序列 */
    err = ssd1306_init_sequence();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SSD1306 初始化失败 (err=%s)，检查接线和 I2C 地址(0x%02X)",
                 esp_err_to_name(err), OLED_I2C_ADDR);
        i2c_driver_delete(OLED_I2C_PORT);
        return ESP_FAIL;
    }

    /* 清屏 */
    fb_clear();
    ssd1306_flush();

    s_oled_ready = true;
    ESP_LOGI(TAG, "SSD1306 OLED 初始化成功");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  oled_display_show_boot                                              */
/* ------------------------------------------------------------------ */
void oled_display_show_boot(const char *ap_ip)
{
    if (!s_oled_ready) return;

    fb_clear();
    fb_draw_str(10, 0,  "WiFi Analyzer");
    fb_hline(0, 9, OLED_WIDTH);
    fb_draw_str(0,  14, "Connect to:");
    fb_draw_str(0,  24, WIFI_AP_SSID);
    fb_draw_str(0,  34, "Pass:" WIFI_AP_PASSWORD);
    fb_draw_str(0,  46, "http://");
    fb_draw_str(42, 46, ap_ip ? ap_ip : "192.168.4.1");
    ssd1306_flush();
}

/* ------------------------------------------------------------------ */
/*  oled_display_update                                                 */
/* ------------------------------------------------------------------ */
void oled_display_update(const wifi_scan_result_t *result)
{
    if (!s_oled_ready) return;
    const int title_sep_y   = 9;
    const int list_start_y  = 14;
    const int list_row_step = 10;
    const int footer_sep_y  = 54;
    const int footer_text_y = 56;

    if (result != NULL) {
        int total_pages;

        memcpy(&s_last_result, result, sizeof(wifi_scan_result_t));
        s_has_result = true;

        /* Keep the user's current page after refresh; only clamp if page count shrinks. */
        total_pages = (s_last_result.count + 3) / 4;
        if (total_pages <= 0) {
            s_display_page = 0;
        } else if (s_display_page >= total_pages) {
            s_display_page = total_pages - 1;
        }
    }

    fb_clear();

    /* ── 标题栏 ── */
    fb_draw_str(0, 0, "WiFi Analyzer");
    if (s_has_result) {
        fb_draw_printf(96, 0, "N:%d", s_last_result.count);
    }
    fb_hline(0, title_sep_y, OLED_WIDTH);

    if (!s_has_result || s_last_result.count == 0) {
        fb_draw_str(20, 28, "Scanning...");
        ssd1306_flush();
        return;
    }

    /* ── 网络列表（每页4条）── */
    int start = s_display_page * 4;
    int end   = start + 4;
    if (end > s_last_result.count) end = s_last_result.count;

    for (int i = start; i < end; i++) {
        int row = i - start;
        int y   = list_start_y + row * list_row_step;
        const wifi_network_t *net = &s_last_result.networks[i];

        /* SSID（最多11字符，超出截断） */
        char ssid_buf[13];
        const char *ssid = (strlen(net->ssid) > 0) ? net->ssid : "<hidden>";
        if (strlen(ssid) > 11) {
            strncpy(ssid_buf, ssid, 10);
            ssid_buf[10] = '~';
            ssid_buf[11] = '\0';
        } else {
            strncpy(ssid_buf, ssid, sizeof(ssid_buf) - 1);
            ssid_buf[sizeof(ssid_buf) - 1] = '\0';
        }
        fb_draw_str(0, y, ssid_buf);

        /* 信号条（右侧，底部对齐到 y+7） */
        fb_draw_signal_bars(82, y + 7, wifi_rssi_to_bars(net->rssi));

        /* RSSI 数值 */
        fb_draw_printf(106, y, "%d", net->rssi);
    }

    /* ── 底部状态栏 ── */
    fb_hline(0, footer_sep_y, OLED_WIDTH);
    int total_pages = (s_last_result.count + 3) / 4;
    fb_draw_printf(0, footer_text_y, "P%d/%d Best:CH%d %ddBm",
                   s_display_page + 1,
                   total_pages,
                   s_last_result.networks[0].channel,
                   s_last_result.networks[0].rssi);

    ssd1306_flush();
}

/* ------------------------------------------------------------------ */
/*  oled_display_next_page                                              */
/* ------------------------------------------------------------------ */
void oled_display_next_page(void)
{
    if (!s_oled_ready) return;
    if (!s_has_result || s_last_result.count == 0) return;
    int total_pages = (s_last_result.count + 3) / 4;
    s_display_page  = (s_display_page + 1) % total_pages;
    oled_display_update(NULL);  /* 传 NULL 表示使用缓存结果重绘 */
    ESP_LOGI(TAG, "翻页到第 %d 页", s_display_page + 1);
}
