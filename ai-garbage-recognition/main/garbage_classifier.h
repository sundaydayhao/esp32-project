/**
 * garbage_classifier.h
 * 垃圾分类识别模块接口 / Garbage Classifier Interface
 */

#pragma once

#include "app_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化分类器
 * @return ESP_OK 成功
 */
esp_err_t classifier_init(void);

/**
 * @brief 执行垃圾分类识别
 * @param[out] result 分类结果
 * @return ESP_OK 成功
 */
esp_err_t classifier_run(classification_result_t *result);

/**
 * @brief 获取垃圾类型的中文名称
 */
const char* classifier_get_name_cn(garbage_type_t type);

/**
 * @brief 获取垃圾类型的英文名称
 */
const char* classifier_get_name_en(garbage_type_t type);

#ifdef __cplusplus
}
#endif