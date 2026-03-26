#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// =============================================================================
// RTL-SDR (RTL2832U + R820T2) USB Host 驱动
// =============================================================================
// ESP32-S3 作为 USB Host，直接驱动 RTL-SDR
//
// 硬件连接：
//   RTL-SDR USB 接口          ESP32-S3
//   ─────────────────────────────────────
//   USB D+  (绿线)    ──→    GPIO20 (USB_D+)
//   USB D-  (白线)    ──→    GPIO19 (USB_D-)
//   USB GND (黑线)    ──→    GND
//   USB VBUS(红线)    ──→    5V (外部供电，不要接 ESP32-S3 3.3V)
//
// 注意：ESP32-S3 USB OTG 为 Full Speed (12 Mbps)
//       RTL-SDR 将以 Full Speed 模式工作，采样率限制在 ~1 MSPS
//
// RTL-SDR 支持的 VID/PID：
//   0x0BDA:0x2838 (RTL2838)
//   0x0BDA:0x2832 (RTL2832U)
//   0x0BDA:0x2837 (RTL2837)
// =============================================================================

#define RTLSDR_VID          0x0BDA
#define RTLSDR_PID_2838     0x2838
#define RTLSDR_PID_2832     0x2832
#define RTLSDR_PID_2837     0x2837

#define RTLSDR_XTAL_FREQ    28800000    // 28.8 MHz 晶振
#define RTLSDR_IF_FREQ      0           // 零中频
#define RTLSDR_SAMPLE_RATE  1000000     // 1 MSPS (USB Full Speed 限制)
#define RTLSDR_ADSB_FREQ    1090000000  // 1090 MHz

/**
 * @brief RTL-SDR 状态
 */
typedef enum {
    RTLSDR_STATE_DISCONNECTED = 0,
    RTLSDR_STATE_CONNECTED,
    RTLSDR_STATE_CONFIGURED,
    RTLSDR_STATE_STREAMING,
} rtlsdr_state_t;

/**
 * @brief IQ 数据回调函数类型
 *        当有新的 IQ 数据到达时被调用（在 USB 任务上下文中）
 *
 * @param buf  IQ 数据缓冲区（uint8_t，I/Q 交替，127.5 为零点）
 * @param len  缓冲区字节数
 */
typedef void (*rtlsdr_iq_callback_t)(const uint8_t *buf, size_t len);

/**
 * @brief 初始化 RTL-SDR USB Host 驱动
 *        启动 USB Host 任务，等待 RTL-SDR 插入
 *
 * @return ESP_OK 成功
 */
esp_err_t rtlsdr_init(void);

/**
 * @brief 设置中心频率
 *
 * @param freq_hz 频率（Hz），例如 1090000000 (1090 MHz)
 * @return ESP_OK 成功
 */
esp_err_t rtlsdr_set_freq(uint32_t freq_hz);

/**
 * @brief 开始 IQ 数据流
 *
 * @return ESP_OK 成功
 */
esp_err_t rtlsdr_start_streaming(void);

/**
 * @brief 停止数据流
 */
void rtlsdr_stop_streaming(void);

/**
 * @brief 获取当前状态
 */
rtlsdr_state_t rtlsdr_get_state(void);

/**
 * @brief 获取当前频率（Hz）
 */
uint32_t rtlsdr_get_freq(void);

/**
 * @brief 设置 IQ 数据回调函数
 *        设置后，每次收到 IQ 数据时都会调用此回调。
 *        传入 NULL 则恢复默认的 ADS-B 解码处理。
 *
 * @param cb  回调函数指针，NULL 表示使用内置 ADS-B 解码
 */
void rtlsdr_set_iq_callback(rtlsdr_iq_callback_t cb);