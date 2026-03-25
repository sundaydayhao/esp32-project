#pragma once

#include "esp_err.h"
#include "esp_camera.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将图片发送到微信
 *
 * 根据 Kconfig 配置，使用企业微信 Webhook 或 Server酱 发送图片。
 *
 * @param image_data  JPEG 图片数据指针
 * @param image_len   图片数据长度（字节）
 * @return ESP_OK 发送成功，其他值表示失败
 */
esp_err_t wechat_send_image(const uint8_t *image_data, size_t image_len);

/**
 * @brief 将相机帧发送到微信
 *
 * 针对企业微信图片格式校验失败的场景，会基于帧信息尝试做一次 JPEG
 * 规范化转码后重试。
 *
 * @param frame 相机帧缓冲区
 * @return ESP_OK 发送成功，其它值表示失败
 */
esp_err_t wechat_send_camera_frame(const camera_fb_t *frame);

/**
 * @brief 发送文本消息到微信
 *
 * @param message  要发送的文本消息
 * @return ESP_OK 发送成功，其他值表示失败
 */
esp_err_t wechat_send_text(const char *message);

#ifdef __cplusplus
}
#endif
