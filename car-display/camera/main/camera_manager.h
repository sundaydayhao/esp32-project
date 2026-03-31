#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 摄像头选择枚举
 */
typedef enum {
    CAMERA_LEFT  = 0,   /*!< 左侧摄像头 */
    CAMERA_RIGHT = 1,   /*!< 右侧摄像头 */
    CAMERA_NONE  = -1,  /*!< 无摄像头 */
} camera_id_t;

/**
 * @brief 摄像头帧数据结构
 */
typedef struct {
    uint8_t  *buf;          /*!< 帧数据缓冲区指针 (RGB565格式) */
    size_t    len;          /*!< 帧数据字节长度 */
    uint32_t  width;        /*!< 帧宽度 (像素) */
    uint32_t  height;       /*!< 帧高度 (像素) */
    camera_id_t camera_id;  /*!< 来源摄像头 */
    uint32_t  timestamp_ms; /*!< 采集时间戳 (ms) */
} camera_frame_t;

/**
 * @brief 摄像头管理器配置
 */
typedef struct {
    /* 共享DVP接口引脚 */
    int pin_xclk;
    int pin_pclk;
    int pin_vsync;
    int pin_href;
    int pin_d0;
    int pin_d1;
    int pin_d2;
    int pin_d3;
    int pin_d4;
    int pin_d5;
    int pin_d6;
    int pin_d7;
    int pin_sda;
    int pin_scl;

    /* 左摄像头独立控制引脚 */
    int pin_left_pwdn;
    int pin_left_reset;

    /* 右摄像头独立控制引脚 */
    int pin_right_pwdn;
    int pin_right_reset;

    /* 采集参数 */
    int frame_width;
    int frame_height;
    int xclk_freq_hz;
    int switch_delay_ms;
} camera_manager_config_t;

/**
 * @brief 初始化摄像头管理器
 *
 * @param config 摄像头管理器配置
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t camera_manager_init(const camera_manager_config_t *config);

/**
 * @brief 反初始化摄像头管理器
 */
void camera_manager_deinit(void);

/**
 * @brief 切换到指定摄像头
 *
 * @param camera_id 目标摄像头
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t camera_manager_select(camera_id_t camera_id);

/**
 * @brief 获取当前激活的摄像头
 *
 * @return camera_id_t 当前摄像头ID
 */
camera_id_t camera_manager_get_active(void);

/**
 * @brief 采集一帧图像
 *
 * @param camera_id 要采集的摄像头
 * @param frame     输出帧数据 (调用者需调用 camera_frame_free 释放)
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t camera_manager_capture(camera_id_t camera_id, camera_frame_t **frame);

/**
 * @brief 释放帧数据
 *
 * @param frame 要释放的帧
 */
void camera_frame_free(camera_frame_t *frame);

/**
 * @brief 水平翻转帧图像 (用于镜像校正)
 *
 * @param frame 要翻转的帧 (原地修改)
 */
void camera_frame_flip_horizontal(camera_frame_t *frame);

/**
 * @brief 垂直翻转帧图像
 *
 * @param frame 要翻转的帧 (原地修改)
 */
void camera_frame_flip_vertical(camera_frame_t *frame);

/**
 * @brief 获取摄像头统计信息
 *
 * @param camera_id  摄像头ID
 * @param fps        输出当前帧率
 * @param frame_count 输出总帧数
 */
void camera_manager_get_stats(camera_id_t camera_id, float *fps, uint32_t *frame_count);

#ifdef __cplusplus
}
#endif