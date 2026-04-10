/**
 * @file config_store.c
 * @brief 配置持久化存储实现
 */
#include "config_store.h"
#include "config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static nvs_handle_t s_handle = 0;

esp_err_t config_store_init(void)
{
    esp_err_t ret = nvs_open(NVS_NAMESPACE_CLOCK, NVS_READWRITE, &s_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_STORE, "NVS 打开失败: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t config_store_get_u8(const char *key, uint8_t *val, uint8_t def)
{
    esp_err_t ret = nvs_get_u8(s_handle, key, val);
    if (ret != ESP_OK) *val = def;
    return ret;
}

esp_err_t config_store_set_u8(const char *key, uint8_t val)
{
    esp_err_t ret = nvs_set_u8(s_handle, key, val);
    if (ret != ESP_OK) {
        return ret;
    }

    return nvs_commit(s_handle);
}

esp_err_t config_store_get_str(const char *key, char *buf, size_t len)
{
    return nvs_get_str(s_handle, key, buf, &len);
}
