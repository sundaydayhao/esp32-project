#pragma once

/**
 * @file storage.h
 * @brief 存储模块 - NVS 配置存储 + SPIFFS 日志记录
 *
 * 功能：
 * - NVS：存储提醒配置（断电不丢失）
 * - SPIFFS：存储服药历史记录（JSON格式）
 */

#include <stdint.h>
#include <stdbool.h>
#include "reminder.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 常量定义 ==================== */
#define LOG_FILE_PATH       "/spiffs/med_log.json"
#define MAX_LOG_ENTRIES     100     // 最多保存100条记录
#define LOG_ENTRY_MAX_LEN   128     // 每条记录最大长度

/* ==================== 函数声明 ==================== */

/**
 * @brief 初始化存储模块（NVS + SPIFFS）
 *
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t storage_init(void);

/**
 * @brief 从 NVS 加载提醒配置
 *
 * @param reminders 输出提醒数组
 * @param count 提醒数量
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t storage_load_reminders(reminder_t *reminders, uint8_t count);

/**
 * @brief 保存提醒配置到 NVS
 *
 * @param reminders 提醒数组
 * @param count 提醒数量
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t storage_save_reminders(const reminder_t *reminders, uint8_t count);

/**
 * @brief 记录服药日志到 SPIFFS
 *
 * @param datetime_str 日期时间字符串 "YYYY-MM-DD HH:MM"
 * @param medication_name 药品名称
 * @param dosage 剂量
 * @param status 状态 ("taken" / "missed")
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t storage_log_medication(const char *datetime_str,
                                  const char *medication_name,
                                  const char *dosage,
                                  const char *status);

/**
 * @brief 读取服药历史记录
 *
 * @param buf 输出缓冲区（JSON 数组格式）
 * @param buf_len 缓冲区长度
 * @param max_entries 最多读取条数（0=全部）
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t storage_read_log(char *buf, size_t buf_len, int max_entries);

/**
 * @brief 清除所有服药历史记录
 *
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t storage_clear_log(void);

/**
 * @brief 保存 WiFi 配置到 NVS
 *
 * @param ssid WiFi SSID
 * @param password WiFi 密码
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t storage_save_wifi(const char *ssid, const char *password);

/**
 * @brief 从 NVS 加载 WiFi 配置
 *
 * @param ssid 输出 SSID 缓冲区
 * @param ssid_len SSID 缓冲区长度
 * @param password 输出密码缓冲区
 * @param pass_len 密码缓冲区长度
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t storage_load_wifi(char *ssid, size_t ssid_len,
                             char *password, size_t pass_len);

#ifdef __cplusplus
}
#endif