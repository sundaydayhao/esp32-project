/**
 * @file web_server.h
 * @brief HTTP Web 服务器（REST API + 内嵌仪表盘）
 *
 * REST API 端点：
 *  GET  /api/data          — 获取最新环境数据 (JSON)
 *  GET  /api/history?n=N   — 获取最近 N 条历史记录 (JSON)
 *  GET  /api/alarms        — 获取报警状态 (JSON)
 *  GET  /api/thresholds    — 获取报警阈值 (JSON)
 *  POST /api/thresholds    — 设置报警阈值 (JSON body)
 *  GET  /api/config        — 获取设备配置 (JSON)
 *  POST /api/config        — 更新设备配置 (JSON body)
 *  POST /api/ota           — 触发 OTA 升级 (JSON: {"url":"..."})
 *  POST /api/reboot        — 重启设备
 *  POST /api/calibrate     — 传感器校准命令
 *  GET  /                  — 内嵌 HTML 仪表盘
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 Web 服务器
 */
esp_err_t web_server_start(void);

/**
 * @brief 停止 Web 服务器
 */
esp_err_t web_server_stop(void);

#ifdef __cplusplus
}
#endif