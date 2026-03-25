/**
 * @file dht11.h
 * @brief DHT11 温湿度传感器驱动头文件
 */

#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DHT11 读取结果结构体
 */
typedef struct {
    float temperature;  // 温度（摄氏度）
    float humidity;     // 湿度（%RH）
} dht11_data_t;

/**
 * @brief 初始化 DHT11
 * @param gpio_num GPIO 引脚号
 */
void dht11_init(gpio_num_t gpio_num);

/**
 * @brief 读取 DHT11 数据
 * @param data 输出数据结构体指针
 * @return ESP_OK 成功，ESP_FAIL 失败
 */
esp_err_t dht11_read(dht11_data_t *data);

#ifdef __cplusplus
}
#endif