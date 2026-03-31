#include "storage.h"
#include "reminder.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_spiffs.h"
#include "cJSON.h"

static const char *TAG = "storage";
#define NVS_NAMESPACE "med_reminder"

esp_err_t storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Storage initialized");
    return ESP_OK;
}

esp_err_t storage_load_reminders(reminder_t *reminders, uint8_t count)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (ret != ESP_OK) return ret;

    for (int i = 0; i < count; i++) {
        char key[8];
        snprintf(key, sizeof(key), "rem_%d", i);
        size_t len = sizeof(reminder_config_t);
        ret = nvs_get_blob(handle, key, &reminders[i].config, &len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "No saved reminder %d", i);
        }
    }
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t storage_save_reminders(const reminder_t *reminders, uint8_t count)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;

    for (int i = 0; i < count; i++) {
        char key[8];
        snprintf(key, sizeof(key), "rem_%d", i);
        ret = nvs_set_blob(handle, key, &reminders[i].config, sizeof(reminder_config_t));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Save reminder %d failed", i);
        }
    }
    ret = nvs_commit(handle);
    nvs_close(handle);
    return ret;
}

esp_err_t storage_log_medication(const char *datetime_str, const char *medication_name,
                                  const char *dosage, const char *status)
{
    FILE *f = fopen(LOG_FILE_PATH, "a");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open log file");
        return ESP_FAIL;
    }
    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "time",   datetime_str    ? datetime_str    : "");
    cJSON_AddStringToObject(entry, "drug",   medication_name ? medication_name : "");
    cJSON_AddStringToObject(entry, "dosage", dosage          ? dosage          : "");
    cJSON_AddStringToObject(entry, "status", status          ? status          : "");
    char *json_str = cJSON_PrintUnformatted(entry);
    if (json_str) {
        fprintf(f, "%s\n", json_str);
        free(json_str);
    }
    cJSON_Delete(entry);
    fclose(f);
    return ESP_OK;
}

esp_err_t storage_read_log(char *buf, size_t buf_len, int max_entries)
{
    if (!buf || buf_len == 0) return ESP_ERR_INVALID_ARG;
    FILE *f = fopen(LOG_FILE_PATH, "r");
    if (!f) {
        buf[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }

    if (max_entries <= 0) {
        /* Read all */
        size_t n = fread(buf, 1, buf_len - 1, f);
        buf[n] = '\0';
    } else {
        /* Read up to max_entries lines */
        size_t pos = 0;
        int lines = 0;
        char line[LOG_ENTRY_MAX_LEN + 2];
        while (lines < max_entries && fgets(line, sizeof(line), f)) {
            size_t len = strlen(line);
            if (pos + len >= buf_len - 1) break;
            memcpy(buf + pos, line, len);
            pos += len;
            lines++;
        }
        buf[pos] = '\0';
    }
    fclose(f);
    return ESP_OK;
}

esp_err_t storage_clear_log(void)
{
    FILE *f = fopen(LOG_FILE_PATH, "w");
    if (!f) return ESP_FAIL;
    fclose(f);
    return ESP_OK;
}

esp_err_t storage_save_wifi(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;
    nvs_set_str(handle, "wifi_ssid", ssid     ? ssid     : "");
    nvs_set_str(handle, "wifi_pass", password ? password : "");
    ret = nvs_commit(handle);
    nvs_close(handle);
    return ret;
}

esp_err_t storage_load_wifi(char *ssid, size_t ssid_len,
                             char *password, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (ret != ESP_OK) return ret;
    nvs_get_str(handle, "wifi_ssid", ssid,     &ssid_len);
    nvs_get_str(handle, "wifi_pass", password, &pass_len);
    nvs_close(handle);
    return ESP_OK;
}