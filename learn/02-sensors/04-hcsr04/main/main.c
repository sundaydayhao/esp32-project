/**
 * @file main.c
 * @brief HC-SR04 超声波测距传感器示例
 *
 * 功能：
 *   - 使用 HC-SR04 超声波模块测量距离
 *   - 使用 ESP32 高精度定时器测量回波时间
 *   - 测量范围：2cm ~ 400cm
 *
 * 硬件连接：
 *   HC-SR04 VCC  → 5V（注意：HC-SR04 需要 5V 供电）
 *   HC-SR04 TRIG → GPIO5
 *   HC-SR04 ECHO → GPIO18（通过分压电路降至 3.3V）
 *   HC-SR04 GND  → GND
 *
 * 分压电路（5V → 3.3V）：
 *   ECHO → 1kΩ → GPIO18
 *                    ↓
 *                   2kΩ
 *                    ↓
 *                   GND
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

static const char *TAG = "HCSR04";

#define TRIG_GPIO   GPIO_NUM_5
#define ECHO_GPIO   GPIO_NUM_18

/* 声速：340 m/s = 0.034 cm/us */
#define SOUND_SPEED_CM_PER_US   0.034f

/* 超时时间：400cm 对应约 23529us，设置 30000us 超时 */
#define ECHO_TIMEOUT_US         30000

/**
 * @brief 测量距离
 * @return 距离（cm），-1 表示超时或错误
 */
static float measure_distance(void)
{
    /* 发送触发脉冲（10us 高电平） */
    gpio_set_level(TRIG_GPIO, 0);
    ets_delay_us(2);
    gpio_set_level(TRIG_GPIO, 1);
    ets_delay_us(10);
    gpio_set_level(TRIG_GPIO, 0);

    /* 等待 ECHO 变高（超声波发出） */
    int64_t start_wait = esp_timer_get_time();
    while (gpio_get_level(ECHO_GPIO) == 0) {
        if ((esp_timer_get_time() - start_wait) > ECHO_TIMEOUT_US) {
            ESP_LOGW(TAG, "等待 ECHO 高电平超时");
            return -1.0f;
        }
    }

    /* 记录 ECHO 高电平开始时间 */
    int64_t echo_start = esp_timer_get_time();

    /* 等待 ECHO 变低（超声波返回） */
    while (gpio_get_level(ECHO_GPIO) == 1) {
        if ((esp_timer_get_time() - echo_start) > ECHO_TIMEOUT_US) {
            ESP_LOGW(TAG, "ECHO 高电平持续超时（超出测量范围）");
            return -1.0f;
        }
    }

    /* 计算 ECHO 高电平持续时间（微秒） */
    int64_t echo_duration = esp_timer_get_time() - echo_start;

    /* 计算距离：距离 = 时间 × 声速 / 2（来回） */
    float distance = (echo_duration * SOUND_SPEED_CM_PER_US) / 2.0f;

    return distance;
}

void app_main(void)
{
    ESP_LOGI(TAG, "HC-SR04 超声波测距示例启动");

    /* 配置 TRIG 引脚（输出） */
    gpio_config_t trig_conf = {
        .pin_bit_mask = (1ULL << TRIG_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&trig_conf);
    gpio_set_level(TRIG_GPIO, 0);

    /* 配置 ECHO 引脚（输入） */
    gpio_config_t echo_conf = {
        .pin_bit_mask = (1ULL << ECHO_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&echo_conf);

    ESP_LOGI(TAG, "TRIG: GPIO%d, ECHO: GPIO%d", TRIG_GPIO, ECHO_GPIO);
    ESP_LOGI(TAG, "开始测距...");

    while (1) {
        float distance = measure_distance();

        if (distance > 0) {
            ESP_LOGI(TAG, "距离: %.1f cm", distance);

            /* 距离预警 */
            if (distance < 10.0f) {
                ESP_LOGW(TAG, "⚠️  障碍物很近！(%.1f cm)", distance);
            } else if (distance < 30.0f) {
                ESP_LOGI(TAG, "注意：距离较近 (%.1f cm)", distance);
            }
        } else {
            ESP_LOGE(TAG, "测距失败（超出范围或无回波）");
        }

        /* 测量间隔 60ms（HC-SR04 建议最小间隔） */
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}