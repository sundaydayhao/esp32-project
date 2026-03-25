#pragma once

#include "esp_err.h"
#include "camera_manager.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化并启动图像 HTTP 服务器
 * @return ESP_OK 成功
 */
esp_err_t image_server_init(void);

/**
 * @brief 停止图像 HTTP 服务器
 */
void image_server_stop(void);

/**
 * @brief 更新服务器上的最新图像
 * @param frame 新的图像帧数据
 * @param event_type 事件类型描述字符串（如 "人员活动" 或 "快递到达"）
 * @return ESP_OK 成功
 */
esp_err_t image_server_update_image(const doorman_frame_t *frame, const char *event_type);

/**
 * @brief 获取最新图像的访问 URL
 * @param url_buf 存储 URL 的缓冲区
 * @param buf_len 缓冲区长度
 * @return ESP_OK 成功
 */
esp_err_t image_server_get_url(char *url_buf, size_t buf_len);

/**
 * @brief 获取图像历史记录数量
 * @return 已存储的图像数量
 */
int image_server_get_image_count(void);

#ifdef __cplusplus
}
#endif