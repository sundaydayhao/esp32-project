/**
 * @file sensor_manager.h
 * @brief 传感器管理器
 *
 * 统一管理所有传感器的初始化、采样、数据聚合与健康状态监控。
 * 采用滑动窗口均值滤波消除毛刺，并对连续失败进行自动重试。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 滑动窗口大小 ---- */
#define SENSOR_FILTER_WINDOW    5

/* ---- 传感器健康状态 ---- */
typedef enum {
    SENSOR_STATUS_OK      = 0,
    SENSOR_STATUS_WARN    = 1,  /* 连续失败 < 3 次 */
    SENSOR_STATUS_ERROR   = 2,  /* 连续失败 >= 3 次 */
    SENSOR_STATUS_OFFLINE = 3,  /* 初始化失败 */
} sensor_status_t;

/* ---- 聚合环境数据（对外暴露的唯一数据结构） ---- */
typedef struct {
    /* 温湿度 (SHT31 主，BME280 备) */
    float    temperature;       /* °C  */
    float    humidity;          /* %RH */
    /* 气压与海拔 (BME280) */
    float    pressure;          /* hPa */
    float    altitude;          /* m   */
    /* CO₂ (MH-Z19B) */
    int32_t  co2_ppm;           /* ppm */
    /* 颗粒物 (PMS5003) */
    uint16_t pm1_0;             /* µg/m³ */
    uint16_t pm2_5;             /* µg/m³ */
    uint16_t pm10;              /* µg/m³ */
    uint8_t  aqi_level;
    /* 时间戳 */
    uint32_t timestamp_ms;
    int64_t  unix_time;         /* Unix 时间戳 (s)，NTP 同步后有效 */
    /* 传感器健康状态 */
    sensor_status_t status_sht31;
    sensor_status_t status_bme280;
    sensor_status_t status_mhz19;
    sensor_status_t status_pms5003;
    /* 数据有效标志 */
    bool valid;
} env_data_t;

/**
 * @brief 初始化所有传感器及 I2C/UART 总线
 * @return ESP_OK 表示至少一个传感器初始化成功
 */
esp_err_t sensor_manager_init(void);

/**
 * @brief 执行一次全量采样（阻塞，约 200ms）
 * @param out  输出聚合数据
 * @return ESP_OK / ESP_ERR_*
 */
esp_err_t sensor_manager_sample(env_data_t *out);

/**
 * @brief 获取最新缓存数据（非阻塞，线程安全）
 * @param out  输出聚合数据
 */
void sensor_manager_get_latest(env_data_t *out);

/**
 * @brief 传感器采样任务（由 main 创建）
 */
void sensor_task(void *arg);

/**
 * @brief 获取传感器管理器互斥锁（供外部模块读取数据时使用）
 */
SemaphoreHandle_t sensor_manager_get_mutex(void);

#ifdef __cplusplus
}
#endif