/**
 * @file sht31.h
 * @brief SHT31 高精度温湿度传感器驱动
 *
 * 特性：
 *  - 温度精度 ±0.3°C，湿度精度 ±2%RH
 *  - 支持单次测量与周期测量模式
 *  - 内置 CRC-8 校验
 *  - 支持软复位与加热器控制
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 测量结果 ---- */
typedef struct {
    float    temperature;   /* °C  */
    float    humidity;      /* %RH */
    bool     valid;
    uint32_t timestamp_ms;
} sht31_data_t;

/* ---- 重复性模式 ---- */
typedef enum {
    SHT31_REPEAT_HIGH   = 0,
    SHT31_REPEAT_MEDIUM = 1,
    SHT31_REPEAT_LOW    = 2,
} sht31_repeatability_t;

/**
 * @brief 初始化 SHT31（软复位 + 状态寄存器清除）
 * @param i2c_port  I2C 端口号
 * @param i2c_addr  设备地址 (0x44 或 0x45)
 * @return ESP_OK / ESP_ERR_*
 */
esp_err_t sht31_init(i2c_port_t i2c_port, uint8_t i2c_addr);

/**
 * @brief 单次触发测量（阻塞等待结果）
 * @param data  输出数据结构
 * @return ESP_OK / ESP_ERR_*
 */
esp_err_t sht31_read(sht31_data_t *data);

/**
 * @brief 软复位
 */
esp_err_t sht31_soft_reset(void);

/**
 * @brief 控制内置加热器（用于除湿/自检）
 * @param enable  true=开启，false=关闭
 */
esp_err_t sht31_heater_ctrl(bool enable);

/**
 * @brief 读取状态寄存器
 * @param status  输出 16-bit 状态字
 */
esp_err_t sht31_read_status(uint16_t *status);

#ifdef __cplusplus
}
#endif