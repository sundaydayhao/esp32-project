/**
 * @file main.c
 * @brief ESP32 LCD1602 clock and weather display
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "lcd1602.h"
#include "wifi_manager.h"
#include "ntp_client.h"
#include "weather_client.h"

static const char *TAG = "main";

#define TASK_STACK_NTP      3072
#define TASK_STACK_WEATHER  16384
#define TASK_STACK_DISPLAY  4096
#define TASK_PRIO_NTP       3
#define TASK_PRIO_WEATHER   2
#define TASK_PRIO_DISPLAY   4

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

static void display_task(void *arg)
{
    (void)arg;

    char buf[32];
    struct tm t = {0};
    weather_data_t wx = {0};
    int last_sec = -1;

    ESP_LOGI(TAG, "Display task started");

    lcd1602_clear();
    lcd1602_write_at(0, 0, "Display start");
    lcd1602_write_at(1, 0, "WiFi/NTP/WX...");

    while (1) {
        ntp_client_get_time(&t);

        if (t.tm_sec == last_sec) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        last_sec = t.tm_sec;

        weather_client_get_data(&wx);

        if (ntp_client_is_synced()) {
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d %-4s",
                     t.tm_hour, t.tm_min, t.tm_sec,
                     wx.valid ? weather_text_to_ascii(wx.text) : "N/A");
        } else {
            snprintf(buf, sizeof(buf), "--:--:-- %-4s",
                     wx.valid ? weather_text_to_ascii(wx.text) : "N/A");
        }
        lcd1602_write_at(0, 0, "                ");
        lcd1602_write_at(0, 0, buf);

        if (ntp_client_is_synced()) {
            snprintf(buf, sizeof(buf), "%02d-%02d %s",
                     t.tm_mon + 1, t.tm_mday, s_wday[t.tm_wday]);
        } else {
            snprintf(buf, sizeof(buf), "-- -- ---");
        }
        lcd1602_write_at(1, 0, "                ");
        lcd1602_write_at(1, 0, buf);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  LCD1602 Clock & Weather v2.0");
    ESP_LOGI(TAG, "  Target: %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "  16x2 HD44780 compatible display");
    ESP_LOGI(TAG, "========================================");

    esp_err_t ret = nvs_flash_init();
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

    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        lcd1602_write_at(1, 0, "WiFi fail");
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

    if (xTaskCreatePinnedToCore(
            ntp_task, "ntp",
            TASK_STACK_NTP, NULL,
            TASK_PRIO_NTP, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ntp task");
    }

    if (xTaskCreatePinnedToCore(
            weather_task, "weather",
            TASK_STACK_WEATHER, NULL,
            TASK_PRIO_WEATHER, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create weather task");
    }

    if (xTaskCreatePinnedToCore(
            display_task, "display",
            TASK_STACK_DISPLAY, NULL,
            TASK_PRIO_DISPLAY, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display task");
    }

    ESP_LOGI(TAG, "All tasks started, free heap: %lu bytes",
             (unsigned long)esp_get_free_heap_size());
}