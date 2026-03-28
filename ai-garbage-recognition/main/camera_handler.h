/**
 * camera_handler.h
 * 摄像头处理模块接口 / Camera Handler Interface
 */

#pragma once

#include "esp_camera.h"
#include "app_config.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化摄像头
 * @return ESP_OK 成功
 */
esp_err_t camera_init(void);

/**
 * @brief 反初始化摄像头，释放资源
 */
void camera_deinit(void);

/**
 * @brief 捕获一帧图像并转换为 RGB888 格式存入内部缓冲区
 * @return ESP_OK 成功
 */
esp_err_t camera_capture_frame(void);

/**
 * @brief 获取 RGB888 图像缓冲区指针
 * @return 缓冲区指针，NULL 表示未初始化
 */
uint8_t* camera_get_rgb_buffer(void);

/**
 * @brief 获取缓冲区大小
 * @return 字节数
 */
size_t camera_get_buffer_size(void);

esp_err_t camera_get_jpeg_buffer(uint8_t **jpeg_buf, size_t *jpeg_len);

/**
 * @brief 检测是否有物体放入 (基于帧差运动检测)
 * @return true 检测到运动/物体
 */
bool camera_detect_object(void);

/**
 * @brief 获取 ROI 区域平均颜色
 * @param[out] r 红色分量
 * @param[out] g 绿色分量
 * @param[out] b 蓝色分量
 */
void camera_get_roi_avg_color(uint8_t *r, uint8_t *g, uint8_t *b);

#ifdef __cplusplus
}
#endif
