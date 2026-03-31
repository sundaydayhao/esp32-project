/**
 * @file env_history.c
 * @brief Indoor/outdoor environment history sampler
 */
#include "history/env_history.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "esp_log.h"
#include "net/ntp_client.h"
#include "net/weather_client.h"
#include "nvs.h"
#include "sensor/dht11_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static env_history_point_t s_points[ENV_HISTORY_CAPACITY];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static size_t s_count = 0;
static size_t s_head = 0;
static nvs_handle_t s_nvs = 0;

#define ENV_HISTORY_CAPTURE_GRACE_S 10
#define ENV_HISTORY_BLOB_VERSION 1U

typedef struct {
    uint32_t version;
    uint32_t count;
    uint32_t head;
    env_history_point_t points[ENV_HISTORY_CAPACITY];
} env_history_blob_t;

static env_history_blob_t *env_history_alloc_blob(void)
{
    env_history_blob_t *blob = calloc(1, sizeof(*blob));
    if (!blob) {
        ESP_LOGE(TAG_HISTORY, "Failed to allocate history snapshot buffer (%u bytes)",
                 (unsigned)sizeof(*blob));
        return NULL;
    }

    blob->version = ENV_HISTORY_BLOB_VERSION;
    return blob;
}

static esp_err_t env_history_save_snapshot(void)
{
    if (s_nvs == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    env_history_blob_t *blob = env_history_alloc_blob();
    if (!blob) {
        return ESP_ERR_NO_MEM;
    }

    portENTER_CRITICAL(&s_lock);
    blob->count = (uint32_t)s_count;
    blob->head = (uint32_t)s_head;
    memcpy(blob->points, s_points, sizeof(s_points));
    portEXIT_CRITICAL(&s_lock);

    esp_err_t ret = nvs_set_blob(s_nvs, NVS_KEY_ENV_HISTORY, blob, sizeof(*blob));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_HISTORY, "Failed to save history blob: %s", esp_err_to_name(ret));
        free(blob);
        return ret;
    }

    ret = nvs_commit(s_nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_HISTORY, "Failed to commit history blob: %s", esp_err_to_name(ret));
        free(blob);
        return ret;
    }

    free(blob);
    return ESP_OK;
}

static void env_history_reset_locked(void)
{
    memset(s_points, 0, sizeof(s_points));
    s_count = 0;
    s_head = 0;
}

static esp_err_t env_history_load_snapshot(void)
{
    if (s_nvs == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    env_history_blob_t *blob = env_history_alloc_blob();
    if (!blob) {
        return ESP_ERR_NO_MEM;
    }

    size_t blob_size = sizeof(*blob);
    esp_err_t ret = nvs_get_blob(s_nvs, NVS_KEY_ENV_HISTORY, blob, &blob_size);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        free(blob);
        return ESP_ERR_NOT_FOUND;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_HISTORY, "Failed to load history blob: %s", esp_err_to_name(ret));
        free(blob);
        return ret;
    }
    if (blob_size != sizeof(*blob) || blob->version != ENV_HISTORY_BLOB_VERSION ||
        blob->count > ENV_HISTORY_CAPACITY || blob->head >= ENV_HISTORY_CAPACITY) {
        ESP_LOGW(TAG_HISTORY, "History blob is invalid, clearing persisted data");
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_erase_key(s_nvs, NVS_KEY_ENV_HISTORY));
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_commit(s_nvs));
        free(blob);
        return ESP_ERR_INVALID_SIZE;
    }

    portENTER_CRITICAL(&s_lock);
    memcpy(s_points, blob->points, sizeof(s_points));
    s_count = blob->count;
    s_head = blob->head;
    portEXIT_CRITICAL(&s_lock);

    free(blob);
    return ESP_OK;
}

static void env_history_store_point(const env_history_point_t *point)
{
    size_t write_index = 0;
    bool updated = false;

    if (!point) {
        return;
    }

    portENTER_CRITICAL(&s_lock);
    if (s_count > 0) {
        size_t latest_index = (s_head + s_count - 1) % ENV_HISTORY_CAPACITY;
        if (s_points[latest_index].timestamp_s == point->timestamp_s) {
            s_points[latest_index] = *point;
            updated = true;
        } else {
            if (s_count < ENV_HISTORY_CAPACITY) {
                write_index = (s_head + s_count) % ENV_HISTORY_CAPACITY;
                s_count++;
            } else {
                write_index = s_head;
                s_head = (s_head + 1) % ENV_HISTORY_CAPACITY;
            }
            s_points[write_index] = *point;
            updated = true;
        }
    } else {
        write_index = (s_head + s_count) % ENV_HISTORY_CAPACITY;
        s_count++;
        s_points[write_index] = *point;
        updated = true;
    }
    portEXIT_CRITICAL(&s_lock);

    if (updated) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(env_history_save_snapshot());
    }
}

static TickType_t env_history_delay_until_next_capture(void)
{
    time_t now = ntp_client_get_time();

    if (ntp_client_is_synced() && now > 0) {
        time_t current_slot = (now / ENV_HISTORY_INTERVAL_S) * ENV_HISTORY_INTERVAL_S;
        time_t target = current_slot + ENV_HISTORY_CAPTURE_GRACE_S;

        if (now >= target) {
            target = current_slot + ENV_HISTORY_INTERVAL_S + ENV_HISTORY_CAPTURE_GRACE_S;
        }

        if (target <= now) {
            target = now + 1;
        }

        return pdMS_TO_TICKS((uint32_t)(target - now) * 1000U);
    }

    return pdMS_TO_TICKS(60000);
}

static void env_history_capture_once(void)
{
    dht11_data_t indoor = {0};
    weather_data_t outdoor = {0};
    env_history_point_t point = {0};
    time_t now = ntp_client_get_time();

    if (now <= 0) {
        return;
    }

    dht11_sensor_get_data(&indoor);
    weather_client_get_data(&outdoor);

    point.timestamp_s = (int64_t)((now / ENV_HISTORY_INTERVAL_S) * ENV_HISTORY_INTERVAL_S);
    point.indoor_valid = indoor.valid;
    point.indoor_temp_c = indoor.temperature_c;
    point.indoor_humidity = indoor.humidity;
    point.outdoor_valid = outdoor.now_valid;
    point.outdoor_temp_c = outdoor.temp_now;
    point.outdoor_humidity = outdoor.humidity;

    env_history_store_point(&point);
}

esp_err_t env_history_init(void)
{
    esp_err_t ret = nvs_open(NVS_NAMESPACE_CLOCK, NVS_READWRITE, &s_nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_HISTORY, "Failed to open NVS for history: %s", esp_err_to_name(ret));
        return ret;
    }

    portENTER_CRITICAL(&s_lock);
    env_history_reset_locked();
    portEXIT_CRITICAL(&s_lock);

    ret = env_history_load_snapshot();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG_HISTORY, "Restored persisted history: count=%u head=%u",
                 (unsigned)s_count, (unsigned)s_head);
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG_HISTORY, "No persisted history found, starting fresh");
    } else {
        portENTER_CRITICAL(&s_lock);
        env_history_reset_locked();
        portEXIT_CRITICAL(&s_lock);
        ESP_ERROR_CHECK_WITHOUT_ABORT(env_history_save_snapshot());
    }

    ESP_LOGI(TAG_HISTORY, "Environment history initialized: interval=%us capacity=%u",
             (unsigned)ENV_HISTORY_INTERVAL_S, (unsigned)ENV_HISTORY_CAPACITY);
    return ESP_OK;
}

size_t env_history_get_count(void)
{
    size_t count = 0;

    portENTER_CRITICAL(&s_lock);
    count = s_count;
    portEXIT_CRITICAL(&s_lock);

    return count;
}

size_t env_history_copy_points(env_history_point_t *points, size_t max_points)
{
    size_t count = 0;

    if (!points || max_points == 0) {
        return 0;
    }

    portENTER_CRITICAL(&s_lock);
    count = s_count;
    if (count > max_points) {
        count = max_points;
    }
    for (size_t i = 0; i < count; i++) {
        size_t index = (s_head + i) % ENV_HISTORY_CAPACITY;
        points[i] = s_points[index];
    }
    portEXIT_CRITICAL(&s_lock);

    return count;
}

void env_history_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(DHT11_STARTUP_DELAY_MS + 1000));

    while (1) {
        vTaskDelay(env_history_delay_until_next_capture());
        env_history_capture_once();
    }
}
