/**
 * @file bmp_decoder.h
 * @brief BMP 图片解码器（支持 24-bit / 32-bit BMP）
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 解码 BMP 文件到帧缓冲区
 * @param path      BMP 文件路径
 * @param fb        目标帧缓冲区
 * @param mode      缩放模式
 * @param bg_color  背景色
 * @return ESP_OK 成功
 */
esp_err_t bmp_decode_to_fb(const char *path, uint16_t *fb,
                            scale_mode_t mode, uint16_t bg_color);

#ifdef __cplusplus
}
#endif