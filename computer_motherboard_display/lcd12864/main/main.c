/**
 * @file main.c
 * @brief ESP32 + LCD12864 实时时钟与天气显示
 *
 * 显示布局（ST7920 文本模式，4行 x 16字符 / 8汉字）：
 * ┌────────────────────────────────┐
 * │  行0:  时间    12:34:56        │
 * │  行1:  日期    2026-03-26 Thu  │
 * │  行2:  天气    晴  25°C        │
 * │  行3:  湿度    湿度:60% 东北风 │
 * └────────────────────────────────┘
 *
 * 注意：ST7920 内置 GB2312 字库，文本模式下可直接显示中文。
 *       源文件需以 GB2312 编码保存，或使用 UTF-8 并在编译时转换。
 *       本文件使用 ASCII + 数字显示，中文通过硬编码 GB2312 字节写入。
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
#include "wifi_manager.h"
#include "ntp_client.h"
#include "weather_client.h"

static const char *TAG = "main";

/* ----------------------------------------------------------------
 * 任务栈大小与优先级
 * ---------------------------------------------------------------- */
#define TASK_STACK_NTP      3072
#define TASK_STACK_WEATHER  6144
#define TASK_STACK_DISPLAY  3072
#define TASK_PRIO_NTP       3
#define TASK_PRIO_WEATHER   2
#define TASK_PRIO_DISPLAY   4

/* ----------------------------------------------------------------
 * 星期名称（ASCII，适配 ST7920 半宽字符）
 * ---------------------------------------------------------------- */
static const char *s_weekday[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

/* ----------------------------------------------------------------
 * 显示任务
 * 每秒刷新时间，天气数据由 weather_task 后台更新
 * ---------------------------------------------------------------- */
static void display_task(void *arg)
{
    (void)arg;

    char line[20];
    struct tm   t;
    weather_data_t wx;
    int last_sec = -1;

    /* 等待 NTP 同步（最多 60 秒），未同步则显示 "--:--:--" */
    ESP_LOGI(TAG, "Waiting NTP sync for display...");
    ntp_client_wait_sync(60000);

    while (1) {
        ntp_client_get_time(&t);

        /* 每秒刷新一次 */
        if (t.tm_sec == last_sec) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        last_sec = t.tm_sec;

        /* ---- 行0：时间 ---- */
        if (ntp_client_is_synced()) {
            snprintf(line, sizeof(line), "  %02d:%02d:%02d  ",
                     t.tm_hour, t.tm_min, t.tm_sec);
        } else {
            snprintf(line, sizeof(line), "  --:--:--  ");
        }
        lcd12864_print_at(0, 0, line);

        /* ---- 行1：日期 + 星期 ---- */
        if (ntp_client_is_synced()) {
            snprintf(line, sizeof(line), "%04d-%02d-%02d %s",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                     s_weekday[t.tm_wday]);
        } else {
            snprintf(line, sizeof(line), "----/--/-- ---");
        }
        lcd12864_print_at(1, 0, line);

        /* ---- 行2：天气状况 + 温度 ---- */
        weather_client_get_data(&wx);
        if (wx.valid) {
            /* wx.text 是 GB2312 编码的中文，直接写入 ST7920 */
            /* 先清行（写16个空格） */
            lcd12864_print_at(2, 0, "                ");
            lcd12864_set_cursor(2, 0);
            lcd12864_print(wx.text);
            /* 在第5列（10字节偏移）写温度 */
            snprintf(line, sizeof(line), " %3d`C", (int)wx.temp);
            lcd12864_print(line);
        } else {
            lcd12864_print_at(2, 0, "  No weather    ");
        }

        /* ---- 行3：湿度 + 风速 ---- */
        if (wx.valid) {
            snprintf(line, sizeof(line), "Hum:%3d%% W:%3dkm",
                     (int)wx.humidity, (int)wx.wind_speed);
            lcd12864_print_at(3, 0, line);
        } else {
            lcd12864_print_at(3, 0, "                ");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ----------------------------------------------------------------
 * app_main
 * ---------------------------------------------------------------- */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  LCD12864 Clock & Weather v1.0");
    ESP_LOGI(TAG, "  ESP32 + ST7920 128x64");
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

    /* 2. LCD 初始化 */
    ret = lcd12864_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD init failed: %s", esp_err_to_name(ret));
        /* LCD 失败时死循环，防止后续崩溃 */
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    /* 启动画面 */
    lcd12864_clear();
    lcd12864_print_at(0, 0, "  LCD12864 Clock");
    lcd12864_print_at(1, 0, "  Connecting WiFi");
    lcd12864_print_at(3, 0, "  Please wait...");

    /* 3. WiFi 初始化 */
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        lcd12864_print_at(1, 0, "  WiFi init fail");
    }

    /* 等待 WiFi 连接（最多 15 秒） */
    ret = wifi_manager_wait_connected(15000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected");
        lcd12864_print_at(1, 0, "  WiFi connected");
    } else {
        ESP_LOGW(TAG, "WiFi not connected, running offline");
        lcd12864_print_at(1, 0, "  WiFi failed   ");
    }

    /* 4. 天气客户端初始化 */
    ESP_ERROR_CHECK(weather_client_init());

    /* 5. NTP 初始化（WiFi 连接后） */
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

    ESP_LOGI(TAG, "All tasks started");
    ESP_LOGI(TAG, "Free heap: %lu bytes",
             (unsigned long)esp_get_free_heap_size());
}