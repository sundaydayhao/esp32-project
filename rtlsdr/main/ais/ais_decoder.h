#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// AIS 配置
// ---------------------------------------------------------------------------
#define AIS_CH_A_FREQ       161975000   // 161.975 MHz  VHF CH 87B
#define AIS_CH_B_FREQ       162025000   // 162.025 MHz  VHF CH 88B
#define AIS_SAMPLE_RATE     250000      // 250 kHz 采样率
#define AIS_BAUD_RATE       9600        // 9600 baud GMSK
#define AIS_MAX_VESSELS     64          // 最大跟踪船只数
#define AIS_VESSEL_TIMEOUT  600         // 船只超时时间（秒）

// ---------------------------------------------------------------------------
// 导航状态
// ---------------------------------------------------------------------------
#define AIS_NAV_UNDERWAY_ENGINE     0
#define AIS_NAV_AT_ANCHOR           1
#define AIS_NAV_NOT_UNDER_COMMAND   2
#define AIS_NAV_RESTRICTED          3
#define AIS_NAV_CONSTRAINED_DRAUGHT 4
#define AIS_NAV_MOORED              5
#define AIS_NAV_AGROUND             6
#define AIS_NAV_FISHING             7
#define AIS_NAV_UNDERWAY_SAILING    8

// ---------------------------------------------------------------------------
// 船只数据结构
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t mmsi;              // 海上移动业务标识
    char     name[21];          // 船名（20字符）
    char     callsign[8];       // 呼号（7字符）
    char     destination[21];   // 目的港（20字符）

    float    lat;               // 纬度（度，北正南负）
    float    lon;               // 经度（度，东正西负）
    bool     pos_valid;         // 位置有效

    float    sog;               // 对地速度（节）
    float    cog;               // 对地航向（度）
    uint16_t heading;           // 真航向（度，511=不可用）
    int8_t   rot;               // 转向率（度/分，-128=不可用）

    uint8_t  nav_status;        // 导航状态
    uint8_t  ship_type;         // 船舶类型
    uint32_t imo;               // IMO编号

    uint16_t dim_bow;           // 船首到天线距离（米）
    uint16_t dim_stern;         // 船尾到天线距离（米）
    uint8_t  dim_port;          // 左舷到天线距离（米）
    uint8_t  dim_starboard;     // 右舷到天线距离（米）

    uint32_t last_seen_ms;      // 最后接收时间戳（ms）
    uint32_t msg_count;         // 接收消息数
    uint8_t  last_msg_type;     // 最后消息类型
} vessel_t;

// ---------------------------------------------------------------------------
// 统计信息
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t total_frames;      // 总帧数
    uint32_t valid_frames;      // 有效帧数
    uint32_t crc_errors;        // CRC 错误数
    uint32_t vessel_count;      // 当前跟踪船只数
    uint32_t uptime_s;          // 运行时间（秒）
    uint32_t ch_a_frames;       // 信道 A 帧数
    uint32_t ch_b_frames;       // 信道 B 帧数
} ais_stats_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/**
 * @brief 初始化 AIS 解码器
 */
esp_err_t ais_decoder_init(void);

/**
 * @brief 启动 AIS 接收（开始调谐 RTL-SDR）
 */
esp_err_t ais_decoder_start(void);

/**
 * @brief 停止 AIS 接收
 */
void ais_decoder_stop(void);

/**
 * @brief 获取船只列表（调用后需调用 ais_release_vessel_list）
 * @param[out] vessels  指向内部数组的指针
 * @param[out] count    有效船只数量
 */
void ais_get_vessel_list(vessel_t **vessels, int *count);

/**
 * @brief 释放船只列表锁
 */
void ais_release_vessel_list(void);

/**
 * @brief 获取统计信息
 */
void ais_get_stats(ais_stats_t *stats);

/**
 * @brief 注册 HTTP/WebSocket 处理器到 httpd
 */
esp_err_t ais_register_handlers(httpd_handle_t server);

/**
 * @brief 向所有 WebSocket 客户端广播当前船只数据
 */
void ais_ws_broadcast(void);

// ---------------------------------------------------------------------------
// 辅助函数
// ---------------------------------------------------------------------------
const char *ais_nav_status_str(uint8_t status);
const char *ais_ship_type_str(uint8_t type);

#ifdef __cplusplus
}
#endif