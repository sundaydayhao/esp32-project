/**
 * @file ui_font_manager.c
 * @brief Load external GB2312 font from flash partition
 */
#include "ui_font_manager.h"

#include <stdbool.h>

#include "config.h"
#include "esp_log.h"
#include "esp_spiffs.h"

LV_FONT_DECLARE(ui_font_noto_sc_16);

#define UI_FONT_FS_BASE_PATH        "/spiffs"
#define UI_FONT_FS_PARTITION_LABEL  "fonts"
#define UI_FONT_BIN_PATH            "A:/spiffs/ui_font_noto_sc_gb2312_16.bin"

static const lv_font_t *s_font_16 = &ui_font_noto_sc_16;
static lv_font_t *s_external_font_16 = NULL;
static bool s_init_done = false;
static bool s_fs_ready = false;
static bool s_load_attempted = false;
static esp_err_t s_init_result = ESP_OK;
static esp_err_t s_load_result = ESP_ERR_INVALID_STATE;

esp_err_t ui_font_manager_init(void)
{
    if (s_init_done) {
        return s_init_result;
    }

    s_init_done = true;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = UI_FONT_FS_BASE_PATH,
        .partition_label = UI_FONT_FS_PARTITION_LABEL,
        .max_files = 2,
        .format_if_mount_failed = false,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        s_init_result = ret;
        ESP_LOGW(TAG_UI, "Mount font partition failed, fallback to built-in font: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    s_fs_ready = true;
    s_init_result = ESP_OK;
    ESP_LOGI(TAG_UI, "Font partition mounted, external GB2312 font will be loaded later");
    return ESP_OK;
}

const lv_font_t *ui_font_manager_get_font_16(void)
{
    return s_font_16;
}

esp_err_t ui_font_manager_load_external(void)
{
    if (s_external_font_16) {
        return ESP_OK;
    }

    if (!s_fs_ready) {
        return s_init_result != ESP_OK ? s_init_result : ESP_ERR_INVALID_STATE;
    }

    if (s_load_attempted) {
        return s_load_result;
    }

    s_load_attempted = true;
    ESP_LOGI(TAG_UI, "Loading external font from flash: %s", UI_FONT_BIN_PATH);

    s_external_font_16 = lv_binfont_create(UI_FONT_BIN_PATH);
    if (!s_external_font_16) {
        s_load_result = ESP_FAIL;
        ESP_LOGW(TAG_UI, "Load external font failed, fallback to built-in font");
        return s_load_result;
    }

    s_font_16 = s_external_font_16;

    size_t total = 0;
    size_t used = 0;
    if (esp_spiffs_info(UI_FONT_FS_PARTITION_LABEL, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG_UI, "External font loaded from flash: %s (used=%u total=%u)",
                 UI_FONT_BIN_PATH, (unsigned)used, (unsigned)total);
    } else {
        ESP_LOGI(TAG_UI, "External font loaded from flash: %s", UI_FONT_BIN_PATH);
    }

    s_load_result = ESP_OK;
    return ESP_OK;
}

bool ui_font_manager_is_external_loaded(void)
{
    return s_external_font_16 != NULL;
}
