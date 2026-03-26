#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

// =============================================================================
// 频谱仪 - 基于 RTL-SDR 的实时频谱分析
// =============================================================================
// 使用 RTL-SDR 接收 IQ 数据，计算 FFT，通过 WebSocket 推送到浏览器
// 支持切换中心频率（500 kHz ~ 1750 MHz）和带宽（250K/500K/1M/2M MSPS）
// =============================================================================

// FFT 参数
#define SPECTRUM_FFT_SIZE       512     // FFT 点数（2 的幂次）
#define SPECTRUM_UPDATE_MS      100     // 推送间隔（ms），约 10 fps

// 频率范围
#define SPECTRUM_DEFAULT_FREQ   100000000   // 100 MHz
#define SPECTRUM_MIN_FREQ         500000    // 500 kHz
#define SPECTRUM_MAX_FREQ      1750000000   // 1750 MHz

// 支持的带宽（采样率）
#define SPECTRUM_BW_250K    250000
#define SPECTRUM_BW_500K    500000
#define SPECTRUM_BW_1M     1000000
#define SPECTRUM_BW_2M     2000000
#define SPECTRUM_DEFAULT_BW SPECTRUM_BW_1M

// WebSocket 最大客户端数
#define SPECTRUM_WS_MAX_CLIENTS 4

/**
 * @brief 初始化频谱仪（分配 FFT 缓冲区，创建任务）
 */
esp_err_t spectrum_analyzer_init(void);

/**
 * @brief 启动频谱仪
 * @param freq_hz 中心频率（Hz）
 * @param bw_hz   带宽/采样率（Hz），建议使用 SPECTRUM_BW_* 宏
 */
esp_err_t spectrum_analyzer_start(uint32_t freq_hz, uint32_t bw_hz);

/**
 * @brief 停止频谱仪，切换回 ADS-B
 */
esp_err_t spectrum_analyzer_stop(void);

/**
 * @brief 切换中心频率（不停止接收）
 */
esp_err_t spectrum_analyzer_set_freq(uint32_t freq_hz);

/**
 * @brief 切换带宽（需要重启流）
 */
esp_err_t spectrum_analyzer_set_bw(uint32_t bw_hz);

/**
 * @brief 获取当前中心频率（Hz）
 */
uint32_t spectrum_analyzer_get_freq(void);

/**
 * @brief 获取当前带宽（Hz）
 */
uint32_t spectrum_analyzer_get_bw(void);

/**
 * @brief 是否正在运行
 */
bool spectrum_analyzer_is_active(void);

/**
 * @brief 注册 HTTP/WebSocket 路由
 *   GET  /spectrum/ws      → WebSocket，实时推送 FFT 数据（二进制帧）
 *   GET  /spectrum/status  → JSON 状态
 *   POST /spectrum/freq    → 设置中心频率
 *   POST /spectrum/bw      → 设置带宽
 *   POST /spectrum/start   → 启动
 *   POST /spectrum/stop    → 停止
 */
esp_err_t spectrum_analyzer_register_handlers(httpd_handle_t server);

/**
 * @brief 处理 IQ 数据（由 RTL-SDR IQ 回调调用）
 */
void spectrum_process_iq(const uint8_t *buf, size_t len);