#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

// =============================================================================
// FM 接收器 - 基于 RTL-SDR 的 FM 广播解调
// =============================================================================
// 使用 RTL-SDR 接收 FM 广播（87.5 ~ 108 MHz）
// 通过 HTTP 流式传输 PCM 音频，浏览器用 Web Audio API 播放
// =============================================================================

// FM 频率范围
#define FM_DEFAULT_FREQ     100000000   // 100.0 MHz
#define FM_MIN_FREQ          87500000   //  87.5 MHz
#define FM_MAX_FREQ         108000000   // 108.0 MHz

// 音频参数（RTL-SDR 采样率 1 MSPS，抽取 22 倍 → ~45.5 kHz）
#define FM_SAMPLE_RATE      1000000     // RTL-SDR 采样率
#define FM_AUDIO_RATE       44100       // 目标音频采样率
#define FM_DECIMATION       22          // 抽取比 (1000000 / 44100 ≈ 22)

// 音频流块大小
#define FM_AUDIO_CHUNK_SAMPLES  1024                        // 每块采样数
#define FM_AUDIO_CHUNK_BYTES    (FM_AUDIO_CHUNK_SAMPLES * 2) // 每块字节数（int16_t）
#define FM_AUDIO_QUEUE_DEPTH    8                           // 队列深度

/**
 * @brief 初始化 FM 接收器（创建队列和互斥锁）
 * @return ESP_OK 成功
 */
esp_err_t fm_receiver_init(void);

/**
 * @brief 启动 FM 接收
 *        停止 ADS-B 流，将 RTL-SDR 调谐到指定 FM 频率
 * @param freq_hz FM 频率（Hz），例如 100000000 (100 MHz)
 * @return ESP_OK 成功
 */
esp_err_t fm_receiver_start(uint32_t freq_hz);

/**
 * @brief 停止 FM 接收，切换回 ADS-B 模式
 * @return ESP_OK 成功
 */
esp_err_t fm_receiver_stop(void);

/**
 * @brief 切换 FM 频率（不停止接收）
 * @param freq_hz FM 频率（Hz）
 * @return ESP_OK 成功
 */
esp_err_t fm_receiver_set_freq(uint32_t freq_hz);

/**
 * @brief 获取当前 FM 频率（Hz）
 */
uint32_t fm_receiver_get_freq(void);

/**
 * @brief 是否正在接收 FM
 */
bool fm_receiver_is_active(void);

/**
 * @brief 向 HTTP 服务器注册 FM 相关路由
 *        - GET  /fm/audio   → PCM 音频流
 *        - GET  /fm/status  → JSON 状态
 *        - POST /fm/freq    → 设置频率
 *        - POST /fm/start   → 启动 FM
 *        - POST /fm/stop    → 停止 FM
 * @param server HTTP 服务器句柄
 * @return ESP_OK 成功
 */
esp_err_t fm_receiver_register_handlers(httpd_handle_t server);

/**
 * @brief 处理 IQ 数据（由 RTL-SDR IQ 回调调用）
 *        执行 FM 解调并将音频块放入队列
 * @param buf IQ 数据缓冲区（uint8_t，I/Q 交替，127.5 为零点）
 * @param len 缓冲区字节数
 */
void fm_process_iq(const uint8_t *buf, size_t len);