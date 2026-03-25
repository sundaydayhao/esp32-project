/**
 * @file pms5003.h
 * @brief PMS5003 激光颗粒物传感器驱动
 *
 * 特性：
 *  - 测量 PM1.0 / PM2.5 / PM10 (µg/m³)
 *  - 提供标准颗粒物计数 (0.3~10 µm)
 *  - UART 通信，9600 baud
 *  - 支持主动/被动模式切换
 *  - 支持睡眠/唤醒控制（降低功耗）
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* 标准颗粒物浓度 (µg/m³) */
    uint16_t pm1_0_std;
    uint16_t pm2_5_std;
    uint16_t pm10_std;
    /* 大气环境颗粒物浓度 (µg/m³) */
    uint16_t pm1_0_atm;
    uint16_t pm2_5_atm;
    uint16_t pm10_atm;
    /* 颗粒物计数 (个/0.1L) */
    uint16_t cnt_0_3um;
    uint16_t cnt_0_5um;
    uint16_t cnt_1_0um;
    uint16_t cnt_2_5um;
    uint16_t cnt_5_0um;
    uint16_t cnt_10_0um;
    /* AQI 等级 (0=优, 1=良, 2=轻度, 3=中度, 4=重度, 5=严重) */
    uint8_t  aqi_level;
    bool     valid;
    uint32_t timestamp_ms;
} pms5003_data_t;

/**
 * @brief 初始化 PMS5003
 */
esp_err_t pms5003_init(uart_port_t uart_port, int tx_pin, int rx_pin, int set_pin);

/**
 * @brief 读取颗粒物数据（被动模式单次请求）
 */
esp_err_t pms5003_read(pms5003_data_t *data);

/**
 * @brief 进入睡眠模式（降低功耗）
 */
esp_err_t pms5003_sleep(void);

/**
 * @brief 从睡眠唤醒（需等待 30s 稳定）
 */
esp_err_t pms5003_wakeup(void);

/**
 * @brief 计算 PM2.5 对应的 AQI 等级
 */
uint8_t pms5003_calc_aqi_level(uint16_t pm25_atm);

#ifdef __cplusplus
}
#endif