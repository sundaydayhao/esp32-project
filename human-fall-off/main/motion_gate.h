/*
 * 运动门控模块
 * 负责判断当前是否有人体活动，过滤空闲场景
 */

#ifndef MOTION_GATE_H
#define MOTION_GATE_H

#include "esp_err.h"
#include "wifi_csi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 运动状态枚举
 */
typedef enum {
    MOTION_STATE_IDLE = 0,      // 空闲，无明显活动
    MOTION_STATE_ACTIVE = 1,    // 活动中
    MOTION_STATE_UNKNOWN = 2,   // 未知状态
} motion_state_t;

/**
 * @brief 运动门控配置
 */
typedef struct {
    float threshold;            // 运动检测阈值
    uint32_t window_size;       // 滑动窗口大小
} motion_gate_config_t;

/**
 * @brief 运动状态回调函数类型
 * 
 * @param state 当前运动状态
 * @param user_ctx 用户上下文
 */
typedef void (*motion_state_callback_t)(motion_state_t state, void *user_ctx);

/**
 * @brief 初始化运动门控模块
 * 
 * @param config 配置参数
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t motion_gate_init(const motion_gate_config_t *config);

/**
 * @brief 处理 CSI 数据
 * 
 * @param csi_data CSI 数据包
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t motion_gate_process_csi(const csi_data_t *csi_data);

/**
 * @brief 获取当前运动状态
 * 
 * @return motion_state_t 当前运动状态
 */
motion_state_t motion_gate_get_state(void);

/**
 * @brief 注册运动状态变化回调
 * 
 * @param callback 回调函数
 * @param user_ctx 用户上下文
 * @return esp_err_t ESP_OK 成功, 其他值失败
 */
esp_err_t motion_gate_register_callback(motion_state_callback_t callback, void *user_ctx);

/**
 * @brief 获取运动强度值
 * 
 * @return float 当前运动强度 (0.0 - 1.0)
 */
float motion_gate_get_intensity(void);

#ifdef __cplusplus
}
#endif

#endif // MOTION_GATE_H