/**
 * @file main.c
 * @brief DHT11 温湿度传感器示例
 *
 * 功能：每 2 秒读取一次温湿度数据并打印
 *
 * 硬件连接：
 *   DHT11 VCC → 3.3V
 *   DHT11 DATA → GPIO4（通过 10kΩ 上拉电阻到 3.3V）
 *   DHT11 GND → GND
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "dht11.h"

static const char *TAG = "DHT11_Main";

#define DHT11_GPIO GPIO_NUM_4

void app_main(void)
{
    ESP_LOGI(TAG, "DHT11 温湿度传感器示例启动");

    /* 初始化 DHT11 */
    dht11_init(DHT11_GPIO);

    dht11_data_t sensor_data;
    int error_count = 0;

    while (1) {
        esp_err_t ret = dht11_read(&sensor_data);

        if (ret == ESP_OK) {
            error_count = 0;
            ESP_LOGI(TAG, "温度: %.1f°C  湿度: %.1f%%RH",
                     sensor_data.temperature, sensor_data.humidity);

            /* 温度预警 */
            if (sensor_data.temperature > 35.0f) {
                ESP_LOGW(TAG, "⚠️  温度过高！");
            }
            if (sensor_data.humidity > 80.0f) {
                ESP_LOGW(TAG, "⚠️  湿度过高！");
            }
        } else {
            error_count++;
            ESP_LOGE(TAG, "读取失败（连续失败 %d 次）", error_count);
        }

        /* DHT11 最小采样间隔为 1 秒，建议 2 秒 */
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}