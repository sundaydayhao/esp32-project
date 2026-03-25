/**
 * @file data_logger.c
 * @brief 本地数据记录器实现
 */
#include "data_logger.h"
#include "alarm/alarm_manager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---- 文件头魔数与版本 ---- */
#define LOG_MAGIC       0x494D454C  /* "IMEL" */
#define LOG_VERSION     1

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint32_t write_idx;     /* 下一个写入位置 */
    uint32_t total_count;   /* 已写入总条数 */
    uint8_t  reserved[7];
} log_header_t;             /* 20 字节 */

#define HEADER_SIZE     sizeof(log_header_t)
#define RECORD_SIZE     sizeof(log_record_t)
#define MAX_RECORDS     DATA_LOG_MAX_RECORDS

static log_header_t s_header;
static bool         s_initialized = false;

static esp_err_t header_read(FILE *f)
{
    fseek(f, 0, SEEK_SET);
    if (fread(&s_header, 1, HEADER_SIZE, f) != HEADER_SIZE) return ESP_FAIL;
    if (s_header.magic != LOG_MAGIC) return ESP_ERR_NOT_FOUND;
    return ESP_OK;
}

static esp_err_t header_write(FILE *f)
{
    fseek(f, 0, SEEK_SET);
    if (fwrite(&s_header, 1, HEADER_SIZE, f) != HEADER_SIZE) return ESP_FAIL;
    fflush(f);
    return ESP_OK;
}

static long record_offset(uint32_t idx)
{
    return (long)(HEADER_SIZE + (idx % MAX_RECORDS) * RECORD_SIZE);
}

esp_err_t data_logger_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/spiffs",
        .partition_label        = "storage",
        .max_files              = 4,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG_DATALOG, "SPIFFS 挂载失败: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("storage", &total, &used);
    ESP_LOGI(TAG_DATALOG, "SPIFFS: total=%uKB used=%uKB",
             (unsigned)(total / 1024), (unsigned)(used / 1024));

    /* 尝试打开现有日志文件 */
    FILE *f = fopen(DATA_LOG_SPIFFS_PATH, "rb");
    if (f) {
        if (header_read(f) == ESP_OK) {
            ESP_LOGI(TAG_DATALOG, "加载现有日志: %lu 条记录", s_header.total_count);
            fclose(f);
            s_initialized = true;
            return ESP_OK;
        }
        fclose(f);
    }

    /* 创建新日志文件 */
    f = fopen(DATA_LOG_SPIFFS_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG_DATALOG, "创建日志文件失败");
        return ESP_FAIL;
    }

    memset(&s_header, 0, sizeof(s_header));
    s_header.magic   = LOG_MAGIC;
    s_header.version = LOG_VERSION;
    header_write(f);

    /* 预分配空间 */
    log_record_t empty = {0};
    for (int i = 0; i < MAX_RECORDS; i++) {
        fwrite(&empty, 1, RECORD_SIZE, f);
    }
    fclose(f);

    ESP_LOGI(TAG_DATALOG, "日志文件已创建 (容量 %d 条)", MAX_RECORDS);
    s_initialized = true;
    return ESP_OK;
}

esp_err_t data_logger_write(const env_data_t *data)
{
    if (!data || !s_initialized) return ESP_ERR_INVALID_STATE;

    log_record_t rec = {
        .unix_time   = data->unix_time,
        .temperature = (int16_t)(data->temperature * 100.0f),
        .humidity    = (uint16_t)(data->humidity * 100.0f),
        .pressure    = (uint16_t)data->pressure,
        .co2_ppm     = (uint16_t)(data->co2_ppm > 65535 ? 65535 : data->co2_ppm),
        .pm1_0       = data->pm1_0,
        .pm2_5       = data->pm2_5,
        .pm10        = data->pm10,
        .aqi_level   = data->aqi_level,
        .alarm_bits  = (uint8_t)(alarm_manager_get_active_bits() & 0xFF),
        .health_bits = (uint8_t)(
            (data->status_sht31   & 0x03) |
            ((data->status_bme280  & 0x03) << 2) |
            ((data->status_mhz19   & 0x03) << 4) |
            ((data->status_pms5003 & 0x03) << 6)),
    };

    FILE *f = fopen(DATA_LOG_SPIFFS_PATH, "r+b");
    if (!f) {
        ESP_LOGE(TAG_DATALOG, "打开日志文件失败");
        return ESP_FAIL;
    }

    fseek(f, record_offset(s_header.write_idx), SEEK_SET);
    fwrite(&rec, 1, RECORD_SIZE, f);

    s_header.write_idx = (s_header.write_idx + 1) % MAX_RECORDS;
    s_header.total_count++;
    header_write(f);
    fclose(f);

    ESP_LOGD(TAG_DATALOG, "记录已写入 (总计 %lu 条)", s_header.total_count);
    return ESP_OK;
}

esp_err_t data_logger_read_latest(log_record_t *buf, uint32_t count, uint32_t *actual)
{
    if (!buf || !actual || !s_initialized) return ESP_ERR_INVALID_ARG;

    uint32_t avail = (s_header.total_count < MAX_RECORDS) ?
                     s_header.total_count : MAX_RECORDS;
    *actual = (count < avail) ? count : avail;

    if (*actual == 0) return ESP_OK;

    FILE *f = fopen(DATA_LOG_SPIFFS_PATH, "rb");
    if (!f) return ESP_FAIL;

    /* 从最旧的记录开始读取 */
    uint32_t start_idx;
    if (s_header.total_count <= MAX_RECORDS) {
        start_idx = (s_header.write_idx + MAX_RECORDS - *actual) % MAX_RECORDS;
    } else {
        start_idx = (s_header.write_idx + MAX_RECORDS - *actual) % MAX_RECORDS;
    }

    for (uint32_t i = 0; i < *actual; i++) {
        uint32_t idx = (start_idx + i) % MAX_RECORDS;
        fseek(f, record_offset(idx), SEEK_SET);
        fread(&buf[i], 1, RECORD_SIZE, f);
    }
    fclose(f);
    return ESP_OK;
}

uint32_t data_logger_get_count(void)
{
    return s_initialized ? s_header.total_count : 0;
}

esp_err_t data_logger_clear(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    memset(&s_header, 0, sizeof(s_header));
    s_header.magic   = LOG_MAGIC;
    s_header.version = LOG_VERSION;

    FILE *f = fopen(DATA_LOG_SPIFFS_PATH, "r+b");
    if (!f) return ESP_FAIL;
    header_write(f);
    fclose(f);
    ESP_LOGI(TAG_DATALOG, "日志已清空");
    return ESP_OK;
}

void data_logger_task(void *arg)
{
    ESP_LOGI(TAG_DATALOG, "数据记录任务启动");
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        env_data_t data;
        sensor_manager_get_latest(&data);
        if (data.valid) {
            data_logger_write(&data);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(DATA_LOG_MS));
    }
}