/**
 * @file button_handler.cpp
 * @brief 按键处理，支持短按和长按检测
 */

#include "button_handler.h"
#include "app_config.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_timer.h"

static const char *TAG = "BTN";

static button_event_cb_t s_event_cb = NULL;
static TaskHandle_t s_button_task_handle = NULL;
static volatile bool s_button_pressed = false;
static volatile int64_t s_press_time_us = 0;

/**
 * @brief GPIO中断处理函数
 */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    int level = gpio_get_level((gpio_num_t)BUTTON_PIN);
    if (level == BUTTON_ACTIVE_LEVEL) {
        // 按下
        s_button_pressed = true;
        s_press_time_us = esp_timer_get_time();
    } else {
        // 释放
        s_button_pressed = false;
    }

    // 通知按键任务
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (s_button_task_handle) {
        vTaskNotifyGiveFromISR(s_button_task_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/**
 * @brief 按键处理任务
 */
static void button_task(void *arg)
{
    ESP_LOGI(TAG, "按键任务启动");

    bool last_pressed = false;
    bool long_press_fired = false;

    while (1) {
        // 等待中断通知或超时检查长按
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));

        // 消抖
        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));

        int level = gpio_get_level((gpio_num_t)BUTTON_PIN);
        bool pressed = (level == BUTTON_ACTIVE_LEVEL);

        if (pressed && !last_pressed) {
            // 按下事件
            ESP_LOGD(TAG, "按键按下");
            long_press_fired = false;
            if (s_event_cb) s_event_cb(BUTTON_EVENT_PRESS);

        } else if (!pressed && last_pressed) {
            // 释放事件
            ESP_LOGD(TAG, "按键释放");
            if (s_event_cb) s_event_cb(BUTTON_EVENT_RELEASE);

        } else if (pressed && last_pressed && !long_press_fired) {
            // 检查长按
            int64_t held_ms = (esp_timer_get_time() - s_press_time_us) / 1000;
            if (held_ms >= BUTTON_LONG_PRESS_MS) {
                ESP_LOGI(TAG, "长按检测 (%lld ms)", held_ms);
                long_press_fired = true;
                if (s_event_cb) s_event_cb(BUTTON_EVENT_LONG_PRESS);
            }
        }

        last_pressed = pressed;
    }
}

// ============================================================
// 公共接口实现
// ============================================================

esp_err_t button_handler_init(button_event_cb_t event_cb)
{
    ESP_LOGI(TAG, "初始化按键处理器 (GPIO %d)", BUTTON_PIN);

    s_event_cb = event_cb;

    // 配置GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (BUTTON_ACTIVE_LEVEL == 0) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (BUTTON_ACTIVE_LEVEL == 1) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf);

    // 安装GPIO中断服务
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)BUTTON_PIN, gpio_isr_handler, NULL);

    // 创建按键处理任务
    xTaskCreate(button_task, "button_task", 2048, NULL, 5, &s_button_task_handle);

    ESP_LOGI(TAG, "按键处理器初始化完成");
    return ESP_OK;
}

void button_handler_deinit(void)
{
    gpio_isr_handler_remove((gpio_num_t)BUTTON_PIN);
    if (s_button_task_handle) {
        vTaskDelete(s_button_task_handle);
        s_button_task_handle = NULL;
    }
}