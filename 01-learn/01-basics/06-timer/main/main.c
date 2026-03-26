/**
 * @file main.c
 * @brief 定时器示例 - ESP 高精度定时器
 *
 * 功能：
 *   - 使用 esp_timer 创建周期性定时器
 *   - 使用 esp_timer 创建单次定时器
 *   - 演示定时器回调函数的使用
 *
 * esp_timer 特点：
 *   - 基于硬件定时器，精度高（微秒级）
 *   - 支持周期性和单次触发
 *   - 回调在专用任务中执行
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "Timer";

#define LED_GPIO GPIO_NUM_2

static int led_state = 0;
static int periodic_count = 0;

/**
 * @brief 周期性定时器回调函数（每 500ms 触发）
 * 注意：回调函数应尽量简短，避免阻塞
 */
static void periodic_timer_callback(void *arg)
{
    periodic_count++;
    led_state = !led_state;
    gpio_set_level(LED_GPIO, led_state);

    /* 每 10 次打印一次（避免日志过多） */
    if (periodic_count % 10 == 0) {
        int64_t time_since_boot = esp_timer_get_time();
        ESP_LOGI(TAG, "周期定时器触发 %d 次，运行时间: %lld 秒",
                 periodic_count, time_since_boot / 1000000);
    }
}

/**
 * @brief 单次定时器回调函数
 */
static void one_shot_timer_callback(void *arg)
{
    ESP_LOGI(TAG, "单次定时器触发！（3秒后）");
    ESP_LOGI(TAG, "当前时间: %lld 微秒", esp_timer_get_time());
}

void app_main(void)
{
    ESP_LOGI(TAG, "定时器示例启动");

    /* 配置 LED GPIO */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* 创建周期性定时器（每 500ms 触发） */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &periodic_timer_callback,
        .name = "periodic_led"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 500000));  // 500ms = 500000 微秒
    ESP_LOGI(TAG, "周期性定时器已启动（500ms 间隔）");

    /* 创建单次定时器（3秒后触发） */
    const esp_timer_create_args_t one_shot_timer_args = {
        .callback = &one_shot_timer_callback,
        .name = "one_shot"
    };
    esp_timer_handle_t one_shot_timer;
    ESP_ERROR_CHECK(esp_timer_create(&one_shot_timer_args, &one_shot_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(one_shot_timer, 3000000));  // 3秒 = 3000000 微秒
    ESP_LOGI(TAG, "单次定时器已启动（3秒后触发）");

    /* 主循环：每 5 秒打印定时器信息 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        /* 查询定时器状态 */
        uint64_t period;
        esp_timer_get_period(periodic_timer, &period);
        ESP_LOGI(TAG, "周期定时器周期: %llu 微秒", period);
        ESP_LOGI(TAG, "系统运行时间: %lld 毫秒", esp_timer_get_time() / 1000);
    }
}