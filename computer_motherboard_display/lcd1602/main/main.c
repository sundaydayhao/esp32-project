/**
 * @file main.c
 * @brief ESP32 LCD1602 clock, weather, indoor sensor, and hourly speech
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "audio_output.h"
#include "dht11.h"
#include "hourly_chime.h"
#include "lcd1602.h"
#include "ntp_client.h"
#include "weather_client.h"
#include "wifi_manager.h"

static const char *TAG = "main";

#define TASK_STACK_NTP       3072
#define TASK_STACK_WEATHER   16384
#define TASK_STACK_DISPLAY    4096
#define TASK_STACK_DHT11      3072
#define TASK_STACK_CHIME      4096
#define TASK_PRIO_NTP         3
#define TASK_PRIO_WEATHER     2
#define TASK_PRIO_DISPLAY     4
#define TASK_PRIO_DHT11       2
#define TASK_PRIO_CHIME       2

static const char *s_wday[7] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

static const char *weather_text_to_ascii(const char *text)
{
    if (!text || !*text) {
        return "N/A";
    }

    if (strstr(text, "\xE6\x99\xB4")) {
        return "SUNNY";
    }
    if (strstr(text, "\xE9\x9B\xB7")) {
        return "STORM";
    }
    if (strstr(text, "\xE9\x9B\xAA")) {
        return "SNOW";
    }
    if (strstr(text, "\xE9\x9B\xA8")) {
        return "RAIN";
    }
    if (strstr(text, "\xE9\x9B\xBE") || strstr(text, "\xE9\x9C\xBE")) {
        return "FOG";
    }
    if (strstr(text, "\xE9\x98\xB4")) {
        return "CLOUD";
    }
    if (strstr(text, "\xE4\xBA\x91")) {
        return "CLOUDY";
    }

    return "N/A";
}

static void format_time_line(char *buf, size_t buf_size, const struct tm *t, const weather_data_t *wx)
{
    const char *weather = (wx && wx->valid) ? weather_text_to_ascii(wx->text) : "N/A";

    if (t && t->tm_year > 0) {
        snprintf(buf, buf_size, "%02d:%02d:%02d %-4s",
                 t->tm_hour, t->tm_min, t->tm_sec, weather);
    } else {
        snprintf(buf, buf_size, "--:--:-- %-4s", weather);
    }
}

static void format_date_line(char *buf, size_t buf_size, const struct tm *t)
{
    if (!t || t->tm_year <= 0) {
        snprintf(buf, buf_size, "---- -- -- ---");
        return;
    }

    snprintf(buf, buf_size, "%04d-%02d-%02d %s",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, s_wday[t->tm_wday]);
}

static void format_indoor_line(char *buf, size_t buf_size, const dht11_reading_t *indoor)
{
    if (!indoor || !indoor->valid) {
        snprintf(buf, buf_size, "IN: --C --%%");
        return;
    }

    snprintf(buf, buf_size, "IN:%2dC %3u%%",
             (int)indoor->temperature_c, (unsigned)indoor->humidity_pct);
}

static void format_outdoor_line(char *buf, size_t buf_size, const weather_data_t *wx)
{
    if (!wx || !wx->valid) {
        snprintf(buf, buf_size, "OUT:--C --%%");
        return;
    }

    snprintf(buf, buf_size, "OUT:%2d-%2dC %3u%%",
             (int)wx->temp_min, (int)wx->temp_max, (unsigned)wx->humidity);
}

static void format_compare_line(char *buf, size_t buf_size,
                                const weather_data_t *wx,
                                const dht11_reading_t *indoor)
{
    if (!wx || !wx->valid || !indoor || !indoor->valid) {
        snprintf(buf, buf_size, "TEMP/HUM N/A");
        return;
    }

    snprintf(buf, buf_size, "T:%2dC H:%3u%% %s",
             (int)indoor->temperature_c,
             (unsigned)indoor->humidity_pct,
             weather_text_to_ascii(wx->text));
}

static void display_task(void *arg)
{
    (void)arg;

    char line0[17];
    char line1[17];
    struct tm t = {0};
    weather_data_t wx = {0};
    dht11_reading_t indoor = {0};
    int last_sec = -1;
    int screen_index = 0;
    TickType_t last_switch_tick = 0;

    ESP_LOGI(TAG, "Display task started");

    lcd1602_clear();
    lcd1602_write_at(0, 0, "Display start");
    lcd1602_write_at(1, 0, "WiFi/NTP/WX...");

    while (1) {
        ntp_client_get_time(&t);
        weather_client_get_data(&wx);
        dht11_get_reading(&indoor);

        if (t.tm_sec != last_sec) {
            last_sec = t.tm_sec;

            if ((xTaskGetTickCount() - last_switch_tick) >= pdMS_TO_TICKS((uint32_t)CONFIG_DISPLAY_SCREEN_SWITCH_SEC * 1000U)) {
                last_switch_tick = xTaskGetTickCount();
                screen_index = (screen_index + 1) % 2;
                lcd1602_clear();
            }

            format_time_line(line0, sizeof(line0), &t, &wx);
            snprintf(line0, sizeof(line0), "%-16.16s", line0);
            lcd1602_write_at(0, 0, line0);

            if (screen_index == 0) {
                format_date_line(line1, sizeof(line1), &t);
            } else {
                char compare_line[32];
                format_indoor_line(compare_line, sizeof(compare_line), &indoor);
                if (wx.valid) {
                    snprintf(line1, sizeof(line1), "IN:%2dC OUT:%2dC",
                             indoor.valid ? (int)indoor.temperature_c : 0,
                             (int)wx.temp);
                } else {
                    format_compare_line(line1, sizeof(line1), &wx, &indoor);
                }
            }

            snprintf(line1, sizeof(line1), "%-16.16s", line1);
            lcd1602_write_at(1, 0, line1);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void start_task_or_log(TaskFunction_t task_fn,
                              const char *name,
                              uint32_t stack_words,
                              UBaseType_t prio,
                              BaseType_t core_id)
{
    if (xTaskCreatePinnedToCore(task_fn, name, stack_words, NULL, prio, NULL, core_id) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task: %s", name);
    }
}

void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  LCD1602 Clock + DHT11 + Voice");
    ESP_LOGI(TAG, "  Target: %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "  LCD1602 16x2 HD44780 display");
    ESP_LOGI(TAG, "========================================");

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = lcd1602_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD init failed: %s", esp_err_to_name(ret));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    lcd1602_clear();
    lcd1602_write_at(0, 0, "LCD1602 Clock");
    lcd1602_write_at(1, 0, "Booting...");

    ret = dht11_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DHT11 init failed: %s", esp_err_to_name(ret));
        lcd1602_write_at(1, 0, "DHT11 fail");
    }

    ret = audio_output_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Audio init failed: %s", esp_err_to_name(ret));
    }

    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        lcd1602_write_at(1, 0, "WiFi init fail");
    }

    ret = wifi_manager_wait_connected(15000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected");
        lcd1602_write_at(1, 0, "WiFi OK");
    } else {
        ESP_LOGW(TAG, "WiFi not connected, offline mode");
        lcd1602_write_at(1, 0, "Offline mode");
    }

    ESP_ERROR_CHECK(weather_client_init());
    ntp_client_init();

    start_task_or_log(ntp_task, "ntp", TASK_STACK_NTP, TASK_PRIO_NTP, 0);
    start_task_or_log(weather_task, "weather", TASK_STACK_WEATHER, TASK_PRIO_WEATHER, 0);
    start_task_or_log(dht11_task, "dht11", TASK_STACK_DHT11, TASK_PRIO_DHT11, 0);
    start_task_or_log(hourly_chime_task, "hourly_chime", TASK_STACK_CHIME, TASK_PRIO_CHIME, 0);
    start_task_or_log(display_task, "display", TASK_STACK_DISPLAY, TASK_PRIO_DISPLAY, 1);

    ESP_LOGI(TAG, "All tasks started, free heap: %lu bytes",
             (unsigned long)esp_get_free_heap_size());
}