#pragma once

/**
 * @file time_manager.h
 * @brief 时间管理模块 - DS3231 RTC + NTP 时间同步
 *
 * 功能：
 * - 上电后通过 NTP 同步时间并写入 DS3231
 * - 断网/断电后从 DS3231 读取时间
 * - 提供格式化时间字符串
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 常量定义 ==================== */
#define TIME_STR_LEN        32
#define DATE_STR_LEN        32
#define WEEKDAY_STR_LEN     8

/* ==================== 枚举定义 ==================== */

typedef enum {
    TIME_SOURCE_NONE = 0,   // 未同步
    TIME_SOURCE_RTC,        // 来自 DS3231 RTC
    TIME_SOURCE_NTP,        // 来自 NTP 服务器
} time_source_t;

/* ==================== 函数声明 ==================== */

/**
 * @brief 初始化时间管理模块
 *
 * 初始化 DS3231 RTC，尝试从 RTC 读取时间
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t time_manager_init(void);

/**
 * @brief 启动 NTP 时间同步
 *
 * 连接 NTP 服务器同步时间，成功后写入 DS3231
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t time_manager_sync_ntp(void);

/**
 * @brief 获取当前时间
 *
 * @param tm 输出时间结构体
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t time_manager_get_time(struct tm *tm);

/**
 * @brief 获取当前时间戳（Unix 时间）
 *
 * @return time_t 时间戳
 */
time_t time_manager_get_timestamp(void);

/**
 * @brief 获取时间来源
 *
 * @return time_source_t 时间来源
 */
time_source_t time_manager_get_source(void);

/**
 * @brief 检查时间是否已同步
 *
 * @return true 已同步
 */
bool time_manager_is_synced(void);

/**
 * @brief 格式化时间字符串 "HH:MM:SS"
 *
 * @param buf 输出缓冲区
 * @param len 缓冲区长度
 */
void time_manager_get_time_str(char *buf, size_t len);

/**
 * @brief 格式化日期字符串 "YYYY-MM-DD"
 *
 * @param buf 输出缓冲区
 * @param len 缓冲区长度
 */
void time_manager_get_date_str(char *buf, size_t len);

/**
 * @brief 获取星期字符串（中文）
 *
 * @param buf 输出缓冲区
 * @param len 缓冲区长度
 */
void time_manager_get_weekday_str(char *buf, size_t len);

/**
 * @brief 获取完整日期时间字符串 "YYYY-MM-DD HH:MM:SS"
 *
 * @param buf 输出缓冲区
 * @param len 缓冲区长度
 */
void time_manager_get_datetime_str(char *buf, size_t len);

#ifdef __cplusplus
}
#endif