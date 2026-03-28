/**
 * @file main.c
 * @brief NVS 非易失性存储示例
 *
 * 功能：
 *   - 读写整数、字符串、浮点数
 *   - 统计重启次数
 *   - 保存/恢复配置参数
 *   - NVS 命名空间管理
 *
 * NVS 特点：
 *   - 数据掉电不丢失
 *   - 支持多种数据类型
 *   - 使用键值对存储
 *   - 支持命名空间隔离
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "NVS";

/* NVS 命名空间 */
#define NVS_NAMESPACE   "app_config"

/**
 * @brief 统计重启次数
 */
static void nvs_count_restarts(void)
{
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));

    /* 读取重启次数 */
    int32_t restart_count = 0;
    esp_err_t err = nvs_get_i32(handle, "restart_cnt", &restart_count);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "首次启动，初始化重启计数");
        restart_count = 0;
    } else {
        ESP_ERROR_CHECK(err);
    }

    restart_count++;
    ESP_LOGI(TAG, "设备已重启 %d 次", restart_count);

    /* 保存更新后的计数 */
    ESP_ERROR_CHECK(nvs_set_i32(handle, "restart_cnt", restart_count));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

/**
 * @brief 保存配置
 */
static void nvs_save_config(void)
{
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));

    /* 保存各种类型的数据 */
    ESP_ERROR_CHECK(nvs_set_str(handle, "wifi_ssid", "MyWiFi"));
    ESP_ERROR_CHECK(nvs_set_str(handle, "wifi_pass", "password123"));
    ESP_ERROR_CHECK(nvs_set_i32(handle, "led_brightness", 80));
    ESP_ERROR_CHECK(nvs_set_u8(handle,  "led_mode", 2));

    /* 保存浮点数（NVS 不直接支持 float，转换为 uint32_t） */
    float temp_threshold = 35.5f;
    uint32_t temp_raw;
    memcpy(&temp_raw, &temp_threshold, sizeof(float));
    ESP_ERROR_CHECK(nvs_set_u32(handle, "temp_thresh", temp_raw));

    /* 保存二进制数据（blob） */
    uint8_t mac_addr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    ESP_ERROR_CHECK(nvs_set_blob(handle, "mac_addr", mac_addr, sizeof(mac_addr)));

    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);

    ESP_LOGI(TAG, "配置已保存到 NVS");
}

/**
 * @brief 读取配置
 */
static void nvs_load_config(void)
{
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle));

    /* 读取字符串 */
    char ssid[32] = {0};
    size_t ssid_len = sizeof(ssid);
    if (nvs_get_str(handle, "wifi_ssid", ssid, &ssid_len) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi SSID: %s", ssid);
    }

    /* 读取整数 */
    int32_t brightness = 0;
    if (nvs_get_i32(handle, "led_brightness", &brightness) == ESP_OK) {
        ESP_LOGI(TAG, "LED 亮度: %d%%", brightness);
    }

    uint8_t led_mode = 0;
    if (nvs_get_u8(handle, "led_mode", &led_mode) == ESP_OK) {
        ESP_LOGI(TAG, "LED 模式: %d", led_mode);
    }

    /* 读取浮点数 */
    uint32_t temp_raw = 0;
    if (nvs_get_u32(handle, "temp_thresh", &temp_raw) == ESP_OK) {
        float temp_threshold;
        memcpy(&temp_threshold, &temp_raw, sizeof(float));
        ESP_LOGI(TAG, "温度阈值: %.1f°C", temp_threshold);
    }

    /* 读取 blob */
    uint8_t mac_addr[6] = {0};
    size_t mac_len = sizeof(mac_addr);
    if (nvs_get_blob(handle, "mac_addr", mac_addr, &mac_len) == ESP_OK) {
        ESP_LOGI(TAG, "MAC 地址: %02X:%02X:%02X:%02X:%02X:%02X",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
    }

    nvs_close(handle);
}

/**
 * @brief 查看 NVS 统计信息
 */
static void nvs_show_stats(void)
{
    nvs_stats_t nvs_stats;
    nvs_get_stats(NULL, &nvs_stats);
    ESP_LOGI(TAG, "NVS 统计:");
    ESP_LOGI(TAG, "  已用条目: %d", nvs_stats.used_entries);
    ESP_LOGI(TAG, "  空闲条目: %d", nvs_stats.free_entries);
    ESP_LOGI(TAG, "  总条目数: %d", nvs_stats.total_entries);
    ESP_LOGI(TAG, "  命名空间数: %d", nvs_stats.namespace_count);
}

void app_main(void)
{
    ESP_LOGI(TAG, "NVS 存储示例启动");

    /* 初始化 NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 需要擦除重新初始化");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS 初始化完成");

    /* 统计重启次数 */
    nvs_count_restarts();

    /* 保存配置 */
    nvs_save_config();

    /* 读取配置 */
    nvs_load_config();

    /* 显示 NVS 统计 */
    nvs_show_stats();

    ESP_LOGI(TAG, "NVS 操作完成，重启设备可以看到重启计数增加");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}