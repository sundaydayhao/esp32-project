/**
 * @file main.c
 * @brief FreeRTOS 队列示例
 *
 * 功能：
 *   - 生产者-消费者模式
 *   - 使用队列在任务间传递数据
 *   - 演示队列满/空的处理
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "Queue";

#define QUEUE_SIZE  10

/* 传感器数据结构 */
typedef struct {
    int id;
    float temperature;
    float humidity;
    int64_t timestamp;
} sensor_data_t;

static QueueHandle_t s_data_queue = NULL;

/**
 * @brief 生产者任务：模拟传感器采集数据
 */
static void producer_task(void *pvParameters)
{
    ESP_LOGI(TAG, "生产者任务启动");
    int id = 0;

    while (1) {
        sensor_data_t data = {
            .id          = id++,
            .temperature = 20.0f + (id % 20),
            .humidity    = 50.0f + (id % 30),
            .timestamp   = esp_timer_get_time() / 1000,
        };

        /* 发送数据到队列（等待最多 100ms） */
        if (xQueueSend(s_data_queue, &data, pdMS_TO_TICKS(100)) == pdTRUE) {
            ESP_LOGI(TAG, "生产者: 发送数据 #%d (队列剩余: %d/%d)",
                     data.id,
                     uxQueueSpacesAvailable(s_data_queue),
                     QUEUE_SIZE);
        } else {
            ESP_LOGW(TAG, "生产者: 队列已满，数据 #%d 丢弃", data.id);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * @brief 消费者任务：处理传感器数据
 */
static void consumer_task(void *pvParameters)
{
    ESP_LOGI(TAG, "消费者任务启动");
    sensor_data_t data;

    while (1) {
        /* 从队列接收数据（永久等待） */
        if (xQueueReceive(s_data_queue, &data, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "消费者: 处理数据 #%d: 温度=%.1f°C 湿度=%.1f%% 时间=%lldms",
                     data.id, data.temperature, data.humidity, data.timestamp);

            /* 模拟数据处理耗时 */
            vTaskDelay(pdMS_TO_TICKS(800));
        }
    }
}

/**
 * @brief 队列监控任务
 */
static void monitor_task(void *pvParameters)
{
    while (1) {
        UBaseType_t waiting = uxQueueMessagesWaiting(s_data_queue);
        UBaseType_t spaces  = uxQueueSpacesAvailable(s_data_queue);
        ESP_LOGI(TAG, "队列状态: 等待处理=%d, 空闲=%d", waiting, spaces);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "FreeRTOS 队列示例启动");

    /* 创建队列 */
    s_data_queue = xQueueCreate(QUEUE_SIZE, sizeof(sensor_data_t));
    if (s_data_queue == NULL) {
        ESP_LOGE(TAG, "队列创建失败");
        return;
    }
    ESP_LOGI(TAG, "队列创建成功，容量: %d", QUEUE_SIZE);

    /* 创建任务 */
    xTaskCreate(producer_task, "producer", 2048, NULL, 5, NULL);
    xTaskCreate(consumer_task, "consumer", 2048, NULL, 4, NULL);
    xTaskCreate(monitor_task,  "monitor",  2048, NULL, 1, NULL);
}