#pragma once

/**
 * @file imu_sensor.h
 * @brief IMU传感器接口 (MPU6050)
 * 
 * 通过MPU6050获取加速度和陀螺仪数据
 * 用于检测转向、颠簸、急刹车等驾驶行为
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IMU原始数据
 */
typedef struct {
    float accel_x;      // X轴加速度 (g)
    float accel_y;      // Y轴加速度 (g)
    float accel_z;      // Z轴加速度 (g)
    float gyro_x;       // X轴角速度 (°/s)
    float gyro_y;       // Y轴角速度 (°/s)
    float gyro_z;       // Z轴角速度 (°/s)
    float temperature;  // 温度 (°C)
} imu_raw_data_t;

/**
 * @brief 驾驶行为数据
 */
typedef struct {
    float steering_angle;   // 转向角速度 (°/s), 正=右转, 负=左转
    float lateral_g;        // 横向G力 (g)
    float longitudinal_g;   // 纵向G力 (g), 正=加速, 负=制动
    float vertical_g;       // 垂直G力 (g), 用于检测颠簸
    bool is_turning_left;   // 正在左转
    bool is_turning_right;  // 正在右转
    bool is_braking_hard;   // 急刹车
    bool is_bumpy;          // 路面颠簸
    bool imu_valid;         // IMU数据是否有效
} driving_behavior_t;

/**
 * @brief IMU数据回调函数类型
 */
typedef void (*imu_callback_t)(const driving_behavior_t *behavior);

/**
 * @brief 初始化IMU传感器
 * @return ESP_OK 成功
 */
esp_err_t imu_sensor_init(void);

/**
 * @brief 注册IMU数据回调
 * @param cb 回调函数
 */
void imu_sensor_register_callback(imu_callback_t cb);

/**
 * @brief 获取当前驾驶行为数据
 * @param behavior 输出驾驶行为数据
 */
void imu_sensor_get_behavior(driving_behavior_t *behavior);

/**
 * @brief 获取IMU原始数据
 * @param raw 输出原始数据
 */
void imu_sensor_get_raw(imu_raw_data_t *raw);

/**
 * @brief 校准IMU（静止状态下调用）
 */
void imu_sensor_calibrate(void);

#ifdef __cplusplus
}
#endif