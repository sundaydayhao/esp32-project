/**
 * @file ota_manager.c
 * @brief OTA 固件升级管理器实现
 */
#include "ota_manager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static ota_status_t      s_status;
static SemaphoreHandle_t s_trigger;
static char              s_ota_url[256];

static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt)
{
    (void)evt;
    return ESP_OK;
}

esp_err_t ota_manager_init(void)
{
    s_trigger = xSemaphoreCreateBinary();
    configASSERT(s_trigger);
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = OTA_STATE_IDLE;
    ota_manager_mark_valid();
    ESP_LOGI(TAG_OTA, "OTA 管理器初始化完成 (当前固件: %s)", FW_VERSION_STR);
    return ESP_OK;
}

esp_err_t ota_manager_start(const char *url)
{
    if (!url || strlen(url) == 0) return ESP_ERR_INVALID_ARG;
    if (s_status.state != OTA_STATE_IDLE) {
        ESP_LOGW(TAG_OTA, "OTA 正在进行中，忽略新请求");
        return ESP_ERR_INVALID_STATE;
    }
    strlcpy(s_ota_url, url, sizeof(s_ota_url));
    xSemaphoreGive(s_trigger);
    ESP_LOGI(TAG_OTA, "OTA 升级已触发: %s", url);
    return ESP_OK;
}

void ota_manager_get_status(ota_status_t *status)
{
    if (status) *status = s_status;
}

esp_err_t ota_manager_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
            if (ret == ESP_OK)
                ESP_LOGI(TAG_OTA, "固件已标记为有效，取消回滚");
            return ret;
        }
    }
    return ESP_OK;
}

static void do_ota_upgrade(void)
{
    ESP_LOGI(TAG_OTA, "开始 OTA 升级: %s", s_ota_url);
    s_status.state         = OTA_STATE_DOWNLOADING;
    s_status.bytes_written = 0;
    s_status.total_size    = 0;
    s_status.progress_pct  = 0;
    s_status.error_msg[0]  = '\0';

    esp_http_client_config_t http_cfg = {
        .url               = s_ota_url,
        .timeout_ms        = OTA_RECV_TIMEOUT_MS,
        .keep_alive_enable = true,
        .event_handler     = ota_http_event_handler,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t ret = esp_https_ota_begin(&ota_cfg, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_OTA, "OTA 初始化失败: %s", esp_err_to_name(ret));
        s_status.state = OTA_STATE_FAILED;
        strlcpy(s_status.error_msg, esp_err_to_name(ret), sizeof(s_status.error_msg));
        return;
    }

    esp_app_desc_t app_desc;
    if (esp_https_ota_get_img_desc(ota_handle, &app_desc) == ESP_OK)
        ESP_LOGI(TAG_OTA, "新固件版本: %s", app_desc.version);

    while (1) {
        ret = esp_https_ota_perform(ota_handle);
        if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
        s_status.bytes_written = esp_https_ota_get_image_len_read(ota_handle);
        int total = esp_https_ota_get_image_size(ota_handle);
        if (total > 0) {
            s_status.total_size   = (uint32_t)total;
            s_status.progress_pct = (uint8_t)(s_status.bytes_written * 100 / total);
        }
        ESP_LOGD(TAG_OTA, "下载进度: %u%%", s_status.progress_pct);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_OTA, "OTA 下载失败: %s", esp_err_to_name(ret));
        esp_https_ota_abort(ota_handle);
        s_status.state = OTA_STATE_FAILED;
        strlcpy(s_status.error_msg, esp_err_to_name(ret), sizeof(s_status.error_msg));
        return;
    }

    s_status.state = OTA_STATE_VERIFYING;
    ret = esp_https_ota_finish(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_OTA, "OTA 完成失败: %s", esp_err_to_name(ret));
        s_status.state = OTA_STATE_FAILED;
        strlcpy(s_status.error_msg, esp_err_to_name(ret), sizeof(s_status.error_msg));
        return;
    }

    s_status.state        = OTA_STATE_SUCCESS;
    s_status.progress_pct = 100;
    ESP_LOGI(TAG_OTA, "OTA 升级成功！3 秒后重启...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

void ota_task(void *arg)
{
    ESP_LOGI(TAG_OTA, "OTA 任务启动，等待升级触发...");
    while (1) {
        if (xSemaphoreTake(s_trigger, portMAX_DELAY) == pdTRUE) {
            do_ota_upgrade();
            s_status.state = OTA_STATE_IDLE;
        }
    }
}