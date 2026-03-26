/**
 * @file main.c
 * @brief FreeRTOS 多任务示例
 *
 * 功能：
 *   - 创建多个任务，演示并发执行
 *   - 任务优先级和调度
 *   - 任务间通信（信号量、互斥锁）
 *   - 任务状态监控
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "FreeRTOS";

#define LED_GPIO    GPIO_NUM_2
#define STACK_SIZE  2048

/* 互斥锁，保护共享资源 */
static SemaphoreHandle_t s_mutex = NULL;
static int s_shared_counter = 0;

/**
 * @brief 任务1：LED 闪烁（高优先级）
 */
static void task_led_blink(void *pvParameters)
{
    ESP_LOGI(TAG, "任务1 [LED闪烁] 启动，优先级: %d",
             uxTaskPriorityGet(NULL));

    while (1) {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(800));
    }
}

/**
 * @brief 任务2：计数器（中优先级）
 */
static void task_counter(void *pvParameters)
{
    ESP_LOGI(TAG, "任务2 [计数器] 启动，优先级: %d",
             uxTaskPriorityGet(NULL));

    while (1) {
        /* 获取互斥锁，保护共享变量 */
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_shared_counter++;
            int local_count = s_shared_counter;
            xSemaphoreGive(s_mutex);

            ESP_LOGI(TAG, "任务2: 计数器 = %d", local_count);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief 任务3：系统监控（低优先级）
 */
static void task_monitor(void *pvParameters)
{
    ESP_LOGI(TAG, "任务3 [系统监控] 启动，优先级: %d",
             uxTaskPriorityGet(NULL));

    while (1) {
        /* 打印系统信息 */
        ESP_LOGI(TAG, "任务3: 空闲堆内存 = %d bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "任务3: 运行时间 = %lld 秒", esp_timer_get_time() / 1000000);

        /* 读取共享计数器 */
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ESP_LOGI(TAG, "任务3: 共享计数器 = %d", s_shared_counter);
            xSemaphoreGive(s_mutex);
        }

        /* 打印任务列表 */
        char task_list[512];
        vTaskList(task_list);
        ESP_LOGI(TAG, "任务列表:\n名称\t\t状态\t优先级\t栈\tID\n%s", task_list);

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/**
 * @brief 任务4：一次性任务（执行完自动删除）
 */
static void task_one_shot(void *pvParameters)
{
    ESP_LOGI(TAG, "任务4 [一次性] 启动");

    for (int i = 3; i > 0; i--) {
        ESP_LOGI(TAG, "任务4: 倒计时 %d...", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "任务4: 执行完毕，自动删除");
    vTaskDelete(NULL);  // 删除自身
}

void app_main(void)
{
    ESP_LOGI(TAG, "FreeRTOS 多任务示例启动");

    /* 配置 LED */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    /* 创建互斥锁 */
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "互斥锁创建失败");
        return;
    }

    /* 创建任务
     * xTaskCreate(函数, 名称, 栈大小, 参数, 优先级, 句柄)
     * 优先级：数字越大优先级越高，0 为最低
     */
    xTaskCreate(task_led_blink, "led_blink", STACK_SIZE, NULL, 5, NULL);
    xTaskCreate(task_counter,   "counter",   STACK_SIZE, NULL, 3, NULL);
    xTaskCreate(task_monitor,   "monitor",   STACK_SIZE * 2, NULL, 1, NULL);
    xTaskCreate(task_one_shot,  "one_shot",  STACK_SIZE, NULL, 2, NULL);

    ESP_LOGI(TAG, "所有任务已创建");

    /* app_main 可以继续运行或返回 */
    /* 返回后 main task 会被删除，但其他任务继续运行 */
}