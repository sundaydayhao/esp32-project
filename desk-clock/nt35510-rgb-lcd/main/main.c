#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "audio/audio_output.h"
#include "audio/hourly_chime.h"
#include "config.h"
#include "history/env_history.h"
#include "input/button_time_announce.h"
#include "lcd/lcd_driver.h"
#include "net/ntp_client.h"
#include "net/weather_client.h"
#include "net/wifi_manager.h"
#include "sensor/dht11_sensor.h"
#include "storage/config_store.h"
#include "storage/log_file.h"
#include "ui/ui_renderer.h"
#include "web/web_dashboard.h"

#if SYSTEM_BOOT_MODE == BOOT_MODE_LCD_DIAGNOSTIC
static void lcd_diag_task(void *arg)
{
    uint32_t pattern = 0;
    (void)arg;

    while (1) {
        esp_err_t ret = lcd_driver_show_test_pattern(pattern);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_MAIN, "LCD diagnostic pattern %lu failed: %s",
                     (unsigned long)pattern, esp_err_to_name(ret));
        }
        pattern = (pattern + 1) % 4;
        vTaskDelay(pdMS_TO_TICKS(LCD_DIAGNOSTIC_SWITCH_MS));
    }
}
#endif

static void halt_on_error(const char *subsystem, esp_err_t err)
{
    ESP_LOGE(TAG_MAIN, "%s failed: %s", subsystem, esp_err_to_name(err));
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void log_heap_status(const char *stage)
{
    ESP_LOGI(TAG_MAIN, "%s: heap=%lu KB internal=%lu KB largest_internal=%lu KB",
             stage,
             (unsigned long)(esp_get_free_heap_size() / 1024),
             (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned long)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
}

static void start_ui_task_or_halt(void)
{
    log_heap_status("Before UI task create");
    if (xTaskCreatePinnedToCore(
            ui_renderer_task, "ui",
            TASK_STACK_UI, NULL,
            TASK_PRIO_UI, NULL, 1) != pdPASS) {
        halt_on_error("UI task create", ESP_FAIL);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG_MAIN, "SD log config: miso=%d mosi=%d sclk=%d cs=%d freq=%dMHz",
             SD_LOG_PIN_MISO, SD_LOG_PIN_MOSI, SD_LOG_PIN_SCLK, SD_LOG_PIN_CS, SD_LOG_FREQ_MHZ);

    esp_err_t log_ret = log_file_init();
    if (log_ret != ESP_OK && log_ret != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG_MAIN, "SD log init failed: %s", esp_err_to_name(log_ret));
    }

    ESP_LOGI(TAG_MAIN, "========================================");
    ESP_LOGI(TAG_MAIN, "  %s v%s", DEVICE_NAME, FW_VERSION_STR);
    ESP_LOGI(TAG_MAIN, "  ESP32-S3 @ 240MHz + %s", DISPLAY_BOARD_NAME);
    ESP_LOGI(TAG_MAIN, "========================================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG_MAIN, "NVS needs erase, reinitializing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(config_store_init());

    ret = lcd_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "LCD init failed: %s", esp_err_to_name(ret));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    uint8_t brightness = 100;
    config_store_get_u8(NVS_KEY_BRIGHTNESS, &brightness, 100);
    lcd_driver_set_brightness(brightness);

    ret = audio_output_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_MAIN, "Audio init failed: %s", esp_err_to_name(ret));
    } else {
        ret = audio_output_play_test_tone();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG_MAIN, "Audio test tone failed: %s", esp_err_to_name(ret));
        }
        ret = audio_output_deinit();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG_MAIN, "Audio deinit failed: %s", esp_err_to_name(ret));
        }
    }

#if SYSTEM_BOOT_MODE == BOOT_MODE_LCD_DIAGNOSTIC
    ESP_LOGW(TAG_MAIN, "Boot mode: LCD diagnostic");
    if (xTaskCreatePinnedToCore(lcd_diag_task, "lcd_diag",
                                TASK_STACK_LCD_DIAG, NULL,
                                TASK_PRIO_LCD_DIAG, NULL, 1) != pdPASS) {
        halt_on_error("LCD diagnostic task create", ESP_FAIL);
    }
    return;
#endif

#if SYSTEM_BOOT_MODE == BOOT_MODE_UI_ONLY
    ESP_LOGW(TAG_MAIN, "Boot mode: UI only");
    ESP_ERROR_CHECK(weather_client_init());
    ESP_ERROR_CHECK(dht11_sensor_init());
    ret = ui_renderer_init();
    if (ret != ESP_OK) {
        halt_on_error("UI init", ret);
    }
    xTaskCreatePinnedToCore(
        dht11_sensor_task, "dht11",
        TASK_STACK_DHT11, NULL,
        TASK_PRIO_DHT11, NULL, 0);
    start_ui_task_or_halt();
    ESP_LOGI(TAG_MAIN, "UI-only mode started");
    return;
#endif

    ESP_LOGI(TAG_MAIN, "Boot mode: full app");

    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "WiFi init failed: %s", esp_err_to_name(ret));
    }

    ESP_ERROR_CHECK(web_dashboard_init());
    ESP_ERROR_CHECK(weather_client_init());
    ESP_ERROR_CHECK(dht11_sensor_init());
    ESP_ERROR_CHECK(env_history_init());

    ret = ui_renderer_init();
    if (ret != ESP_OK) {
        halt_on_error("UI init", ret);
    }
    log_heap_status("After UI init");

    xTaskCreatePinnedToCore(
        ntp_task, "ntp",
        TASK_STACK_NTP, NULL,
        TASK_PRIO_NTP, NULL, 0);

    xTaskCreatePinnedToCore(
        weather_task, "weather",
        TASK_STACK_WEATHER, NULL,
        TASK_PRIO_WEATHER, NULL, 0);

    xTaskCreatePinnedToCore(
        hourly_chime_task, "hourly_chime",
        TASK_STACK_CHIME, NULL,
        TASK_PRIO_CHIME, NULL, 0);

    xTaskCreatePinnedToCore(
        button_time_announce_task, "button_time",
        TASK_STACK_BUTTON, NULL,
        TASK_PRIO_BUTTON, NULL, 0);

    xTaskCreatePinnedToCore(
        dht11_sensor_task, "dht11",
        TASK_STACK_DHT11, NULL,
        TASK_PRIO_DHT11, NULL, 0);

    xTaskCreatePinnedToCore(
        env_history_task, "history",
        TASK_STACK_HISTORY, NULL,
        TASK_PRIO_HISTORY, NULL, 0);

    if (xTaskCreatePinnedToCore(
            web_dashboard_task, "web",
            TASK_STACK_WEB, NULL,
            TASK_PRIO_WEB, NULL, 0) != pdPASS) {
        ESP_LOGW(TAG_MAIN, "Web monitor task create failed; HTTP server is already running");
    }

    start_ui_task_or_halt();

    ESP_LOGI(TAG_MAIN, "Tasks started");
    ESP_LOGI(TAG_MAIN, "Free heap: %lu KB", esp_get_free_heap_size() / 1024);
    ESP_LOGI(TAG_MAIN, "Free internal heap: %lu KB", esp_get_free_internal_heap_size() / 1024);
}
