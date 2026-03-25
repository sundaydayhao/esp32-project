#pragma once

#include "esp_err.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化摄像头
 *
 * 使用 Kconfig 中配置的引脚和参数初始化摄像头模块。
 * 支持 OV2640、OV5640 等常见摄像头传感器。
 *
 * @return ESP_OK 成功，其他值表示失败
 */
esp_err_t camera_init(void);

/**
 * @brief 拍摄一张照片
 *
 * @return 帧缓冲区指针，使用完毕后必须调用 esp_camera_fb_return() 释放
 *         返回 NULL 表示拍摄失败
 */
camera_fb_t *camera_capture(void);

#ifdef __cplusplus
}
#endif