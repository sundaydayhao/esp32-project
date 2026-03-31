#pragma once

/**
 * @file ds3231.h
 * @brief DS3231 高精度实时时钟（RTC）驱动
 *
 * DS3231 通过 I2C 接口通信，地址为 0x68
 * 支持读写时间、日期，以及温度读取
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 常量定义 ==================== */
#define DS3231_I2C_ADDR         0x68    // DS3231 I2C 地址
#define DS3231_I2C_PORT         I2C_NUM_0
#define DS3231_I2C_FREQ_HZ      100000  // 100kHz

/* DS3231 寄存器地址 */
#define DS3231_REG_SECONDS      0x00
#define DS3231_REG_MINUTES      0x01
#define DS3231_REG_HOURS        0x02
#define DS3231_REG_DAY          0x03
#define DS3231_REG_DATE         0x04
#define DS3231_REG_MONTH        0x05
#define DS3231_REG_YEAR         0x06
#define DS3231_REG_ALARM1_SEC   0x07
#define DS3231_REG_CONTROL      0x0E
#define DS3231_REG_STATUS       0x0F
#define DS3231_REG_TEMP_MSB     0x11
#define DS3231_REG_TEMP_LSB     0x12

/* ==================== 函数声明 ==================== */

/**
 * @brief 初始化 DS3231 RTC
 *
 * @param sda_pin I2C SDA 引脚
 * @param scl_pin I2C SCL 引脚
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t ds3231_init(int sda_pin, int scl_pin);

/**
 * @brief 从 DS3231 读取当前时间
 *
 * @param tm 输出时间结构体指针
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t ds3231_get_time(struct tm *tm);

/**
 * @brief 向 DS3231 写入时间
 *
 * @param tm 要写入的时间结构体指针
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t ds3231_set_time(const struct tm *tm);

/**
 * @brief 读取 DS3231 温度（摄氏度）
 *
 * @param temp 输出温度值（摄氏度）
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t ds3231_get_temperature(float *temp);

/**
 * @brief 检查 DS3231 是否已初始化（时间是否有效）
 *
 * @return true 时间有效
 * @return false 时间无效（需要重新设置）
 */
bool ds3231_is_running(void);

#ifdef __cplusplus
}
#endif