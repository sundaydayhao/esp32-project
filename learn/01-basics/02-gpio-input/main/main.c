/**
 * @file main.c
 * @brief GPIO 输入示例 - 按键控制 LED
 *
 * 功能：
 *   - 使用中断方式检测按键按下
 *   - 按键按下时切换 LED 状态
 *   - 软件消抖处理
 *
 * 硬件连接：
 *   LED:   GPIO2 → 220Ω → LED → GND
 *   按键:  GPIO0 → 按键 → GND（使用内部上拉）
 *
 * 注意：GPIO0 通常是 ESP32 开发板上的 BOOT 按键
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "GPIO_Input";

#define LED_GPIO    GPIO_NUM_2
#define BUTTON_GPIO GPIO_NUM_0

/* 用于中断和任务间通信的队列 */
static QueueHandle_t gpio_evt_queue = NULL;

/**
 * @brief GPIO 中断服务函数（ISR）
 * 注意：ISR 中不能调用非 ISR 安全的函数
 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    /* 将 GPIO 编号发送到队列（从 ISR 发送） */
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

/**
 * @brief 按键处理任务
 */
static void button_task(void *arg)
{
    uint32_t io_num;
    int led_state = 0;
    TickType_t last_press_time = 0;
    const TickType_t debounce_time = pdMS_TO_TICKS(200);  // 200ms 消抖

    while (1) {
        /* 等待队列中的事件 */
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            TickType_t current_time = xTaskGetTickCount();

            /* 软件消抖：忽略 200ms 内的重复触发 */
            if ((current_time - last_press_time) > debounce_time) {
                last_press_time = current_time;

                /* 读取按键状态（低电平表示按下） */
                int button_level = gpio_get_level(io_num);
                ESP_LOGI(TAG, "GPIO%d 状态: %s", io_num,
                         button_level ? "释放" : "按下");

                if (button_level == 0) {  // 按键按下
                    led_state = !led_state;
                    gpio_set_level(LED_GPIO, led_state);
                    ESP_LOGI(TAG, "LED %s", led_state ? "ON" : "OFF");
                }
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "GPIO 输入示例启动");

    /* 配置 LED GPIO（输出） */
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);

    /* 配置按键 GPIO（输入，下降沿中断） */
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,      // 启用内部上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,        // 任意边沿触发
    };
    gpio_config(&btn_conf);

    /* 创建事件队列 */
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    /* 创建按键处理任务 */
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);

    /* 安装 GPIO 中断服务 */
    gpio_install_isr_service(0);

    /* 为按键 GPIO 添加中断处理函数 */
    gpio_isr_handler_add(BUTTON_GPIO, gpio_isr_handler, (void *)BUTTON_GPIO);

    ESP_LOGI(TAG, "按键初始化完成，按下 GPIO%d 按键切换 LED", BUTTON_GPIO);

    /* 主任务可以做其他事情，或者直接删除 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}