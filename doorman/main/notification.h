#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通知类型
 */
typedef enum {
    NOTIFY_TYPE_PERSON,     /*!< 门口有人 */
    NOTIFY_TYPE_PACKAGE,    /*!< 快递到达 */
    NOTIFY_TYPE_ONLINE,     /*!< 设备上线 */
    NOTIFY_TYPE_OFFLINE,    /*!< 设备离线 */
} notify_type_t;

/**
 * @brief 初始化通知模块
 * @return ESP_OK 成功
 */
esp_err_t notification_init(void);

/**
 * @brief 发送微信通知（通过 Server酱）
 * @param type 通知类型
 * @param image_url 图像访问 URL（可为 NULL）
 * @param extra_info 附加信息（可为 NULL）
 * @return ESP_OK 发送成功
 */
esp_err_t notification_send_wechat(notify_type_t type,
                                    const char *image_url,
                                    const char *extra_info);

/**
 * @brief 发送自定义微信通知
 * @param title 通知标题
 * @param content 通知内容（支持 Markdown）
 * @return ESP_OK 发送成功
 */
esp_err_t notification_send_custom(const char *title, const char *content);

/**
 * @brief 检查通知服务是否可用
 * @return true 可用，false 不可用（未配置 SendKey 或无网络）
 */
bool notification_is_available(void);

#ifdef __cplusplus
}
#endif