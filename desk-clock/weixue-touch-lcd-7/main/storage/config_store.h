/**
 * @file config_store.h
 * @brief 配置持久化存储（NVS）
 */
#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t config_store_init(void);
esp_err_t config_store_get_u8(const char *key, uint8_t *val, uint8_t def);
esp_err_t config_store_get_str(const char *key, char *buf, size_t len);

#ifdef __cplusplus
}
#endif
