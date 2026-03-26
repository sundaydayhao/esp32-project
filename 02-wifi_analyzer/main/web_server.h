#pragma once

#include "esp_err.h"
#include "wifi_scanner.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 HTTP 服务器
 *        路由：GET /        → Web 可视化页面
 *              GET /api/scan → JSON 格式扫描结果
 * @return ESP_OK 成功
 */
esp_err_t web_server_init(void);

/**
 * @brief 更新服务器持有的扫描数据（线程安全）
 *        由扫描任务在每次扫描完成后调用
 * @param result  最新扫描结果
 */
void web_server_update_data(const wifi_scan_result_t *result);

#ifdef __cplusplus
}
#endif