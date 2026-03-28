/**
 * online_classifier.h
 * 在线垃圾类型查询模块 / Online Garbage Type Query Interface
 *
 * 工作流程:
 *   1. MobileNetV2 识别物体 → 获取物体名称 (如 "plastic_bottle")
 *   2. 本地查找表快速映射 → 垃圾类型 (离线，毫秒级)
 *   3. 若本地无匹配，通过 WiFi 查询在线 API (需联网)
 */

#pragma once

#include "app_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化在线分类器
 * @param wifi_ssid     WiFi 名称 (NULL 表示不使用 WiFi)
 * @param wifi_password WiFi 密码
 * @return ESP_OK 成功
 */
esp_err_t online_classifier_init(const char *wifi_ssid, const char *wifi_password);

/**
 * @brief 根据物体名称查询垃圾类型
 *        优先使用本地查找表，失败则查询在线 API
 * @param object_name 物体名称 (英文, 如 "battery", "plastic_bottle")
 * @param[out] result 垃圾类型
 * @param[out] confidence 置信度 0-100
 * @return ESP_OK 成功
 */
esp_err_t online_classifier_query(const char *object_name,
                                   garbage_type_t *result,
                                   int *confidence);

esp_err_t online_classifier_query_image(garbage_type_t *result,
                                        int *confidence);

/**
 * @brief 直接通过 HTTP API 查询垃圾类型
 * @param object_name 物体名称
 * @param[out] result 垃圾类型
 * @return ESP_OK 成功
 */
esp_err_t online_classifier_query_api(const char *object_name,
                                       garbage_type_t *result);

#ifdef __cplusplus
}
#endif
