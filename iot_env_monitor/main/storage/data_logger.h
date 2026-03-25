/**
 * @file data_logger.h
 * @brief 本地数据记录器
 *
 * 功能：
 *  - 将环境数据以二进制格式写入 SPIFFS
 *  - 环形缓冲区，自动覆盖最旧记录
 *  - 支持按时间范围查询历史数据
 *  - 提供 CSV 导出接口（供 Web 服务器下载）
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sensors/sensor_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 单条记录（紧凑二进制格式，32 字节） ---- */
typedef struct __attribute__((packed)) {
    int64_t  unix_time;     /* 8B: Unix 时间戳 (s) */
    int16_t  temperature;   /* 2B: °C × 100 */
    uint16_t humidity;      /* 2B: %RH × 100 */
    uint16_t pressure;      /* 2B: hPa */
    uint16_t co2_ppm;       /* 2B: ppm (截断到 uint16) */
    uint16_t pm1_0;         /* 2B: µg/m³ */
    uint16_t pm2_5;         /* 2B: µg/m³ */
    uint16_t pm10;          /* 2B: µg/m³ */
    uint8_t  aqi_level;     /* 1B */
    uint8_t  alarm_bits;    /* 1B: 低 8 位报警状态 */
    uint8_t  health_bits;   /* 1B: 传感器健康状态 */
    uint8_t  reserved[5];   /* 5B: 对齐填充 */
} log_record_t;             /* 总计 32 字节 */

/**
 * @brief 初始化数据记录器（挂载 SPIFFS）
 */
esp_err_t data_logger_init(void);

/**
 * @brief 写入一条记录
 * @param data  环境数据
 */
esp_err_t data_logger_write(const env_data_t *data);

/**
 * @brief 读取最近 N 条记录
 * @param buf    输出缓冲区
 * @param count  请求条数
 * @param actual 实际读取条数
 */
esp_err_t data_logger_read_latest(log_record_t *buf, uint32_t count, uint32_t *actual);

/**
 * @brief 获取已记录总条数
 */
uint32_t data_logger_get_count(void);

/**
 * @brief 清空所有记录
 */
esp_err_t data_logger_clear(void);

/**
 * @brief 数据记录任务（由 main 创建）
 */
void data_logger_task(void *arg);

#ifdef __cplusplus
}
#endif