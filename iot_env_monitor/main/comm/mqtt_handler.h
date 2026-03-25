/**
 * @file mqtt_handler.h
 * @brief MQTT 通信模块
 *
 * 功能：
 *  - 连接 MQTT Broker（支持 TCP / TLS）
 *  - 定时发布遥测数据（JSON 格式）
 *  - 发布报警事件（QoS 2）
 *  - 订阅命令主题（远程配置/校准/重启）
 *  - 断线自动重连（指数退避）
 *  - 支持 Last Will Testament (LWT)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sensors/sensor_manager.h"
#include "alarm/alarm_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MQTT_STATE_DISCONNECTED = 0,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_ERROR,
} mqtt_state_t;

/**
 * @brief 初始化 MQTT 模块（从 NVS 读取配置）
 */
esp_err_t mqtt_handler_init(void);

/**
 * @brief 启动 MQTT 连接
 */
esp_err_t mqtt_handler_start(void);

/**
 * @brief 停止 MQTT 连接
 */
esp_err_t mqtt_handler_stop(void);

/**
 * @brief 发布遥测数据
 * @param data  环境数据
 */
esp_err_t mqtt_publish_telemetry(const env_data_t *data);

/**
 * @brief 发布报警事件
 * @param event  报警事件
 */
esp_err_t mqtt_publish_alarm(const alarm_event_t *event);

/**
 * @brief 发布设备在线状态
 * @param online  true=上线，false=下线
 */
esp_err_t mqtt_publish_status(bool online);

/**
 * @brief 获取当前连接状态
 */
mqtt_state_t mqtt_handler_get_state(void);

/**
 * @brief MQTT 发布任务（由 main 创建）
 */
void mqtt_task(void *arg);

/**
 * @brief 更新 MQTT 配置并保存到 NVS
 * @param broker_url  Broker 地址
 * @param username    用户名（可为 NULL）
 * @param password    密码（可为 NULL）
 */
esp_err_t mqtt_handler_set_config(const char *broker_url,
                                   const char *username,
                                   const char *password);

#ifdef __cplusplus
}
#endif