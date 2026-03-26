#pragma once

/**
 * @file speed_sensor.h
 * @brief 速度传感器接口 (GPS NMEA 解析)
 * 
 * 支持通过GPS模块获取实时车速
 * 同时提供模拟速度模式用于测试
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 速度数据结构
 */
typedef struct {
    float speed_kmh;        // 当前速度 (km/h)
    float speed_ms;         // 当前速度 (m/s)
    float acceleration;     // 加速度 (km/h/s), 正=加速, 负=减速
    float max_speed_kmh;    // 本次行程最高速度
    float avg_speed_kmh;    // 本次行程平均速度
    uint32_t distance_m;    // 本次行程里程 (米)
    bool gps_valid;         // GPS信号是否有效
    bool is_simulated;      // 是否为模拟数据
} speed_data_t;

/**
 * @brief 速度变化回调函数类型
 */
typedef void (*speed_callback_t)(const speed_data_t *data);

/**
 * @brief 初始化速度传感器
 * 
 * 初始化GPS UART，如果GPS不可用则启用模拟模式
 * 
 * @return ESP_OK 成功
 */
esp_err_t speed_sensor_init(void);

/**
 * @brief 注册速度变化回调
 * @param cb 回调函数
 */
void speed_sensor_register_callback(speed_callback_t cb);

/**
 * @brief 获取当前速度数据
 * @param data 输出速度数据
 */
void speed_sensor_get_data(speed_data_t *data);

/**
 * @brief 启用/禁用模拟模式
 * @param enable true=启用模拟, false=使用GPS
 */
void speed_sensor_set_simulation(bool enable);

/**
 * @brief 重置行程数据
 */
void speed_sensor_reset_trip(void);

#ifdef __cplusplus
}
#endif