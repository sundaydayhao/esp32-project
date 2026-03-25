/**
 * @file main.c
 * @brief PWM 调光示例 - 使用 LEDC 外设
 *
 * 功能：
 *   - 使用 ESP32 的 LEDC（LED Control）外设生成 PWM 信号
 *   - 实现 LED 呼吸灯效果（亮度渐变）
 *   - 演示 PWM 频率和占空比的配置
 *
 * 硬件连接：
 *   LED 正极 → GPIO18（通过 220Ω 电阻）
 *   LED 负极 → GND
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "PWM";

/* PWM 配置参数 */
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO      GPIO_NUM_18
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_13_BIT  // 13位分辨率，0~8191
#define LEDC_FREQUENCY      5000               // 5kHz PWM 频率

/* 最大占空比值（2^13 - 1 = 8191） */
#define LEDC_DUTY_MAX       ((1 << LEDC_DUTY_RES) - 1)

/**
 * @brief 初始化 LEDC PWM
 */
static void ledc_init(void)
{
    /* 配置 LEDC 定时器 */
    ledc_timer_config_t ledc_timer = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz         = LEDC_FREQUENCY,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    /* 配置 LEDC 通道 */
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = LEDC_OUTPUT_IO,
        .duty       = 0,   // 初始占空比为 0
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

/**
 * @brief 设置 LED 亮度（0~100%）
 */
static void set_brightness(uint8_t brightness_percent)
{
    uint32_t duty = (LEDC_DUTY_MAX * brightness_percent) / 100;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "PWM 呼吸灯示例启动");
    ESP_LOGI(TAG, "GPIO%d, 频率: %dHz, 分辨率: %d位",
             LEDC_OUTPUT_IO, LEDC_FREQUENCY, LEDC_DUTY_RES);

    /* 初始化 LEDC */
    ledc_init();

    /* 安装 LEDC 渐变服务（用于硬件渐变） */
    ledc_fade_func_install(0);

    ESP_LOGI(TAG, "开始呼吸灯效果...");

    while (1) {
        /* 方法一：使用硬件渐变（平滑） */
        /* 渐亮：0% → 100%，持续 1000ms */
        ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY_MAX, 1000);
        ledc_fade_start(LEDC_MODE, LEDC_CHANNEL, LEDC_FADE_WAIT_DONE);
        ESP_LOGI(TAG, "渐亮完成");

        /* 渐暗：100% → 0%，持续 1000ms */
        ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL, 0, 1000);
        ledc_fade_start(LEDC_MODE, LEDC_CHANNEL, LEDC_FADE_WAIT_DONE);
        ESP_LOGI(TAG, "渐暗完成");

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}