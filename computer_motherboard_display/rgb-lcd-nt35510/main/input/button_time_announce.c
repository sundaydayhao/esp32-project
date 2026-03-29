#include "button_time_announce.h"

#include <stdbool.h>
#include <time.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio/audio_output.h"
#include "config.h"
#include "net/ntp_client.h"

static bool button_pressed(int level)
{
    return level == BUTTON_TIME_ACTIVE_LEVEL;
}

void button_time_announce_task(void *arg)
{
    (void)arg;

    const TickType_t poll_ticks = pdMS_TO_TICKS(BUTTON_TIME_POLL_MS);
    const TickType_t debounce_ticks = pdMS_TO_TICKS(BUTTON_TIME_DEBOUNCE_MS);

    const gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BUTTON_TIME_ANNOUNCE_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    int raw_level = gpio_get_level(BUTTON_TIME_ANNOUNCE_PIN);
    int stable_level = raw_level;
    TickType_t last_change_tick = xTaskGetTickCount();

    ESP_LOGI(TAG_MAIN, "Button time announce task started on GPIO %d", BUTTON_TIME_ANNOUNCE_PIN);

    while (1) {
        const TickType_t now_tick = xTaskGetTickCount();
        const int current_level = gpio_get_level(BUTTON_TIME_ANNOUNCE_PIN);

        if (current_level != raw_level) {
            raw_level = current_level;
            last_change_tick = now_tick;
        } else if (stable_level != raw_level && (now_tick - last_change_tick) >= debounce_ticks) {
            stable_level = raw_level;
            if (button_pressed(stable_level)) {
                if (!ntp_client_is_synced()) {
                    ESP_LOGW(TAG_AUDIO, "Button press ignored because time is not synced yet");
                } else {
                    struct tm now = {0};
                    esp_err_t ret;

                    ntp_client_get_local_time(&now);
                    ESP_LOGI(TAG_AUDIO, "Button pressed, announcing %02d:%02d", now.tm_hour, now.tm_min);
                    ret = audio_output_play_beijing_time_announcement(now.tm_hour, now.tm_min);
                    if (ret != ESP_OK) {
                        ESP_LOGW(TAG_AUDIO, "Button time announcement failed: %s", esp_err_to_name(ret));
                    }
                }
            }
        }

        vTaskDelay(poll_ticks);
    }
}
