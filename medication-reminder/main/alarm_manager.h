#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 常量定义 ==================== */
#define MAX_ALARMS          8       // 最大闹钟数量
#define MAX_MED_NAME_LEN    24      // 药品名称最大长度
#define MAX_DOSAGE_LEN      16      // 剂量最大长度
#define MAX_NOTES_LEN       32      // 备注最大长度
#define SNOOZE_DURATION_MIN 5       // 稍后提醒延迟（分钟）
#define MAX_SNOOZE_COUNT    3       // 最大稍后提醒次数
#define ALARM_TIMEOUT_MIN   10      // 闹钟超时时间（分钟）

/* ==================== 枚举定义 ==================== */

/**
 * @brief 闹钟状态
 */
typedef enum {
    ALARM_STATE_IDLE = 0,       // 空闲（等待触发）
    ALARM_STATE_TRIGGERED,      // 已触发（正在提醒）
    ALARM_STATE_SNOOZED,        // 已稍后提醒
    ALARM_STATE_ACKNOWLEDGED,   // 已确认（已服药）
    ALARM_STATE_MISSED,         // 已错过
} alarm_state_t;

/**
 * @brief 星期位掩码（bit0=周日, bit1=周一, ..., bit6=周六）
 */
typedef enum {
    ALARM_DAY_SUN = (1 << 0),
    ALARM_DAY_MON = (1 << 1),
    ALARM_DAY_TUE = (1 << 2),
    ALARM_DAY_WED = (1 << 3),
    ALARM_DAY_THU = (1 << 4),
    ALARM_DAY_FRI = (1 << 5),
    ALARM_DAY_SAT = (1 << 6),
    ALARM_DAY_ALL = 0x7F,       // 每天
    ALARM_DAY_WEEKDAY = 0x3E,   // 工作日（周一到周五）
    ALARM_DAY_WEEKEND = 0x41,   // 周末（周六、周日）
} alarm_day_t;

/* ==================== 结构体定义 ==================== */

/**
 * @brief 闹钟配置结构体
 */
typedef struct {
    uint8_t  id;                            // 闹钟ID（0-7）
    uint8_t  hour;                          // 小时（0-23）
    uint8_t  minute;                        // 分钟（0-59）
    uint8_t  days_of_week;                  // 重复星期（位掩码）
    char     medication_name[MAX_MED_NAME_LEN]; // 药品名称
    char     dosage[MAX_DOSAGE_LEN];        // 剂量（如"1片"）
    char     notes[MAX_NOTES_LEN];          // 备注（如"饭后服用"）