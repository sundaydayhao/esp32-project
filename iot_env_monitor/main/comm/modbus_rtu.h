/**
 * @file modbus_rtu.h
 * @brief Modbus RTU 从站实现（RS-485）
 *
 * 支持功能码：
 *  - FC03: 读保持寄存器 (Read Holding Registers)
 *  - FC04: 读输入寄存器 (Read Input Registers)
 *  - FC06: 写单个保持寄存器 (Write Single Register)
 *  - FC10: 写多个保持寄存器 (Write Multiple Registers)
 *
 * 寄存器映射（输入寄存器 40001~）：
 *  40001: 温度 × 100 (int16, °C × 100)
 *  40002: 湿度 × 100 (int16, %RH × 100)
 *  40003: 气压整数部分 (uint16, hPa)
 *  40004: 气压小数部分 × 100 (uint16)
 *  40005: CO₂ 浓度高字 (uint16)
 *  40006: CO₂ 浓度低字 (uint16)
 *  40007: PM1.0 (uint16, µg/m³)
 *  40008: PM2.5 (uint16, µg/m³)
 *  40009: PM10  (uint16, µg/m³)
 *  40010: AQI 等级 (uint16)
 *  40011: 报警状态位图 (uint16)
 *  40012: 传感器健康状态位图 (uint16)
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_REG_TEMPERATURE  0   /* 40001 */
#define MODBUS_REG_HUMIDITY     1   /* 40002 */
#define MODBUS_REG_PRESSURE_I   2   /* 40003 */
#define MODBUS_REG_PRESSURE_F   3   /* 40004 */
#define MODBUS_REG_CO2_H        4   /* 40005 */
#define MODBUS_REG_CO2_L        5   /* 40006 */
#define MODBUS_REG_PM1_0        6   /* 40007 */
#define MODBUS_REG_PM2_5        7   /* 40008 */
#define MODBUS_REG_PM10         8   /* 40009 */
#define MODBUS_REG_AQI          9   /* 40010 */
#define MODBUS_REG_ALARM_BITS   10  /* 40011 */
#define MODBUS_REG_HEALTH_BITS  11  /* 40012 */
#define MODBUS_REG_COUNT        12

/**
 * @brief 初始化 Modbus RTU 从站
 */
esp_err_t modbus_rtu_init(void);

/**
 * @brief 更新输入寄存器数据（由传感器任务调用）
 */
void modbus_rtu_update_registers(void);

/**
 * @brief Modbus RTU 从站任务（由 main 创建）
 */
void modbus_task(void *arg);

#ifdef __cplusplus
}
#endif