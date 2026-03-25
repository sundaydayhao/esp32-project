#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 图像帧数据结构
 */
typedef struct {
    uint8_t *data;      /*!< JPEG 图像数据指针 */
    size_t   len;       /*!< 图像数据长度（字节） */
    uint32_t timestamp; /*!< 拍摄时间戳（毫秒） */
    uint32_t width;     /*!< 图像宽度 */
    uint32_t height;    /*!< 图像高度 */
} doorman_frame_t;

/**
 * @brief 初始化摄像头
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t camera_manager_init(void);

/**
 * @brief 反初始化摄像头，释放资源
 */
void camera_manager_deinit(void);

/**
 * @brief 拍摄一张照片
 * @param frame 输出的帧数据（调用者需调用 camera_manager_free_frame 释放）
 * @return ESP_OK 成功
 */
esp_err_t camera_manager_capture(doorman_frame_t *frame);

/**
 * @brief 释放帧数据内存
 * @param frame 要释放的帧数据
 */
void camera_manager_free_frame(doorman_frame_t *frame);

/**
 * @brief 获取原始灰度帧（用于运动检测）
 * @param gray_buf 输出灰度数据缓冲区（调用者分配）
 * @param width 输出图像宽度
 * @param height 输出图像高度
 * @return ESP_OK 成功
 */
esp_err_t camera_manager_get_grayscale(uint8_t **gray_buf, uint32_t *width, uint32_t *height);

/**
 * @brief 释放灰度帧缓冲区
 */
void camera_manager_free_grayscale(uint8_t *gray_buf);

/**
 * @brief 检查摄像头是否已初始化
 */
bool camera_manager_is_ready(void);

/**
 * @brief 设置摄像头图像质量
 * @param quality JPEG 质量 (0-63，数值越小质量越高)
 */
esp_err_t camera_manager_set_quality(int quality);

/**
 * @brief 设置摄像头亮度
 * @param brightness 亮度 (-2 到 2)
 */
esp_err_t camera_manager_set_brightness(int brightness);

#ifdef __cplusplus
}
#endif