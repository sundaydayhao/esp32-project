/*
 * WiFi CSI 数据采集模块
 * 负责 CSI 数据的采集、缓存和分发
 */

#ifndef WIFI_CSI_H
#define WIFI_CSI_H

#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CSI 数据包最大子载波数量 */
#define CSI_MAX_SUBCARRIERS 128

/* CSI 数据缓冲区大小 */
#define CSI_BUFFER_SIZE 100

/**
 * @brief WiFi CSI 配置结构
 */
typedef struct {
    const char *ssid;           // WiFi SSID
    const char *password;       // WiFi 密码
    uint8_t channel;            // WiFi 信道 (1-13)
    bool enable_csi;            // 是否启用 CSI
} wifi_csi_config_t;

/**
 * @brief CSI 数据包结构
 */
typedef struct {
    uint64_t timestamp_us;      // 时间戳 (微秒)
    int8_t rssi;                // RSSI 值
    uint8_t rate;               // 数据速率
    uint8_t sig_mode;           // 信号模式
    uint8_t mcs;                // MCS 索引
    uint8_t cwb;                // 信道带宽
    uint16_t len;               // CSI 数据长度
    int8_t data[CSI_MAX_SUBCARRIERS * 2];  // CSI 数据 (I/Q)
} csi_data_t;

/**
 * @brief CSI 数据回调函数类型
 * 
 * @param data CSI 数据包指针
 * @param user_ctx 用户上下文
 */
typedef void (*csi_data_callback_t)(const csi_data_t *data, void *user_ctx);

/**
 * @brief 初始化 WiFi CSI 模块
 * 
 * @param config 配置参数
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t wifi_csi_init(const wifi_csi_config_t *config);

/**
 * @brief 启动 CSI 数据采集
 * 
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t wifi_csi_start(void);

/**
 * @brief 停止 CSI 数据采集
 * 
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t wifi_csi_stop(void);

/**
 * @brief 注册 CSI 数据回调函数
 * 
 * @param callback 回调函数
 * @param user_ctx 用户上下文
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t wifi_csi_register_callback(csi_data_callback_t callback, void *user_ctx);

/**
 * @brief 获取最新的 CSI 数据
 * 
 * @param data 输出 CSI 数据
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t wifi_csi_get_latest(csi_data_t *data);

/**
 * @brief 获取 CSI 统计信息
 * 
 * @param total_packets 总接收包数
 * @param dropped_packets 丢弃包数
 */
void wifi_csi_get_stats(uint32_t *total_packets, uint32_t *dropped_packets);

#ifdef __cplusplus
}
#endif

#endif // WIFI_CSI_H