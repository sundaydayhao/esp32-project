/**
 * @file locker.h
 * @brief 格口管理模块
 *
 * 管理每个格口的状态机、舵机控制、门磁检测、取件码验证
 *
 * 格口状态机:
 *
 *   EMPTY ──[存件]──> OCCUPIED ──[正确取件码]──> UNLOCKED ──[关门]──> EMPTY
 *     ^                                              │
 *     └──────────────[管理员重置]────────────────────┘
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// 格口状态定义
// ============================================================

typedef enum {
    LOCKER_STATE_EMPTY    = 0,  ///< 空闲，可存件
    LOCKER_STATE_OCCUPIED = 1,  ///< 已存件，等待取件
    LOCKER_STATE_UNLOCKED = 2,  ///< 已解锁，门已打开
    LOCKER_STATE_LOCKED_OUT = 3,///< 因多次错误被锁定
} locker_state_t;

// ============================================================
// 格口信息结构体
// ============================================================

typedef struct {
    uint8_t         id;                     ///< 格口编号 (0-based)
    locker_state_t  state;                  ///< 当前状态
    char            code[8];                ///< 取件码 (最多6位+'\0')
    bool            door_open;              ///< 门是否打开
    uint8_t         fail_count;             ///< 连续失败次数
    int64_t         deposit_time;           ///< 存件时间戳 (ms)
    int64_t         unlock_time;            ///< 解锁时间戳 (ms)
    int64_t         lockout_until;          ///< 锁定截止时间戳 (ms)
} locker_info_t;

// ============================================================
// 操作结果
// ============================================================

typedef enum {
    LOCKER_OK           = 0,    ///< 操作成功
    LOCKER_ERR_INVALID  = 1,    ///< 无效参数
    LOCKER_ERR_STATE    = 2,    ///< 状态不允许此操作
    LOCKER_ERR_CODE     = 3,    ///< 取件码错误
    LOCKER_ERR_LOCKED   = 4,    ///< 格口被锁定 (多次错误)
    LOCKER_ERR_TIMEOUT  = 5,    ///< 操作超时
} locker_result_t;

// ============================================================
// API
// ============================================================

/**
 * @brief 初始化所有格口
 * @return ESP_OK 成功
 */
esp_err_t locker_init(void);

/**
 * @brief 格口管理任务 (在FreeRTOS任务中运行)
 * @param arg 未使用
 */
void locker_task(void *arg);

/**
 * @brief 获取格口信息
 * @param id   格口编号
 * @param info 输出格口信息
 * @return LOCKER_OK 成功
 */
locker_result_t locker_get_info(uint8_t id, locker_info_t *info);

/**
 * @brief 存件操作 - 分配格口并设置取件码
 * @param id   格口编号 (0xFF = 自动选择空闲格口)
 * @param code 取件码 (NULL = 自动生成)
 * @param out_id   输出实际使用的格口编号
 * @param out_code 输出实际使用的取件码
 * @return LOCKER_OK 成功
 */
locker_result_t locker_deposit(uint8_t id, const char *code,
                                uint8_t *out_id, char *out_code);

/**
 * @brief 取件操作 - 验证取件码并解锁
 * @param code 输入的取件码
 * @param out_id 输出对应的格口编号
 * @return LOCKER_OK 成功, LOCKER_ERR_CODE 取件码错误
 */
locker_result_t locker_pickup(const char *code, uint8_t *out_id);

/**
 * @brief 强制解锁指定格口 (管理员操作)
 * @param id 格口编号
 * @return LOCKER_OK 成功
 */
locker_result_t locker_force_unlock(uint8_t id);

/**
 * @brief 强制锁定指定格口
 * @param id 格口编号
 * @return LOCKER_OK 成功
 */
locker_result_t locker_force_lock(uint8_t id);

/**
 * @brief 重置格口为空闲状态
 * @param id 格口编号
 * @return LOCKER_OK 成功
 */
locker_result_t locker_reset(uint8_t id);

/**
 * @brief 获取空闲格口数量
 * @return 空闲格口数量
 */
uint8_t locker_get_empty_count(void);

/**
 * @brief 检查门是否关闭
 * @param id 格口编号
 * @return true=门关闭, false=门打开
 */
bool locker_is_door_closed(uint8_t id);

#ifdef __cplusplus
}
#endif