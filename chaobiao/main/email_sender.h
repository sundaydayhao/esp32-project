#pragma once

#include "esp_camera.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将 JPEG 图片作为邮件附件发送
 *
 * @param image_data JPEG 图片数据指针
 * @param image_len  图片数据长度（字节）
 * @return ESP_OK 发送成功，其他值表示失败
 */
esp_err_t email_send_image(const uint8_t *image_data, size_t image_len);

/**
 * @brief 将相机帧作为邮件附件发送
 *
 * 如果相机输出不是 JPEG，会先在内存中转码为 JPEG。
 *
 * @param frame 相机帧缓冲区
 * @return ESP_OK 发送成功，其他值表示失败
 */
esp_err_t email_send_camera_frame(const camera_fb_t *frame);

/**
 * @brief 发送纯文本邮件通知
 *
 * @param message 要发送的文本消息
 * @return ESP_OK 发送成功，其他值表示失败
 */
esp_err_t email_send_text(const char *message);

#ifdef __cplusplus
}
#endif
