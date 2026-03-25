#include "flash_led.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "flash_led";

#if CONFIG_FLASH_LED_ENABLE
static bool s_flash_led_initialized = false;

static int flash_led_active_level(void)
{
#if CONFIG_FLASH_LED_ACTIVE_HIGH
    return 1;
#else
    return 0;
#endif
}

static int flash_led_inactive_level(void)
{
    return flash_led_active_level() ? 0 : 1;
}
#endif

esp_err_t flash_led_init(void)
{
#if CONFIG_FLASH_LED_ENABLE
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CONFIG_FLASH_LED_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Flash LED gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = gpio_set_level(CONFIG_FLASH_LED_PIN, flash_led_inactive_level());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Flash LED set inactive level failed: %s", esp_err_to_name(err));
        return err;
    }

    s_flash_led_initialized = true;
    ESP_LOGI(TAG, "Flash LED ready on GPIO%d", CONFIG_FLASH_LED_PIN);
#endif

    return ESP_OK;
}

void flash_led_capture_begin(void)
{
#if CONFIG_FLASH_LED_ENABLE
    if (!s_flash_led_initialized) {
        return;
    }

    gpio_set_level(CONFIG_FLASH_LED_PIN, flash_led_active_level());
    if (CONFIG_FLASH_LED_PRECAPTURE_MS > 0) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_FLASH_LED_PRECAPTURE_MS));
    }
#endif
}

void flash_led_capture_end(void)
{
#if CONFIG_FLASH_LED_ENABLE
    if (!s_flash_led_initialized) {
        return;
    }

    if (CONFIG_FLASH_LED_POSTCAPTURE_MS > 0) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_FLASH_LED_POSTCAPTURE_MS));
    }
    gpio_set_level(CONFIG_FLASH_LED_PIN, flash_led_inactive_level());
#endif
}
