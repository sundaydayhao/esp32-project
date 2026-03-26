#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// 无人机检测频段（RTL-SDR 可覆盖范围内）
// ---------------------------------------------------------------------------
#define DRONE_BAND_COUNT    6

typedef struct {
    uint32_t    freq_hz;        // 中心频率
    uint32_t    bw_hz;          // 带宽
    const char *name;           // 频段名称
    const char *desc;           // 说明
} drone_band_t;

// 预定义频段
extern const drone_band_t DRONE_BANDS[DRONE_BAND_COUNT];

// ---------------------------------------------------------------------------
// 告警级别
// ---------------------------------------------------------------------------
typedef enum {
    DRONE_ALERT_NONE    = 0,
    DRONE_ALERT_LOW     = 1,    // 弱信号，可能是无人机
    DRONE_ALERT_MEDIUM  = 2,    // 中等信号，疑似无人机
    DRONE_ALERT_HIGH    = 3,    // 强信号，很可能是无人机
} drone_alert_level_t;

// ---------------------------------------------------------------------------
// 检测事件
// ---------------------------------------------------------------------------
#define DRONE_MAX_EVENTS    64

typedef struct {
    uint32_t    timestamp_s;    // 检测时间（秒）
    uint32_t    freq_hz;        // 检测频率
    float       power_db;       // 信号强度（dB）
    float       snr_db;         // 信噪比（dB）
    drone_alert_level_t level;  // 告警级别
    char        band_name[16];  // 频段名称
    bool        active;         // 是否仍在活跃
} drone_event_t;

// ---------------------------------------------------------------------------
// 频段扫描结果
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t    freq_hz;
    float       power_db;       // 当前信号功率（dBFS）
    float       noise_floor_db; // 噪底（dBFS）
    float       snr_db;         // 信噪比
    drone_alert_level_t alert;  // 当前告警级别
    uint32_t    hit_count;      // 累计触发次数
    uint32_t    last_hit_ms;    // 最后触发时间
} band_status_t;

// ---------------------------------------------------------------------------
// 统计
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t    scan_count;         // 扫描次数
    uint32_t    total_alerts;       // 总告警次数
    uint32_t    active_alerts;      // 当前活跃告警
    uint32_t    uptime_s;           // 运行时间
    band_status_t bands[DRONE_BAND_COUNT]; // 各频段状态
} drone_stats_t;

// ---------------------------------------------------------------------------
// 告警阈值配置
// ---------------------------------------------------------------------------
typedef struct {
    float   snr_low_db;     // 低级告警 SNR 阈值（默认 10 dB）
    float   snr_med_db;     // 中级告警 SNR 阈值（默认 20 dB）
    float   snr_high_db;    // 高级告警 SNR 阈值（默认 30 dB）
    uint32_t dwell_ms;      // 每频段驻留时间（默认 200 ms）
} drone_config_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
esp_err_t drone_detector_init(void);
esp_err_t drone_detector_start(void);
void      drone_detector_stop(void);
bool      drone_detector_is_active(void);

void drone_get_stats(drone_stats_t *stats);
void drone_get_events(drone_event_t **events, int *count);
void drone_release_events(void);
void drone_clear_events(void);

esp_err_t drone_set_config(const drone_config_t *cfg);
void      drone_get_config(drone_config_t *cfg);

esp_err_t drone_register_handlers(httpd_handle_t server);
void      drone_ws_broadcast(void);

const char *drone_alert_level_str(drone_alert_level_t level);

#ifdef __cplusplus
}
#endif