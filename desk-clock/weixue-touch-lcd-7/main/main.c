#include <stdio.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "audio/audio_player.h"
#include "app_events.h"
#include "config.h"
#include "lcd/lcd_driver.h"
#include "net/ntp_client.h"
#include "net/weather_client.h"
#include "net/wifi_manager.h"
#include "storage/config_store.h"
#include "ui/ui_font_manager.h"
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

static void start_task_or_halt(TaskFunction_t task_fn, const char *name,
                               uint32_t stack_size, UBaseType_t priority,
                               BaseType_t core_id)
{
    if (xTaskCreatePinnedToCore(
            task_fn, name,
            stack_size, NULL,
            priority, NULL, core_id) != pdPASS) {
        halt_on_error(name, ESP_FAIL);
    }
}

void app_main(void)
{
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
    if (brightness == 0) {
        ESP_LOGW(TAG_MAIN, "Stored brightness is 0, forcing panel on");
        brightness = 100;
        ret = config_store_set_u8(NVS_KEY_BRIGHTNESS, brightness);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG_MAIN, "Failed to persist fallback brightness: %s", esp_err_to_name(ret));
        }
    }
    lcd_driver_set_brightness(brightness);

#if SYSTEM_BOOT_MODE == BOOT_MODE_LCD_DIAGNOSTIC
    ESP_LOGW(TAG_MAIN, "Boot mode: LCD diagnostic");
    if (xTaskCreatePinnedToCore(lcd_diag_task, "lcd_diag",
                                TASK_STACK_LCD_DIAG, NULL,
                                TASK_PRIO_LCD_DIAG, NULL, 1) != pdPASS) {
        halt_on_error("LCD diagnostic task create", ESP_FAIL);
    }
    return;
#endif

    ESP_ERROR_CHECK(app_events_init());

#if SYSTEM_BOOT_MODE == BOOT_MODE_UI_ONLY
    ESP_LOGW(TAG_MAIN, "Boot mode: UI only");
    start_task_or_halt(ui_font_loader_task, "font",
                       TASK_STACK_FONT, TASK_PRIO_FONT, 0);
    start_task_or_halt(ui_renderer_task, "ui",
                       TASK_STACK_UI, TASK_PRIO_UI, 1);
    ESP_LOGI(TAG_MAIN, "UI-only mode started");
    return;
#endif

    ESP_LOGI(TAG_MAIN, "Boot mode: full app");

    start_task_or_halt(ui_font_loader_task, "font",
                       TASK_STACK_FONT, TASK_PRIO_FONT, 0);
    start_task_or_halt(ntp_task, "ntp",
                       TASK_STACK_NTP, TASK_PRIO_NTP, 0);
    start_task_or_halt(weather_task, "weather",
                       TASK_STACK_WEATHER, TASK_PRIO_WEATHER, 0);
    start_task_or_halt(ui_renderer_task, "ui",
                       TASK_STACK_UI, TASK_PRIO_UI, 1);

    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "WiFi init failed: %s", esp_err_to_name(ret));
    } else {
        ret = web_dashboard_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG_MAIN, "Web provisioning init failed: %s", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG_MAIN, "Initializing audio player");
    ret = audio_player_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_MAIN, "Audio player init failed: %s (continuing without audio)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG_MAIN, "Audio player ready");
    }

    ESP_LOGI(TAG_MAIN, "Tasks started");
    ESP_LOGI(TAG_MAIN, "Free heap: %lu KB", esp_get_free_heap_size() / 1024);
    ESP_LOGI(TAG_MAIN, "Free internal heap: %lu KB", esp_get_free_internal_heap_size() / 1024);
}
