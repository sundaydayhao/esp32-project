#include "audio_output.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio_output";
static bool s_audio_ready;

static void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void beep(uint32_t on_ms, uint32_t off_ms, int repeat)
{
    for (int i = 0; i < repeat; ++i) {
        gpio_set_level((gpio_num_t)CONFIG_AUDIO_BUZZER_GPIO, 1);
        delay_ms(on_ms);
        gpio_set_level((gpio_num_t)CONFIG_AUDIO_BUZZER_GPIO, 0);
        if (i + 1 < repeat) {
            delay_ms(off_ms);
        }
    }
}

esp_err_t audio_output_init(void)
{
    if (s_audio_ready) {
        return ESP_OK;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << CONFIG_AUDIO_BUZZER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "configure buzzer gpio failed");
    gpio_set_level((gpio_num_t)CONFIG_AUDIO_BUZZER_GPIO, 0);

    s_audio_ready = true;
    ESP_LOGI(TAG, "Buzzer ready on GPIO %d", CONFIG_AUDIO_BUZZER_GPIO);
    return ESP_OK;
}

esp_err_t audio_output_deinit(void)
{
    if (!s_audio_ready) {
        return ESP_OK;
    }

    gpio_set_level((gpio_num_t)CONFIG_AUDIO_BUZZER_GPIO, 0);
    s_audio_ready = false;
    return ESP_OK;
}

bool audio_output_is_ready(void)
{
    return s_audio_ready;
}

esp_err_t audio_output_play_beijing_time_announcement(int hour, int minute)
{
    ESP_RETURN_ON_FALSE(s_audio_ready, ESP_ERR_INVALID_STATE, TAG, "audio not ready");

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Beep time announcement: %02d:%02d", hour, minute);

    /* 以蜂鸣器“提示音”方式报时：
     * - 00 分：3 长音 + 1 短音提示整点
     * - 非 00 分：按小时和分钟分别给出短提示
     */
    if (minute == 0) {
        beep(300, 120, 3);
        delay_ms(200);
        beep(150, 100, 1);
    } else {
        int hour_pulses = (hour % 12 == 0) ? 12 : (hour % 12);
        if (hour_pulses > 0) {
            beep(120, 80, hour_pulses);
            delay_ms(250);
        }
        int minute_pulses = minute / 5;
        if (minute_pulses > 0) {
            beep(80, 60, minute_pulses);
        } else {
            beep(60, 80, 1);
        }
    }

    gpio_set_level((gpio_num_t)CONFIG_AUDIO_BUZZER_GPIO, 0);
    return ESP_OK;
}