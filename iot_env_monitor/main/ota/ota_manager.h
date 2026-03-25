/**
 * @file ota_manager.h
 * @brief OTA 固件升级管理器
 *
 * 功能：
 *  - 支持 HTTP/HTTPS OTA 升级
 *  - 升级前校验固件完整性（SHA-256）
 *  - 升级失败自动回滚（需 bootloader 支持）
 *  - 支持通过 MQTT 命令触发升级
 *  - 升级进度上报
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_STATE_IDLE       = 0,
    OTA_STATE_CHECKING   = 1,
    OTA_STATE_DOWNLOADING= 2,
    OTA_STATE_VERIFYING  = 3,
    OTA_STATE_APPLYING   = 4,
    OTA_STATE_SUCCESS    = 5,
    OTA_STATE_FAILED     = 6,
} ota_state_t;

typedef struct {
    ota_state_t state;
    uint32_t    bytes_written;
    uint32_t    total_size;
    uint8_t     progress_pct;   /* 0~100 */
    char        error_msg[64];
} ota_status_t;

/**
 * @brief 初始化 OTA 管理器
 */
esp_err_t ota_manager_init(void);

/**
 * @brief 启动 OTA 升级
 * @param url  固件下载 URL
 */
esp_err_t ota_manager_start(const char *url);

/**
 * @brief 获取 OTA 状态
 */
void ota_manager_get_status(ota_status_t *status);

/**
 * @brief 标记当前固件为有效（防止回滚）
 */
esp_err_t ota_manager_mark_valid(void);

/**
 * @brief OTA 任务（由 main 创建，等待触发信号）
 */
void ota_task(void *arg);

#ifdef __cplusplus
}
#endif