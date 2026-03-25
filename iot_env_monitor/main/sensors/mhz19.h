/**
 * @file mhz19.h
 * @brief MH-Z19B NDIR CO₂ 传感器驱动
 *
 * 特性：
 *  - 量程 0~5000 ppm（可配置 0~2000 ppm）
 *  - UART 通信，9600 baud
 *  - 支持自动基线校正 (ABC) 开关
 *  - 支持零点/跨度手动校准
 *  - 内置温度读取
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
    int32_t  co2_ppm;       /* CO₂ 浓度 (ppm) */
    int8_t   temperature;   /* 传感器内部温度 (°C) */
    bool     valid;
    uint32_t timestamp_ms;
} mhz19_data_t;

/**
 * @brief 初始化 MH-Z19B
 * @param uart_port  UART 端口
 * @param tx_pin     TX 引脚
 * @param rx_pin     RX 引脚
 */
esp_err_t mhz19_init(uart_port_t uart_port, int tx_pin, int rx_pin);

/**
 * @brief 读取 CO₂ 浓度
 */
esp_err_t mhz19_read(mhz19_data_t *data);

/**
 * @brief 控制自动基线校正 (ABC)
 * @param enable  true=开启（默认），false=关闭（工业场景建议关闭）
 */
esp_err_t mhz19_set_abc(bool enable);

/**
 * @brief 零点校准（传感器需在 400 ppm 新鲜空气中稳定 20 分钟后执行）
 */
esp_err_t mhz19_calibrate_zero(void);

/**
 * @brief 跨度校准
 * @param span_ppm  已知 CO₂ 浓度 (ppm)，通常为 2000
 */
esp_err_t mhz19_calibrate_span(uint16_t span_ppm);

/**
 * @brief 设置量程
 * @param range  2000 或 5000
 */
esp_err_t mhz19_set_range(uint16_t range);

#ifdef __cplusplus
}
#endif