/**
 * @file ntp_client.h
 * @brief NTP 时间同步客户端
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 SNTP 并启动同步（需在 WiFi 连接后调用）
 *        自动设置时区为 CST-8（北京时间）
 */
void ntp_client_init(void);

/**
 * @brief 阻塞等待 NTP 同步完成
 * @param timeout_ms 超时毫秒数
 * @return true=同步成功，false=超时
 */
bool ntp_client_wait_sync(uint32_t timeout_ms);

/**
 * @brief 是否已完成时间同步
 */
bool ntp_client_is_synced(void);

/**
 * @brief 获取当前本地时间（已应用 CST-8 时区）
 * @param tm_out  输出的 struct tm
 */
void ntp_client_get_time(struct tm *tm_out);

/**
 * @brief NTP 周期同步任务（由 main 创建）
 *        每隔 CONFIG_NTP_SYNC_INTERVAL_MIN 分钟重新同步一次
 */
void ntp_task(void *arg);

#ifdef __cplusplus
}
#endif