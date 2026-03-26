/**
 * @file buzzer.c
 * @brief 蜂鸣器驱动模块实现
 */

#include "buzzer.h"
#include "config.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "buzzer";
static bool s_buzzer_initialized = false;

// ============================================================
// 初始化
// ============================================================

esp_err_t buzzer_init(void)
{
    // 配置 LEDC 定时器
    ledc_timer_config_t timer_conf = {
        .speed_mode      = BUZZER_LEDC_MODE,
        .timer_num       = BUZZER_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = BUZZER_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC定时器配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置 LEDC 通道
    ledc_channel_config_t channel_conf = {
        .speed_mode = BUZZER_LEDC_MODE,
        .channel    = BUZZER_LEDC_CHANNEL,
        .timer_sel  = BUZZER_LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = BUZZER_GPIO,
        .duty       = 0,    // 初始静音
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC通道配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    s_buzzer_initialized = true;
    ESP_LOGI(TAG, "蜂鸣器初始化成功 (GPIO %d)", BUZZER_GPIO);
    return ESP_OK;
}

// ============================================================
// 内部辅助函数
// ============================================================

void buzzer_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    if (!s_buzzer_initialized) return;

    // 设置频率
    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz);
    // 设置占空比 50% (1024/2 = 512 for 10-bit)
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 512);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    buzzer_stop();
}

void buzzer_stop(void)
{
    if (!s_buzzer_initialized) return;
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

// ============================================================
// 预设音效
// ============================================================

void buzzer_beep_key(void)
{
    buzzer_beep(2000, 50);
}

void buzzer_beep_success(void)
{
    buzzer_beep(2000, 100);
    vTaskDelay(pdMS_TO_TICKS(50));
    buzzer_beep(3000, 150);
}

void buzzer_beep_error(void)
{
    buzzer_beep(500, 500);
}

void buzzer_beep_warning(void)
{
    for (int i = 0; i < 3; i++) {
        buzzer_beep(1500, 100);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void buzzer_beep_open(void)
{
    // 上升音调: 1kHz -> 2kHz -> 3kHz
    buzzer_beep(1000, 80);
    vTaskDelay(pdMS_TO_TICKS(30));
    buzzer_beep(2000, 80);
    vTaskDelay(pdMS_TO_TICKS(30));
    buzzer_beep(3000, 120);
}