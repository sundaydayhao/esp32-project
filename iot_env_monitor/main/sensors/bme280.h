/**
 * @file bme280.h
 * @brief BME280 温湿度气压传感器驱动
 *
 * 特性：
 *  - 气压范围 300~1100 hPa，精度 ±1 hPa
 *  - 温度范围 -40~+85°C，精度 ±1°C
 *  - 湿度精度 ±3%RH（辅助测量，主温湿度由 SHT31 提供）
 *  - 支持强制模式（单次）与正常模式（周期）
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    temperature;   /* °C  */
    float    humidity;      /* %RH */
    float    pressure;      /* hPa */
    float    altitude;      /* m (相对海平面估算) */
    bool     valid;
    uint32_t timestamp_ms;
} bme280_data_t;

/**
 * @brief 初始化 BME280
 */
esp_err_t bme280_init(i2c_port_t i2c_port, uint8_t i2c_addr);

/**
 * @brief 触发单次测量并读取结果（强制模式）
 */
esp_err_t bme280_read(bme280_data_t *data);

/**
 * @brief 软复位
 */
esp_err_t bme280_soft_reset(void);

/**
 * @brief 读取芯片 ID（应为 0x60）
 */
esp_err_t bme280_chip_id(uint8_t *id);

#ifdef __cplusplus
}
#endif