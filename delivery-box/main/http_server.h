/**
 * @file http_server.h
 * @brief HTTP REST API 服务器模块
 *
 * 提供以下 REST API 端点:
 *
 *   GET  /api/status              - 系统状态
 *   GET  /api/lockers             - 所有格口状态
 *   GET  /api/lockers/{id}        - 指定格口状态
 *   POST /api/lockers/{id}/deposit - 存件 (分配格口+取件码)
 *   POST /api/lockers/{id}/pickup  - 取件 (验证取件码)
 *   POST /api/lockers/{id}/unlock  - 强制解锁 (管理员)
 *   POST /api/lockers/{id}/lock    - 强制锁定 (管理员)
 *   POST /api/lockers/{id}/reset   - 重置格口 (管理员)
 *   POST /api/wifi                 - 配置WiFi
 *   GET  /                         - Web 管理界面
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 HTTP 服务器
 * @return ESP_OK 成功
 */
esp_err_t http_server_start(void);

/**
 * @brief 停止 HTTP 服务器
 * @return ESP_OK 成功
 */
esp_err_t http_server_stop(void);

#ifdef __cplusplus
}
#endif