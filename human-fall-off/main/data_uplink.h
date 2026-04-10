/*
 * 数据上报模块
 * 负责通过 UART/UDP 上报 CSI 数据和事件信息
 */

#ifndef DATA_UPLINK_H
#define DATA_UPLINK_H

#include "esp_err.h"
#include "driver/uart.h"
#include "event_detector.h"
#include "wifi_csi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 数据上报配置
 */
typedef struct {
    uart_port_t uart_num;       // UART 端口号
    uint32_t baud_rate;         // 波特率
    bool enable_udp;            // 是否启用 UDP 上报
    char udp_host[64];          // UDP 目标主机
    uint16_t udp_port;          // UDP 目标端口
} data_uplink_config_t;

/**
 * @brief 初始化数据上报模块
 * 
 * @param config 配置参数
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t data_uplink_init(const data_uplink_config_t *config);

/**
 * @brief 发送 CSI 数据
 * 
 * @param csi_data CSI 数据包
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t data_uplink_send_csi(const csi_data_t *csi_data);

/**
 * @brief 发送跌倒事件
 * 
 * @param event 跌倒事件信息
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t data_uplink_send_event(const fall_event_t *event);

/**
 * @brief 发送文本消息
 * 
 * @param message 消息内容
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t data_uplink_send_message(const char *message);

/**
 * @brief 获取上报统计信息
 * 
 * @param total_sent 总发送数
 * @param total_failed 总失败数
 */
void data_uplink_get_stats(uint32_t *total_sent, uint32_t *total_failed);

#ifdef __cplusplus
}
#endif

#endif // DATA_UPLINK_H