#pragma once

/**
 * @file reminder.h
 * @brief 提醒调度模块 - 管理每日用药提醒
 *
 * 功能：
 * - 支持最多 6 个每日提醒时间点
 * - 每个提醒可配置时间、药物名称、剂量
 * - 提醒状态管理（空闲/触发/升级/已确认/已错过）
 * - 提醒超时升级机制
 */

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 常量定义 ==================== */
#define MAX_REMINDERS           6       // 最大提醒数量
#define MAX_MED_NAME_LEN        25      // 药品名称最大字节数（约8个汉字）
#define MAX_DOSAGE_LEN          17      // 剂量最大字节数
#define REMINDER_TIMEOUT_SEC    (5 * 60)    // 提醒超时（5分钟）→ 升级
#define REMINDER_ESCALATE_SEC   (5 * 60)    // 升级超时（5分钟）→ 错过

/* ==================== 枚举定义 ==================== */

/**
 * @brief 提醒状态
 */
typedef enum {
    REMINDER_STATE_IDLE = 0,        // 空闲（等待触发）
    REMINDER_STATE_TRIGGERED,       // 已触发（正常提醒：蜂鸣+闪烁）
    REMINDER_STATE_ESCALATED,       // 已升级（持续蜂鸣）
    REMINDER_STATE_CONFIRMED,       // 已确认（已服药）
    REMINDER_STATE_MISSED,          // 已错过（超时未确认）
} reminder_state_t;

/* ==================== 结构体定义 ==================== */

/**
 * @brief 提醒配置结构体
 */
typedef struct {
    uint8_t  id;                            // 提醒ID（0-5）
    uint8_t  hour;                          // 小时（0-23）
    uint8_t  minute;                        // 分钟（0-59）
    char     medication_name[MAX_MED_NAME_LEN]; // 药品名称（UTF-8）
    char     dosage[MAX_DOSAGE_LEN];        // 剂量描述（如"1片"）
    bool     enabled;                       // 是否启用
} reminder_config_t;

/**
 * @brief 提醒运行时状态
 */
typedef struct {
    reminder_config_t config;       // 配置信息
    reminder_state_t  state;        // 当前状态
    time_t            trigger_time; // 触发时间戳
    time_t            last_checked; // 上次检查时间（防重复触发）
} reminder_t;

/* ==================== 函数声明 ==================== */

/**
 * @brief 初始化提醒模块（从 NVS 加载配置）
 *
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t reminder_init(void);

/**
 * @brief 检查当前时间是否需要触发提醒
 *
 * 应在每分钟调用一次
 * @param current_time 当前时间
 */
void reminder_check(const struct tm *current_time);

/**
 * @brief 检查提醒超时状态（升级/错过）
 *
 * 应每秒调用一次
 */
void reminder_check_timeout(void);

/**
 * @brief 确认当前触发的提醒（已服药）
 *
 * @return true 确认成功
 * @return false 没有触发中的提醒
 */
bool reminder_confirm(void);

/**
 * @brief 获取当前触发的提醒
 *
 * @return reminder_t* 触发中的提醒，NULL 表示无
 */
reminder_t *reminder_get_active(void);

/**
 * @brief 获取下一个即将触发的提醒
 *
 * @param current_time 当前时间
 * @return reminder_t* 下一个提醒，NULL 表示无
 */
reminder_t *reminder_get_next(const struct tm *current_time);

/**
 * @brief 获取所有提醒配置
 *
 * @param count 输出提醒数量
 * @return reminder_t* 提醒数组指针
 */
reminder_t *reminder_get_all(uint8_t *count);

/**
 * @brief 设置提醒配置
 *
 * @param id 提醒ID（0-5）
 * @param config 配置信息
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t reminder_set_config(uint8_t id, const reminder_config_t *config);

/**
 * @brief 获取提醒配置
 *
 * @param id 提醒ID（0-5）
 * @param config 输出配置信息
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t reminder_get_config(uint8_t id, reminder_config_t *config);

/**
 * @brief 获取提醒状态名称字符串
 *
 * @param state 状态
 * @return const char* 状态名称
 */
const char *reminder_state_str(reminder_state_t state);

#ifdef __cplusplus
}
#endif