/**
 * @file main.c
 * @brief ESP32 + LCD12864 clock, forecast, indoor sensor, and hourly speech
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
#include "lcd12864.h"
#include "ntp_client.h"
#include "weather_client.h"
#include "wifi_manager.h"

static const char *TAG = "main";

#define TASK_STACK_NTP       3072
#define TASK_STACK_WEATHER   16384
#define TASK_STACK_DISPLAY   4096
#define TASK_STACK_DHT11     3072
#define TASK_STACK_CHIME     4096
#define TASK_PRIO_NTP        3
#define TASK_PRIO_WEATHER    2
#define TASK_PRIO_DISPLAY    4
#define TASK_PRIO_DHT11      2
#define TASK_PRIO_CHIME      2

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

static void format_temp_compare(const weather_data_t *wx,
                                const dht11_reading_t *indoor,
                                char *buf, size_t buf_size)
{
    if (!wx->valid || !indoor->valid) {
        snprintf(buf, buf_size, "TEMP NO DATA");
        return;
    }

    if (indoor->temperature_c < wx->temp_min) {
        snprintf(buf, buf_size, "TEMP -%dC LOW", (int)(wx->temp_min - indoor->temperature_c));
    } else if (indoor->temperature_c > wx->temp_max) {
        snprintf(buf, buf_size, "TEMP +%dC HIGH", (int)(indoor->temperature_c - wx->temp_max));
    } else {
        snprintf(buf, buf_size, "TEMP IN RANGE");
    }
}

static void format_humidity_compare(const weather_data_t *wx,
                                    const dht11_reading_t *indoor,
                                    char *buf, size_t buf_size)
{
    int diff;

    if (!wx->valid || !indoor->valid) {
        snprintf(buf, buf_size, "HUM NO DATA");
        return;
    }

    diff = (int)indoor->humidity_pct - (int)wx->humidity;
    if (diff <= -5) {
        snprintf(buf, buf_size, "HUM %d%% DRY", diff);
    } else if (diff >= 5) {
        snprintf(buf, buf_size, "HUM +%d%% WET", diff);
    } else {
        snprintf(buf, buf_size, "HUM NEAR OUT");
    }
}

static void display_task(void *arg)
{
    (void)arg;

    char buf[32];
    char line3[32];
    char line4[32];
    struct tm t = {0};
    weather_data_t wx = {0};
    dht11_reading_t indoor = {0};
    int last_sec = -1;

    ESP_LOGI(TAG, "Display task started");

    lcd12864_enter_text_mode();
    lcd12864_clear();
    lcd12864_print_at(0, 0, "Display start");
    lcd12864_print_at(1, 0, "WiFi/NTP/WX...");

    while (1) {
        time_t now_ts = 0;
        int screen_index;

        ntp_client_get_time(&t);
        if (t.tm_sec == last_sec) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        last_sec = t.tm_sec;

        time(&now_ts);
        screen_index = (int)(((uint32_t)now_ts / (uint32_t)CONFIG_DISPLAY_SCREEN_SWITCH_SEC) % 2U);

        weather_client_get_data(&wx);
        dht11_get_reading(&indoor);

        lcd12864_clear();

        snprintf(buf, sizeof(buf), "%02d:%02d:%02d %-6.6s",
                 t.tm_hour, t.tm_min, t.tm_sec,
                 wx.valid ? weather_text_to_ascii(wx.text) : "N/A");
        lcd12864_print_at(0, 0, buf);

        if (ntp_client_is_synced()) {
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d %s",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                     s_wday[t.tm_wday]);
        } else {
            snprintf(buf, sizeof(buf), "---- -- -- ---");
        }
        lcd12864_print_at(1, 0, buf);

        if (screen_index == 0) {
            if (wx.valid) {
                snprintf(line3, sizeof(line3), "OUT %d-%dC %u%%",
                         (int)wx.temp_min, (int)wx.temp_max, (unsigned)wx.humidity);
            } else {
                snprintf(line3, sizeof(line3), "OUT --C --%%");
            }

            if (indoor.valid) {
                snprintf(line4, sizeof(line4), "IN  %dC %u%%",
                         (int)indoor.temperature_c, (unsigned)indoor.humidity_pct);
            } else {
                snprintf(line4, sizeof(line4), "IN  --C --%%");
            }
        } else {
            format_temp_compare(&wx, &indoor, line3, sizeof(line3));
            format_humidity_compare(&wx, &indoor, line4, sizeof(line4));
        }

        lcd12864_print_at(2, 0, line3);

        lcd12864_print_at(3, 0, line4);

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
    ESP_LOGI(TAG, "  LCD12864 Clock + DHT11 + Voice");
    ESP_LOGI(TAG, "  Target: %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "  ST7920 128x64 (8-bit parallel)");
    ESP_LOGI(TAG, "========================================");

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = lcd12864_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD init failed: %s", esp_err_to_name(ret));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    lcd12864_clear();
    lcd12864_print_at(0, 0, "LCD12864 Clock");
    lcd12864_print_at(1, 0, "Booting...");
    lcd12864_print_at(2, 0, "WiFi/DHT/Voice");

    ret = dht11_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DHT11 init failed: %s", esp_err_to_name(ret));
        lcd12864_print_at(3, 0, "DHT11 init fail");
    }

    ret = audio_output_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Audio init failed: %s", esp_err_to_name(ret));
    }

    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        lcd12864_print_at(2, 0, "WiFi init fail");
    }

    ret = wifi_manager_wait_connected(15000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected");
        lcd12864_print_at(2, 0, "WiFi connected");
    } else {
        ESP_LOGW(TAG, "WiFi not connected, offline mode");
        lcd12864_print_at(2, 0, "WiFi failed");
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
