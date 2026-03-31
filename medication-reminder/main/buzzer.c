/**
 * @file buzzer.c
 * @brief 蜂鸣器 + LED 控制模块实现
 */

#include "buzzer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUZZER";

static buzzer_mode_t s_mode = BUZZER_MODE_OFF;
static int s_tick_count = 0;

esp_err_t buzzer_init(void)
{
    ESP_LOGI(TAG, "初始化蜂鸣器 (GPIO%d) 和 LED (GPIO%d)",
             CONFIG_BUZZER_PIN, CONFIG_LED_RED_PIN);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BUZZER_PIN) | (1ULL << CONFIG_LED_RED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO 配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    buzzer_set(false);
    led_set(false);
    ESP_LOGI(TAG, "蜂鸣器初始化完成");
    return ESP_OK;
}

void buzzer_set_mode(buzzer_mode_t mode)
{
    if (s_mode != mode) {
        ESP_LOGI(TAG, "蜂鸣器模式: %d -> %d", s_mode, mode);
        s_mode = mode;
        s_tick_count = 0;

        /* 切换到关闭模式时立即停止 */
        if (mode == BUZZER_MODE_OFF) {
            buzzer_set(false);
            led_set(false);
        }
    }
}

buzzer_mode_t buzzer_get_mode(void)
{
    return s_mode;
}

void buzzer_tick(void)
{
    s_tick_count++;

    switch (s_mode) {
        case BUZZER_MODE_OFF:
            buzzer_set(false);
            led_set(false);
            break;

        case BUZZER_MODE_NORMAL:
            /* 每2秒响一次：响0.2秒，停1.8秒 */
            /* tick 间隔 100ms，20 ticks = 2秒 */
            if (s_tick_count % 20 < 2) {
                buzzer_set(true);
                led_set(true);
            } else {
                buzzer_set(false);
                led_set(false);
            }
            break;

        case BUZZER_MODE_ESCALATED:
            /* 持续蜂鸣，LED 快速闪烁（每0.5秒） */
            buzzer_set(true);
            led_set((s_tick_count % 5) < 3);
            break;

        case BUZZER_MODE_CONFIRM:
            /* 由 buzzer_play_confirm() 处理 */
            break;

        case BUZZER_MODE_STARTUP:
            /* 由 buzzer_play_startup() 处理 */
            break;

        default:
            break;
    }
}

void buzzer_set(bool on)
{
    gpio_set_level(CONFIG_BUZZER_PIN, on ? 1 : 0);
}

void led_set(bool on)
{
    gpio_set_level(CONFIG_LED_RED_PIN, on ? 1 : 0);
}

void buzzer_play_confirm(void)
{
    /* 两声短促确认音 */
    for (int i = 0; i < 2; i++) {
        buzzer_set(true);
        led_set(true);
        vTaskDelay(pdMS_TO_TICKS(100));
        buzzer_set(false);
        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void buzzer_play_startup(void)
{
    /* 启动音：三声递增 */
    for (int i = 0; i < 3; i++) {
        buzzer_set(true);
        led_set(true);
        vTaskDelay(pdMS_TO_TICKS(80 + i * 40));
        buzzer_set(false);
        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}