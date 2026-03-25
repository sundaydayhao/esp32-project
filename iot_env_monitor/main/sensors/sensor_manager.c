/**
 * @file sensor_manager.c
 * @brief 传感器管理器实现
 */
#include "sensor_manager.h"
#include "sht31.h"
#include "bme280.h"
#include "mhz19.h"
#include "pms5003.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

/* ---- 滑动窗口滤波器 ---- */
typedef struct {
    float    buf[SENSOR_FILTER_WINDOW];
    uint8_t  idx;
    uint8_t  count;
} filter_t;

static float filter_push(filter_t *f, float val)
{
    f->buf[f->idx] = val;
    f->idx = (f->idx + 1) % SENSOR_FILTER_WINDOW;
    if (f->count < SENSOR_FILTER_WINDOW) f->count++;
    float sum = 0;
    for (int i = 0; i < f->count; i++) sum += f->buf[i];
    return sum / f->count;
}

/* ---- 传感器失败计数 ---- */
typedef struct {
    uint8_t fail_count;
    sensor_status_t status;
} sensor_health_t;

static void health_update(sensor_health_t *h, bool ok)
{
    if (ok) {
        h->fail_count = 0;
        h->status = SENSOR_STATUS_OK;
    } else {
        if (h->status == SENSOR_STATUS_OFFLINE) return;
        h->fail_count++;
        h->status = (h->fail_count >= 3) ? SENSOR_STATUS_ERROR : SENSOR_STATUS_WARN;
        if (h->fail_count >= 10) h->status = SENSOR_STATUS_OFFLINE;
    }
}

/* ---- 模块私有状态 ---- */
static env_data_t       s_latest;
static SemaphoreHandle_t s_mutex;

static filter_t s_filt_temp;
static filter_t s_filt_humi;
static filter_t s_filt_pres;
static filter_t s_filt_co2;
static filter_t s_filt_pm25;

static sensor_health_t s_health_sht31;
static sensor_health_t s_health_bme280;
static sensor_health_t s_health_mhz19;
static sensor_health_t s_health_pms5003;

static bool s_sht31_ok   = false;
static bool s_bme280_ok  = false;
static bool s_mhz19_ok   = false;
static bool s_pms5003_ok = false;

/* ---- I2C 总线初始化 ---- */
static esp_err_t i2c_bus_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_SDA_PIN,
        .scl_io_num       = I2C_SCL_PIN,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    esp_err_t ret = i2c_param_config(I2C_MASTER_PORT, &conf);
    if (ret != ESP_OK) return ret;
    return i2c_driver_install(I2C_MASTER_PORT, I2C_MODE_MASTER, 0, 0, 0);
}

esp_err_t sensor_manager_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex);

    memset(&s_latest, 0, sizeof(s_latest));
    memset(&s_filt_temp,  0, sizeof(filter_t));
    memset(&s_filt_humi,  0, sizeof(filter_t));
    memset(&s_filt_pres,  0, sizeof(filter_t));
    memset(&s_filt_co2,   0, sizeof(filter_t));
    memset(&s_filt_pm25,  0, sizeof(filter_t));

    /* 初始化 I2C 总线 */
    esp_err_t ret = i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SENSOR, "I2C 总线初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG_SENSOR, "I2C 总线初始化成功");

    /* 初始化 SHT31 */
    if (sht31_init(I2C_MASTER_PORT, SHT31_I2C_ADDR) == ESP_OK) {
        s_sht31_ok = true;
        s_health_sht31.status = SENSOR_STATUS_OK;
        ESP_LOGI(TAG_SENSOR, "[OK] SHT31 温湿度传感器");
    } else {
        s_health_sht31.status = SENSOR_STATUS_OFFLINE;
        ESP_LOGW(TAG_SENSOR, "[OFFLINE] SHT31 温湿度传感器");
    }

    /* 初始化 BME280 */
    if (bme280_init(I2C_MASTER_PORT, BME280_I2C_ADDR) == ESP_OK) {
        s_bme280_ok = true;
        s_health_bme280.status = SENSOR_STATUS_OK;
        ESP_LOGI(TAG_SENSOR, "[OK] BME280 气压传感器");
    } else {
        s_health_bme280.status = SENSOR_STATUS_OFFLINE;
        ESP_LOGW(TAG_SENSOR, "[OFFLINE] BME280 气压传感器");
    }

    /* 初始化 MH-Z19B */
    if (mhz19_init(MHZ19_UART_PORT, MHZ19_TX_PIN, MHZ19_RX_PIN) == ESP_OK) {
        s_mhz19_ok = true;
        s_health_mhz19.status = SENSOR_STATUS_OK;
        ESP_LOGI(TAG_SENSOR, "[OK] MH-Z19B CO₂ 传感器");
    } else {
        s_health_mhz19.status = SENSOR_STATUS_OFFLINE;
        ESP_LOGW(TAG_SENSOR, "[OFFLINE] MH-Z19B CO₂ 传感器");
    }

    /* 初始化 PMS5003 */
    if (pms5003_init(PMS5003_UART_PORT, PMS5003_TX_PIN,
                     PMS5003_RX_PIN, PMS5003_SET_PIN) == ESP_OK) {
        s_pms5003_ok = true;
        s_health_pms5003.status = SENSOR_STATUS_OK;
        ESP_LOGI(TAG_SENSOR, "[OK] PMS5003 颗粒物传感器");
    } else {
        s_health_pms5003.status = SENSOR_STATUS_OFFLINE;
        ESP_LOGW(TAG_SENSOR, "[OFFLINE] PMS5003 颗粒物传感器");
    }

    bool any_ok = s_sht31_ok || s_bme280_ok || s_mhz19_ok || s_pms5003_ok;
    ESP_LOGI(TAG_SENSOR, "传感器初始化完成 (SHT31=%d BME280=%d MHZ19=%d PMS=%d)",
             s_sht31_ok, s_bme280_ok, s_mhz19_ok, s_pms5003_ok);
    return any_ok ? ESP_OK : ESP_FAIL;
}

esp_err_t sensor_manager_sample(env_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    env_data_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    /* ---- SHT31 采样 ---- */
    if (s_sht31_ok) {
        sht31_data_t d;
        bool ok = (sht31_read(&d) == ESP_OK && d.valid);
        health_update(&s_health_sht31, ok);
        if (ok) {
            tmp.temperature = filter_push(&s_filt_temp, d.temperature);
            tmp.humidity    = filter_push(&s_filt_humi, d.humidity);
        }
    }
    tmp.status_sht31 = s_health_sht31.status;

    /* ---- BME280 采样 ---- */
    if (s_bme280_ok) {
        bme280_data_t d;
        bool ok = (bme280_read(&d) == ESP_OK && d.valid);
        health_update(&s_health_bme280, ok);
        if (ok) {
            tmp.pressure = filter_push(&s_filt_pres, d.pressure);
            tmp.altitude = d.altitude;
            /* 若 SHT31 离线，使用 BME280 温湿度作为备份 */
            if (!s_sht31_ok || s_health_sht31.status >= SENSOR_STATUS_ERROR) {
                tmp.temperature = filter_push(&s_filt_temp, d.temperature);
                tmp.humidity    = filter_push(&s_filt_humi, d.humidity);
            }
        }
    }
    tmp.status_bme280 = s_health_bme280.status;

    /* ---- MH-Z19B 采样 ---- */
    if (s_mhz19_ok) {
        mhz19_data_t d;
        bool ok = (mhz19_read(&d) == ESP_OK && d.valid);
        health_update(&s_health_mhz19, ok);
        if (ok) {
            tmp.co2_ppm = (int32_t)filter_push(&s_filt_co2, (float)d.co2_ppm);
        }
    }
    tmp.status_mhz19 = s_health_mhz19.status;

    /* ---- PMS5003 采样 ---- */
    if (s_pms5003_ok) {
        pms5003_data_t d;
        bool ok = (pms5003_read(&d) == ESP_OK && d.valid);
        health_update(&s_health_pms5003, ok);
        if (ok) {
            tmp.pm2_5      = (uint16_t)filter_push(&s_filt_pm25, (float)d.pm2_5_atm);
            tmp.pm1_0      = d.pm1_0_atm;
            tmp.pm10       = d.pm10_atm;
            tmp.aqi_level  = d.aqi_level;
        }
    }
    tmp.status_pms5003 = s_health_pms5003.status;

    tmp.valid = true;

    /* 线程安全写入缓存 */
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(&s_latest, &tmp, sizeof(env_data_t));
        xSemaphoreGive(s_mutex);
    }

    memcpy(out, &tmp, sizeof(env_data_t));
    return ESP_OK;
}

void sensor_manager_get_latest(env_data_t *out)
{
    if (!out) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(out, &s_latest, sizeof(env_data_t));
        xSemaphoreGive(s_mutex);
    }
}

SemaphoreHandle_t sensor_manager_get_mutex(void)
{
    return s_mutex;
}

void sensor_task(void *arg)
{
    ESP_LOGI(TAG_SENSOR, "传感器采样任务启动");
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        env_data_t data;
        sensor_manager_sample(&data);

        ESP_LOGI(TAG_SENSOR,
                 "T=%.1f°C  RH=%.1f%%  P=%.1fhPa  CO₂=%ldppm  PM2.5=%uµg/m³  AQI=%u",
                 data.temperature, data.humidity, data.pressure,
                 data.co2_ppm, data.pm2_5, data.aqi_level);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_SAMPLE_MS));
    }
}