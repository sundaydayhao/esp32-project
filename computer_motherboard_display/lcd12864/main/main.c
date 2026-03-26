/**
 * @file main.c
 * @brief ESP32 + LCD12864 实时时钟与天气显示（图形模式 + Emoji 图标）
 *
 * 显示布局（图形模式，128x64，5x7 ASCII 字体）：
 * ┌────────────────────────────────────────────────────────────────┐
 * │ [图标16x16] 天气文字  温度                  y=0~15            │
 * │ ─────────────────────────────────────────── y=17              │
 * │  HH:MM:SS                                   y=19~25           │
 * │  YYYY-MM-DD  Www                            y=28~34           │
 * │  Hum:XX%  Wind:XXkm/h                       y=37~43           │
 * │  Feels:XX`C                                 y=46~52           │
 * └────────────────────────────────────────────────────────────────┘
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "lcd12864.h"
#include "lcd12864_gfx.h"
#include "wifi_manager.h"
#include "ntp_client.h"
#include "weather_client.h"

static const char *TAG = "main";

/* ----------------------------------------------------------------
 * 任务参数
 * ---------------------------------------------------------------- */
#define TASK_STACK_NTP      3072
#define TASK_STACK_WEATHER  6144
#define TASK_STACK_DISPLAY  4096
#define TASK_PRIO_NTP       3
#define TASK_PRIO_WEATHER   2
#define TASK_PRIO_DISPLAY   4

/* 星期缩写 */
static const char *s_wday[7] = {
    "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
};

/* ----------------------------------------------------------------
 * 显示任务（图形模式）
 * ---------------------------------------------------------------- */
static void display_task(void *arg)
{
    (void)arg;

    char buf[32];
    struct tm      t;
    weather_data_t wx;
    int last_sec = -1;

    /* 等待 NTP 同步（最多 60 秒） */
    ESP_LOGI(TAG, "Waiting NTP sync...");
    ntp_client_wait_sync(60000);

    /* 初始化图形模式 */
    lcd12864_gfx_init();

    while (1) {
        ntp_client_get_time(&t);

        /* 每秒刷新一次 */
        if (t.tm_sec == last_sec) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        last_sec = t.tm_sec;

        /* 清空帧缓冲区 */
        lcd12864_gfx_clear();

        /* ---- 区域1：天气图标 + 天气文字 + 温度 (y=0~15) ---- */
        weather_client_get_data(&wx);

        /* 左侧 16x16 图标 */
        weather_icon_t icon = wx.valid
            ? lcd12864_gfx_text_to_icon(wx.text)
            : WEATHER_ICON_UNKNOWN;
        lcd12864_gfx_weather_icon(0, 0, icon);

        /* 图标右侧：天气文字（ASCII 部分，中文跳过）+ 温度 */
        if (wx.valid) {
            /* 温度显示在图标右侧第一行 */
            snprintf(buf, sizeof(buf), "Temp:%3d`C", (int)wx.temp);
            lcd12864_gfx_str(20, 0, buf, true);
            /* 体感温度在第二行 */
            snprintf(buf, sizeof(buf), "Feel:%3d`C", (int)wx.feels_like);
            lcd12864_gfx_str(20, 9, buf, true);
        } else {
            lcd12864_gfx_str(20, 0, "No weather", true);
            lcd12864_gfx_str(20, 9, "data yet", true);
        }

        /* 分隔线 */
        lcd12864_gfx_hline(0, 17, 128, true);

        /* ---- 区域2：时间 (y=19~25) ---- */
        if (ntp_client_is_synced()) {
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                     t.tm_hour, t.tm_min, t.tm_sec);
        } else {
            snprintf(buf, sizeof(buf), "--:--:--");
        }
        lcd12864_gfx_str(0, 19, buf, true);

        /* ---- 区域3：日期 + 星期 (y=28~34) ---- */
        if (ntp_client_is_synced()) {
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d %s",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                     s_wday[t.tm_wday]);
        } else {
            snprintf(buf, sizeof(buf), "----/--/-- ---");
        }
        lcd12864_gfx_str(0, 28, buf, true);

        /* ---- 区域4：湿度 + 风速 (y=37~43) ---- */
        if (wx.valid) {
            snprintf(buf, sizeof(buf), "Hum:%3d%% W:%3dkm",
                     (int)wx.humidity, (int)wx.wind_speed);
        } else {
            snprintf(buf, sizeof(buf), "Hum:--%%  W:---km");
        }
        lcd12864_gfx_str(0, 37, buf, true);

        /* ---- 区域5：WiFi 状态 (y=46~52) ---- */
        if (wifi_manager_is_connected()) {
            lcd12864_gfx_str(0, 46, "WiFi:OK", true);
        } else {
            lcd12864_gfx_str(0, 46, "WiFi:--", true);
        }

        /* ---- 区域6：NTP 状态 (y=55~61) ---- */
        if (ntp_client_is_synced()) {
            lcd12864_gfx_str(48, 46, "NTP:Synced", true);
        } else {
            lcd12864_gfx_str(48, 46, "NTP:Sync..", true);
        }

        /* 刷新到屏幕 */
        lcd12864_gfx_flush();

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ----------------------------------------------------------------
 * app_main
 * ---------------------------------------------------------------- */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  LCD12864 Clock & Weather v1.1");
    ESP_LOGI(TAG, "  ESP32 + ST7920 128x64 (8-bit parallel)");
    ESP_LOGI(TAG, "========================================");

    /* 1. NVS 初始化 */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. LCD 初始化（文本模式，用于启动画面） */
    ret = lcd12864_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD init failed: %s", esp_err_to_name(ret));
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    /* 启动画面（文本模式） */
    lcd12864_clear();
    lcd12864_print_at(0, 0, "  LCD12864 Clock");
    lcd12864_print_at(1, 0, "  v1.1 w/ Emoji");
    lcd12864_print_at(2, 0, "  Connecting...");

    /* 3. WiFi 初始化 */
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        lcd12864_print_at(2, 0, "  WiFi init fail");
    }

    ret = wifi_manager_wait_connected(15000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected");
        lcd12864_print_at(2, 0, "  WiFi connected");
    } else {
        ESP_LOGW(TAG, "WiFi not connected, offline mode");
        lcd12864_print_at(2, 0, "  WiFi failed   ");
    }

    /* 4. 天气客户端初始化 */
    ESP_ERROR_CHECK(weather_client_init());

    /* 5. NTP 初始化 */
    ntp_client_init();

    /* 6. 创建后台任务 */
    xTaskCreatePinnedToCore(
        ntp_task, "ntp",
        TASK_STACK_NTP, NULL,
        TASK_PRIO_NTP, NULL, 0);

    xTaskCreatePinnedToCore(
        weather_task, "weather",
        TASK_STACK_WEATHER, NULL,
        TASK_PRIO_WEATHER, NULL, 0);

    xTaskCreatePinnedToCore(
        display_task, "display",
        TASK_STACK_DISPLAY, NULL,
        TASK_PRIO_DISPLAY, NULL, 1);

    ESP_LOGI(TAG, "All tasks started, free heap: %lu bytes",
             (unsigned long)esp_get_free_heap_size());
}