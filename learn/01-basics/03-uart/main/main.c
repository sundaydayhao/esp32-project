/**
 * @file main.c
 * @brief UART 串口通信示例
 *
 * 功能：
 *   - 配置 UART2（GPIO16/17）进行串口通信
 *   - 发送数据并接收回显
 *   - 使用 UART 事件队列处理接收数据
 *
 * 硬件连接（使用 UART2）：
 *   TX: GPIO17 → 对端 RX
 *   RX: GPIO16 → 对端 TX
 *   GND → 对端 GND
 *
 * 也可以通过 USB 串口（UART0）直接在电脑上测试
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "UART";

/* UART 配置参数 */
#define UART_PORT_NUM      UART_NUM_2
#define UART_BAUD_RATE     115200
#define UART_TX_PIN        GPIO_NUM_17
#define UART_RX_PIN        GPIO_NUM_16
#define UART_BUF_SIZE      1024
#define UART_QUEUE_SIZE    20

static QueueHandle_t uart_queue;

/**
 * @brief UART 事件处理任务
 */
static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t *data = (uint8_t *)malloc(UART_BUF_SIZE);

    while (1) {
        /* 等待 UART 事件 */
        if (xQueueReceive(uart_queue, (void *)&event, portMAX_DELAY)) {
            bzero(data, UART_BUF_SIZE);

            switch (event.type) {
            case UART_DATA:
                /* 接收到数据 */
                uart_read_bytes(UART_PORT_NUM, data, event.size, portMAX_DELAY);
                ESP_LOGI(TAG, "接收到 %d 字节: %s", event.size, (char *)data);

                /* 回显数据 */
                uart_write_bytes(UART_PORT_NUM, (const char *)data, event.size);
                break;

            case UART_FIFO_OVF:
                ESP_LOGW(TAG, "FIFO 溢出");
                uart_flush_input(UART_PORT_NUM);
                xQueueReset(uart_queue);
                break;

            case UART_BUFFER_FULL:
                ESP_LOGW(TAG, "环形缓冲区满");
                uart_flush_input(UART_PORT_NUM);
                xQueueReset(uart_queue);
                break;

            case UART_BREAK:
                ESP_LOGI(TAG, "检测到 UART Break");
                break;

            case UART_PARITY_ERR:
                ESP_LOGE(TAG, "奇偶校验错误");
                break;

            case UART_FRAME_ERR:
                ESP_LOGE(TAG, "帧错误");
                break;

            default:
                ESP_LOGI(TAG, "UART 事件类型: %d", event.type);
                break;
            }
        }
    }

    free(data);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "UART 示例启动");

    /* UART 配置 */
    uart_config_t uart_config = {
        .baud_rate  = UART_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* 安装 UART 驱动，并获取事件队列 */
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM,
                                        UART_BUF_SIZE * 2,
                                        UART_BUF_SIZE * 2,
                                        UART_QUEUE_SIZE,
                                        &uart_queue, 0));

    /* 配置 UART 参数 */
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));

    /* 设置 UART 引脚 */
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM,
                                  UART_TX_PIN,
                                  UART_RX_PIN,
                                  UART_PIN_NO_CHANGE,
                                  UART_PIN_NO_CHANGE));

    /* 创建 UART 事件处理任务 */
    xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL, 12, NULL);

    ESP_LOGI(TAG, "UART%d 初始化完成，波特率: %d", UART_PORT_NUM, UART_BAUD_RATE);
    ESP_LOGI(TAG, "TX: GPIO%d, RX: GPIO%d", UART_TX_PIN, UART_RX_PIN);

    /* 主循环：每隔 2 秒发送一条消息 */
    int count = 0;
    while (1) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Hello from ESP32! count=%d\r\n", count++);
        uart_write_bytes(UART_PORT_NUM, msg, strlen(msg));
        ESP_LOGI(TAG, "发送: %s", msg);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}