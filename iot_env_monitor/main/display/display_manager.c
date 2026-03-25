/**
 * @file display_manager.c
 * @brief SSD1306 OLED 显示管理器实现
 *
 * 使用软件 I2C 位操作驱动 SSD1306，内嵌 5×7 ASCII 字体，
 * 无需外部字库文件。
 */
#include "display_manager.h"
#include "config.h"
#include "sensors/sensor_manager.h"
#include "alarm/alarm_manager.h"
#include "comm/mqtt_handler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

/* ================================================================
 * SSD1306 命令集
 * ================================================================ */
#define SSD1306_CMD_DISPLAY_OFF         0xAE
#define SSD1306_CMD_DISPLAY_ON          0xAF
#define SSD1306_CMD_SET_CONTRAST        0x81
#define SSD1306_CMD_ENTIRE_DISPLAY_ON   0xA4
#define SSD1306_CMD_NORMAL_DISPLAY      0xA6
#define SSD1306_CMD_SET_MEM_ADDR_MODE   0x20
#define SSD1306_CMD_SET_COL_ADDR        0x21
#define SSD1306_CMD_SET_PAGE_ADDR       0x22
#define SSD1306_CMD_SET_DISP_START_LINE 0x40
#define SSD1306_CMD_SET_SEG_REMAP       0xA1
#define SSD1306_CMD_SET_MUX_RATIO       0xA8
#define SSD1306_CMD_SET_COM_OUT_DIR     0xC8
#define SSD1306_CMD_SET_DISP_OFFSET     0xD3
#define SSD1306_CMD_SET_COM_PIN_CFG     0xDA
#define SSD1306_CMD_SET_DISP_CLK_DIV   0xD5
#define SSD1306_CMD_SET_PRECHARGE       0xD9
#define SSD1306_CMD_SET_VCOM_DESEL      0xDB
#define SSD1306_CMD_CHARGE_PUMP         0x8D

/* 128×64 帧缓冲区 (128 × 8 pages = 1024 字节) */
static uint8_t s_framebuf[OLED_WIDTH * (OLED_HEIGHT / 8)];
static display_screen_t s_current_screen = DISPLAY_SCREEN_ENV;
static bool s_power_on = true;

/* ================================================================
 * 5×7 ASCII 字体（仅包含可打印字符 0x20~0x7E）
 * ================================================================ */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* '\'' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')' */
    {0x14,0x08,0x3E,0x08,0x14}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* '\\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 'f' */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 't' */
    {0x3C,0x40,0x40,0x20,0x7C}, /* 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* '}' */
    {0x10,0x08,0x08,0x10,0x08}, /* '~' */
};

/* ================================================================
 * SSD1306 底层驱动
 * ================================================================ */
static esp_err_t oled_write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };
    return i2c_master_write_to_device(I2C_MASTER_PORT, OLED_I2C_ADDR,
                                      buf, 2, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t oled_write_data(const uint8_t *data, size_t len)
{
    uint8_t buf[OLED_WIDTH + 1];
    buf[0] = 0x40;
    memcpy(buf + 1, data, len);
    return i2c_master_write_to_device(I2C_MASTER_PORT, OLED_I2C_ADDR,
                                      buf, len + 1, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static void oled_flush(void)
{
    oled_write_cmd(SSD1306_CMD_SET_COL_ADDR);
    oled_write_cmd(0);
    oled_write_cmd(OLED_WIDTH - 1);
    oled_write_cmd(SSD1306_CMD_SET_PAGE_ADDR);
    oled_write_cmd(0);
    oled_write_cmd((OLED_HEIGHT / 8) - 1);

    for (int page = 0; page < OLED_HEIGHT / 8; page++) {
        oled_write_data(&s_framebuf[page * OLED_WIDTH], OLED_WIDTH);
    }
}

static void fb_clear(void)
{
    memset(s_framebuf, 0, sizeof(s_framebuf));
}

static void fb_draw_char(int x, int page, char c)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *glyph = font5x7[c - 0x20];
    for (int col = 0; col < 5; col++) {
        if (x + col < OLED_WIDTH)
            s_framebuf[page * OLED_WIDTH + x + col] = glyph[col];
    }
}

static void fb_draw_str(int x, int page, const char *str)
{
    while (*str && x < OLED_WIDTH) {
        fb_draw_char(x, page, *str++);
        x += 6;
    }
}

static void fb_draw_hline(int y)
{
    int page = y / 8;
    uint8_t bit = 1 << (y % 8);
    for (int x = 0; x < OLED_WIDTH; x++)
        s_framebuf[page * OLED_WIDTH + x] |= bit;
}

/* ================================================================
 * 屏幕渲染函数
 * ================================================================ */
static void render_env_screen(const env_data_t *d)
{
    char buf[32];
    fb_draw_str(0, 0, "-- ENV MONITOR --");
    fb_draw_hline(9);

    snprintf(buf, sizeof(buf), "T: %+.1f C", d->temperature);
    fb_draw_str(0, 1, buf);

    snprintf(buf, sizeof(buf), "RH: %.1f %%", d->humidity);
    fb_draw_str(0, 2, buf);

    snprintf(buf, sizeof(buf), "P: %.1f hPa", d->pressure);
    fb_draw_str(0, 3, buf);

    snprintf(buf, sizeof(buf), "CO2: %ld ppm", d->co2_ppm);
    fb_draw_str(0, 4, buf);
}

static void render_air_screen(const env_data_t *d)
{
    static const char *aqi_str[] = {"Excellent","Good","Light","Moderate","Heavy","Severe"};
    char buf[32];
    fb_draw_str(0, 0, "-- AIR QUALITY --");
    fb_draw_hline(9);

    snprintf(buf, sizeof(buf), "PM1.0: %u ug/m3", d->pm1_0);
    fb_draw_str(0, 1, buf);
    snprintf(buf, sizeof(buf), "PM2.5: %u ug/m3", d->pm2_5);
    fb_draw_str(0, 2, buf);
    snprintf(buf, sizeof(buf), "PM10:  %u ug/m3", d->pm10);
    fb_draw_str(0, 3, buf);
    snprintf(buf, sizeof(buf), "AQI: %s", aqi_str[d->aqi_level < 6 ? d->aqi_level : 5]);
    fb_draw_str(0, 4, buf);
}

static void render_net_screen(void)
{
    char buf[32];
    fb_draw_str(0, 0, "-- NETWORK ------");
    fb_draw_hline(9);

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        snprintf(buf, sizeof(buf), "SSID: %.16s", (char *)ap.ssid);
        fb_draw_str(0, 1, buf);
        snprintf(buf, sizeof(buf), "RSSI: %d dBm", ap.rssi);
        fb_draw_str(0, 2, buf);
    } else {
        fb_draw_str(0, 1, "WiFi: Disconnected");
    }

    mqtt_state_t mqtt_st = mqtt_handler_get_state();
    snprintf(buf, sizeof(buf), "MQTT: %s",
             mqtt_st == MQTT_STATE_CONNECTED ? "Connected" :
             mqtt_st == MQTT_STATE_CONNECTING ? "Connecting" : "Offline");
    fb_draw_str(0, 3, buf);
}

static void render_sys_screen(void)
{
    char buf[32];
    fb_draw_str(0, 0, "-- SYSTEM -------");
    fb_draw_hline(9);

    snprintf(buf, sizeof(buf), "FW: v%s", FW_VERSION_STR);
    fb_draw_str(0, 1, buf);
    snprintf(buf, sizeof(buf), "Model: %s", DEVICE_MODEL);
    fb_draw_str(0, 2, buf);

    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    snprintf(buf, sizeof(buf), "Up: %02lud%02luh%02lum",
             uptime_s / 86400, (uptime_s % 86400) / 3600, (uptime_s % 3600) / 60);
    fb_draw_str(0, 3, buf);

    snprintf(buf, sizeof(buf), "Heap: %luKB", esp_get_free_heap_size() / 1024);
    fb_draw_str(0, 4, buf);
}

static void render_alarm_screen(void)
{
    char buf[32];
    uint32_t bits = alarm_manager_get_active_bits();
    fb_draw_str(0, 0, "-- ALARMS -------");
    fb_draw_hline(9);

    if (bits == 0) {
        fb_draw_str(0, 2, "  All Clear  OK");
        return;
    }

    static const char *alarm_names[] = {
        "T-High","T-Low","RH-High","RH-Low",
        "CO2","PM2.5","PM10","P-High","P-Low"
    };
    int row = 1;
    for (int i = 0; i < 9 && row < 7; i++) {
        if (bits & (1u << i)) {
            snprintf(buf, sizeof(buf), "! %s", alarm_names[i]);
            fb_draw_str(0, row++, buf);
        }
    }
}

/* ================================================================
 * 公共接口
 * ================================================================ */
esp_err_t display_manager_init(void)
{
    /* SSD1306 初始化序列 */
    const uint8_t init_cmds[] = {
        SSD1306_CMD_DISPLAY_OFF,
        SSD1306_CMD_SET_DISP_CLK_DIV,  0x80,
        SSD1306_CMD_SET_MUX_RATIO,     0x3F,
        SSD1306_CMD_SET_DISP_OFFSET,   0x00,
        SSD1306_CMD_SET_DISP_START_LINE,
        SSD1306_CMD_SET_SEG_REMAP,
        SSD1306_CMD_SET_COM_OUT_DIR,
        SSD1306_CMD_SET_COM_PIN_CFG,   0x12,
        SSD1306_CMD_SET_CONTRAST,      0xCF,
        SSD1306_CMD_SET_PRECHARGE,     0xF1,
        SSD1306_CMD_SET_VCOM_DESEL,    0x40,
        SSD1306_CMD_ENTIRE_DISPLAY_ON,
        SSD1306_CMD_NORMAL_DISPLAY,
        SSD1306_CMD_SET_MEM_ADDR_MODE, 0x00,
        SSD1306_CMD_CHARGE_PUMP,       0x14,
        SSD1306_CMD_DISPLAY_ON,
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        esp_err_t ret = oled_write_cmd(init_cmds[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_DISPLAY, "OLED 初始化失败 (cmd=0x%02X): %s",
                     init_cmds[i], esp_err_to_name(ret));
            return ret;
        }
    }

    fb_clear();
    oled_flush();
    ESP_LOGI(TAG_DISPLAY, "OLED 初始化成功 (%dx%d)", OLED_WIDTH, OLED_HEIGHT);
    return ESP_OK;
}

void display_manager_set_screen(display_screen_t screen)
{
    s_current_screen = screen % DISPLAY_SCREEN_COUNT;
}

void display_manager_refresh(void)
{
    env_data_t data;
    sensor_manager_get_latest(&data);

    fb_clear();

    switch (s_current_screen) {
    case DISPLAY_SCREEN_ENV:   render_env_screen(&data);  break;
    case DISPLAY_SCREEN_AIR:   render_air_screen(&data);  break;
    case DISPLAY_SCREEN_NET:   render_net_screen();       break;
    case DISPLAY_SCREEN_SYS:   render_sys_screen();       break;
    case DISPLAY_SCREEN_ALARM: render_alarm_screen();     break;
    default: break;
    }

    /* 报警时在底部显示警告标志 */
    if (alarm_manager_get_active_bits() != 0) {
        fb_draw_str(100, 7, "!ALM");
    }

    oled_flush();
}

void display_manager_power(bool on)
{
    s_power_on = on;
    oled_write_cmd(on ? SSD1306_CMD_DISPLAY_ON : SSD1306_CMD_DISPLAY_OFF);
}

void display_task(void *arg)
{
    ESP_LOGI(TAG_DISPLAY, "显示管理任务启动");
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t   tick      = 0;

    while (1) {
        /* 每 10s 自动切换屏幕 */
        if (tick % (10000 / DISPLAY_REFRESH_MS) == 0) {
            s_current_screen = (s_current_screen + 1) % DISPLAY_SCREEN_COUNT;
        }

        /* 有报警时强制显示报警屏幕 */
        if (alarm_manager_get_active_bits() != 0) {
            s_current_screen = DISPLAY_SCREEN_ALARM;
        }

        display_manager_refresh();
        tick++;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(DISPLAY_REFRESH_MS));
    }
}