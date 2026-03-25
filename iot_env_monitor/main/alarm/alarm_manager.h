/**
 * @file alarm_manager.h
 * @brief 报警管理器
 *
 * 功能：
 *  - 基于阈值的多参数报警检测（含迟滞防抖）
 *  - 报警事件队列（供 MQTT/蜂鸣器/LED 消费）
 *  - 报警状态持久化到 NVS
 *  - 支持运行时动态修改阈值
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "config.h"
#include "sensors/sensor_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 报警 ID ---- */
typedef enum {
    ALARM_ID_TEMP_HIGH    = 0,
    ALARM_ID_TEMP_LOW     = 1,
    ALARM_ID_HUMI_HIGH    = 2,
    ALARM_ID_HUMI_LOW     = 3,
    ALARM_ID_CO2_HIGH     = 4,
    ALARM_ID_PM25_HIGH    = 5,
    ALARM_ID_PM10_HIGH    = 6,
    ALARM_ID_PRES_HIGH    = 7,
    ALARM_ID_PRES_LOW     = 8,
    ALARM_ID_COUNT        = 9,
} alarm_id_t;

/* ---- 报警事件 ---- */
typedef struct {
    alarm_id_t alarm_id;
    char       param_name[16];  /* 参数名称 */
    float      value;           /* 当前值 */
    float      threshold;       /* 触发阈值 */
    bool       active;          /* true=触发，false=恢复 */
    uint8_t    severity;        /* 0=info, 1=warning, 2=critical */
    int64_t    timestamp;       /* Unix 时间戳 (s) */
} alarm_event_t;

/**
 * @brief 初始化报警管理器（从 NVS 加载阈值）
 */
esp_err_t alarm_manager_init(void);

/**
 * @brief 检查环境数据并触发/恢复报警
 * @param data  最新环境数据
 */
void alarm_manager_check(const env_data_t *data);

/**
 * @brief 获取当前活跃报警位图（每位对应一个 alarm_id）
 */
uint32_t alarm_manager_get_active_bits(void);

/**
 * @brief 更新报警阈值并保存到 NVS
 */
esp_err_t alarm_manager_set_thresholds(const alarm_thresholds_t *thresh);

/**
 * @brief 获取当前报警阈值
 */
void alarm_manager_get_thresholds(alarm_thresholds_t *thresh);

/**
 * @brief 报警管理任务（由 main 创建）
 */
void alarm_task(void *arg);

/**
 * @brief 注册报警回调（报警触发/恢复时调用）
 * @param cb  回调函数指针
 */
typedef void (*alarm_callback_t)(const alarm_event_t *event);
void alarm_manager_register_callback(alarm_callback_t cb);

#ifdef __cplusplus
}
#endif