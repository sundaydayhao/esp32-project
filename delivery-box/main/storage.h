/**
 * @file storage.h
 * @brief NVS 持久化存储模块
 *
 * 负责将格口状态、取件码、WiFi配置等数据持久化到 Flash NVS 分区
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 NVS Flash 存储
 * @return ESP_OK 成功
 */
esp_err_t storage_init(void);

/**
 * @brief 保存格口取件码
 * @param locker_id 格口编号 (0 ~ LOCKER_COUNT-1)
 * @param code      取件码字符串 (以'\0'结尾)
 * @return ESP_OK 成功
 */
esp_err_t storage_save_code(uint8_t locker_id, const char *code);

/**
 * @brief 读取格口取件码
 * @param locker_id 格口编号
 * @param code      输出缓冲区
 * @param max_len   缓冲区最大长度
 * @return ESP_OK 成功, ESP_ERR_NVS_NOT_FOUND 未设置
 */
esp_err_t storage_load_code(uint8_t locker_id, char *code, size_t max_len);

/**
 * @brief 清除格口取件码
 * @param locker_id 格口编号
 * @return ESP_OK 成功
 */
esp_err_t storage_clear_code(uint8_t locker_id);

/**
 * @brief 保存格口状态
 * @param locker_id 格口编号
 * @param state     状态值 (0=空闲, 1=已占用, 2=已解锁)
 * @return ESP_OK 成功
 */
esp_err_t storage_save_state(uint8_t locker_id, uint8_t state);

/**
 * @brief 读取格口状态
 * @param locker_id 格口编号
 * @param state     输出状态值
 * @return ESP_OK 成功
 */
esp_err_t storage_load_state(uint8_t locker_id, uint8_t *state);

/**
 * @brief 保存格口存入时间戳
 * @param locker_id 格口编号
 * @param timestamp Unix 时间戳
 * @return ESP_OK 成功
 */
esp_err_t storage_save_timestamp(uint8_t locker_id, int64_t timestamp);

/**
 * @brief 读取格口存入时间戳
 * @param locker_id 格口编号
 * @param timestamp 输出时间戳
 * @return ESP_OK 成功
 */
esp_err_t storage_load_timestamp(uint8_t locker_id, int64_t *timestamp);

/**
 * @brief 保存 WiFi STA 配置
 * @param ssid     WiFi SSID
 * @param password WiFi 密码
 * @return ESP_OK 成功
 */
esp_err_t storage_save_wifi(const char *ssid, const char *password);

/**
 * @brief 读取 WiFi STA 配置
 * @param ssid      输出 SSID 缓冲区
 * @param ssid_len  SSID 缓冲区长度
 * @param password  输出密码缓冲区
 * @param pass_len  密码缓冲区长度
 * @return ESP_OK 成功, ESP_ERR_NVS_NOT_FOUND 未配置
 */
esp_err_t storage_load_wifi(char *ssid, size_t ssid_len,
                             char *password, size_t pass_len);

/**
 * @brief 保存失败尝试次数
 * @param locker_id 格口编号
 * @param count     失败次数
 * @return ESP_OK 成功
 */
esp_err_t storage_save_fail_count(uint8_t locker_id, uint8_t count);

/**
 * @brief 读取失败尝试次数
 * @param locker_id 格口编号
 * @param count     输出失败次数
 * @return ESP_OK 成功
 */
esp_err_t storage_load_fail_count(uint8_t locker_id, uint8_t *count);

#ifdef __cplusplus
}
#endif