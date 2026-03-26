/**
 * @file storage.c
 * @brief NVS 持久化存储模块实现
 */

#include "storage.h"
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "storage";

// ============================================================
// 初始化
// ============================================================

esp_err_t storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区需要擦除，正在重置...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS 初始化成功");
    } else {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(ret));
    }
    return ret;
}

// ============================================================
// 取件码操作
// ============================================================

esp_err_t storage_save_code(uint8_t locker_id, const char *code)
{
    nvs_handle_t handle;
    char key[16];
    snprintf(key, sizeof(key), "code_%d", locker_id);

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_str(handle, key, code);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "格口%d 取件码已保存", locker_id);
    }
    return ret;
}

esp_err_t storage_load_code(uint8_t locker_id, char *code, size_t max_len)
{
    nvs_handle_t handle;
    char key[16];
    snprintf(key, sizeof(key), "code_%d", locker_id);

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_get_str(handle, key, code, &max_len);
    nvs_close(handle);
    return ret;
}

esp_err_t storage_clear_code(uint8_t locker_id)
{
    nvs_handle_t handle;
    char key[16];
    snprintf(key, sizeof(key), "code_%d", locker_id);

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_erase_key(handle, key);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK; // 不存在也算成功
    }
    if (ret == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

// ============================================================
// 格口状态操作
// ============================================================

esp_err_t storage_save_state(uint8_t locker_id, uint8_t state)
{
    nvs_handle_t handle;
    char key[16];
    snprintf(key, sizeof(key), "state_%d", locker_id);

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_u8(handle, key, state);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t storage_load_state(uint8_t locker_id, uint8_t *state)
{
    nvs_handle_t handle;
    char key[16];
    snprintf(key, sizeof(key), "state_%d", locker_id);

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        *state = 0; // 默认空闲
        return ESP_OK;
    }

    ret = nvs_get_u8(handle, key, state);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *state = 0;
        ret = ESP_OK;
    }
    nvs_close(handle);
    return ret;
}

// ============================================================
// 时间戳操作
// ============================================================

esp_err_t storage_save_timestamp(uint8_t locker_id, int64_t timestamp)
{
    nvs_handle_t handle;
    char key[16];
    snprintf(key, sizeof(key), "ts_%d", locker_id);

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_i64(handle, key, timestamp);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t storage_load_timestamp(uint8_t locker_id, int64_t *timestamp)
{
    nvs_handle_t handle;
    char key[16];
    snprintf(key, sizeof(key), "ts_%d", locker_id);

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        *timestamp = 0;
        return ESP_OK;
    }

    ret = nvs_get_i64(handle, key, timestamp);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *timestamp = 0;
        ret = ESP_OK;
    }
    nvs_close(handle);
    return ret;
}

// ============================================================
// WiFi 配置操作
// ============================================================

esp_err_t storage_save_wifi(const char *ssid, const char *password)
{
    nvs_handle_t handle;

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_str(handle, "wifi_ssid", ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, "wifi_pass", password);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi配置已保存: SSID=%s", ssid);
    }
    return ret;
}

esp_err_t storage_load_wifi(char *ssid, size_t ssid_len,
                             char *password, size_t pass_len)
{
    nvs_handle_t handle;

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_get_str(handle, "wifi_ssid", ssid, &ssid_len);
    if (ret == ESP_OK) {
        ret = nvs_get_str(handle, "wifi_pass", password, &pass_len);
    }
    nvs_close(handle);
    return ret;
}

// ============================================================
// 失败次数操作
// ============================================================

esp_err_t storage_save_fail_count(uint8_t locker_id, uint8_t count)
{
    nvs_handle_t handle;
    char key[16];
    snprintf(key, sizeof(key), "fail_%d", locker_id);

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    ret = nvs_set_u8(handle, key, count);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}

esp_err_t storage_load_fail_count(uint8_t locker_id, uint8_t *count)
{
    nvs_handle_t handle;
    char key[16];
    snprintf(key, sizeof(key), "fail_%d", locker_id);

    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        *count = 0;
        return ESP_OK;
    }

    ret = nvs_get_u8(handle, key, count);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *count = 0;
        ret = ESP_OK;
    }
    nvs_close(handle);
    return ret;
}