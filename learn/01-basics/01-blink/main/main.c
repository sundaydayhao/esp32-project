/**
 * @file main.c
 * @brief LED 闪烁示例 - ESP-IDF 入门第一课
 *
 * 功能：控制 GPIO2 上的 LED 每隔 500ms 闪烁一次
 *
 * 硬件连接：
 *   LED 正极 → GPIO2（通过 220Ω 电阻）
 *   LED 负极 → GND
 *
 * 注意：部分 ESP32 开发板的板载 LED 连接在 GPIO2
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "Blink";

/* LED 连接的 GPIO 引脚 */
#define BLINK_GPIO GPIO_NUM_2

void app_main(void)
{
    ESP_LOGI(TAG, "LED 闪烁示例启动");

    /* 配置 GPIO 为输出模式 */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BLINK_GPIO),  // 选择引脚
        .mode = GPIO_MODE_OUTPUT,               // 输出模式
        .pull_up_en = GPIO_PULLUP_DISABLE,      // 禁用上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  // 禁用下拉
        .intr_type = GPIO_INTR_DISABLE,         // 禁用中断
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "GPIO%d 配置完成，开始闪烁...", BLINK_GPIO);

    int led_state = 0;

    while (1) {
        /* 切换 LED 状态 */
        led_state = !led_state;
        gpio_set_level(BLINK_GPIO, led_state);

        ESP_LOGI(TAG, "LED %s", led_state ? "ON" : "OFF");

        /* 延时 500ms */
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}