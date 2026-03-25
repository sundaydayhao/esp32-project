/**
 * @file alarm_manager.c
 * @brief 报警管理器实现
 */
#include "alarm_manager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <time.h>

/* ---- 迟滞系数（防止在阈值附近频繁触发） ---- */
#define ALARM_HYSTERESIS_TEMP   0.5f    /* °C  */
#define ALARM_HYSTERESIS_HUMI   1.0f    /* %RH */
#define ALARM_HYSTERESIS_CO2    50.0f   /* ppm */
#define ALARM_HYSTERESIS_PM     5.0f    /* µg/m³ */
#define ALARM_HYSTERESIS_PRES   2.0f    /* hPa */

/* ---- 连续触发次数才确认报警（防抖） ---- */
#define ALARM_DEBOUNCE_COUNT    3

typedef struct {
    bool     active;
    uint8_t  trigger_count;
    uint8_t  recover_count;
} alarm_state_t;

static alarm_thresholds_t s_thresh;
static alarm_state_t      s_states[ALARM_ID_COUNT];
static uint32_t           s_active_bits = 0;
static alarm_callback_t   s_callback    = NULL;
static QueueHandle_t      s_event_queue;

/* ---- 从 NVS 加载阈值 ---- */
static void load_thresholds(void)
{
    alarm_thresholds_t def = ALARM_THRESH_DEFAULT;
    nvs_handle_t h;
    if (nvs_open("alarm_cfg", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(alarm_thresholds_t);
        if (nvs_get_blob(h, "thresholds", &s_thresh, &len) != ESP_OK) {
            s_thresh = def;
        }
        nvs_close(h);
    } else {
        s_thresh = def;
    }
}

static void save_thresholds(void)
{
    nvs_handle_t h;
    if (nvs_open("alarm_cfg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "thresholds", &s_thresh, sizeof(alarm_thresholds_t));
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ---- 触发/恢复报警 ---- */
static void fire_alarm(alarm_id_t id, const char *name,
                       float value, float threshold,
                       bool active, uint8_t severity)
{
    alarm_event_t evt = {
        .alarm_id  = id,
        .value     = value,
        .threshold = threshold,
        .active    = active,
        .severity  = severity,
        .timestamp = (int64_t)(esp_timer_get_time() / 1000000),
    };
    strlcpy(evt.param_name, name, sizeof(evt.param_name));

    if (active) {
        s_active_bits |= (1u << id);
        ESP_LOGW(TAG_ALARM, "[触发] %s=%.2f 阈值=%.2f", name, value, threshold);
    } else {
        s_active_bits &= ~(1u << id);
        ESP_LOGI(TAG_ALARM, "[恢复] %s=%.2f", name, value);
    }

    /* 发送到事件队列（非阻塞） */
    xQueueSend(s_event_queue, &evt, 0);

    /* 调用注册的回调 */
    if (s_callback) s_callback(&evt);
}

/* ---- 通用阈值检查（含迟滞和防抖） ---- */
static void check_high(alarm_id_t id, const char *name,
                       float value, float high, float hyst, uint8_t severity)
{
    alarm_state_t *st = &s_states[id];
    if (!st->active) {
        if (value > high) {
            st->trigger_count++;
            st->recover_count = 0;
            if (st->trigger_count >= ALARM_DEBOUNCE_COUNT) {
                st->active = true;
                st->trigger_count = 0;
                fire_alarm(id, name, value, high, true, severity);
            }
        } else {
            st->trigger_count = 0;
        }
    } else {
        if (value < high - hyst) {
            st->recover_count++;
            if (st->recover_count >= ALARM_DEBOUNCE_COUNT) {
                st->active = false;
                st->recover_count = 0;
                fire_alarm(id, name, value, high, false, severity);
            }
        } else {
            st->recover_count = 0;
        }
    }
}

static void check_low(alarm_id_t id, const char *name,
                      float value, float low, float hyst, uint8_t severity)
{
    alarm_state_t *st = &s_states[id];
    if (!st->active) {
        if (value < low) {
            st->trigger_count++;
            st->recover_count = 0;
            if (st->trigger_count >= ALARM_DEBOUNCE_COUNT) {
                st->active = true;
                st->trigger_count = 0;
                fire_alarm(id, name, value, low, true, severity);
            }
        } else {
            st->trigger_count = 0;
        }
    } else {
        if (value > low + hyst) {
            st->recover_count++;
            if (st->recover_count >= ALARM_DEBOUNCE_COUNT) {
                st->active = false;
                st->recover_count = 0;
                fire_alarm(id, name, value, low, false, severity);
            }
        } else {
            st->recover_count = 0;
        }
    }
}

esp_err_t alarm_manager_init(void)
{
    s_event_queue = xQueueCreate(16, sizeof(alarm_event_t));
    configASSERT(s_event_queue);

    memset(s_states, 0, sizeof(s_states));
    s_active_bits = 0;
    load_thresholds();

    ESP_LOGI(TAG_ALARM, "报警管理器初始化完成");
    ESP_LOGI(TAG_ALARM, "阈值: T[%.1f~%.1f] RH[%.1f~%.1f] CO2<%.0f PM2.5<%.0f",
             s_thresh.temp_low, s_thresh.temp_high,
             s_thresh.humi_low, s_thresh.humi_high,
             s_thresh.co2_high, s_thresh.pm25_high);
    return ESP_OK;
}

void alarm_manager_check(const env_data_t *data)
{
    if (!data || !data->valid) return;

    check_high(ALARM_ID_TEMP_HIGH, "temp",     data->temperature, s_thresh.temp_high,  ALARM_HYSTERESIS_TEMP, 2);
    check_low (ALARM_ID_TEMP_LOW,  "temp",     data->temperature, s_thresh.temp_low,   ALARM_HYSTERESIS_TEMP, 2);
    check_high(ALARM_ID_HUMI_HIGH, "humidity", data->humidity,    s_thresh.humi_high,  ALARM_HYSTERESIS_HUMI, 1);
    check_low (ALARM_ID_HUMI_LOW,  "humidity", data->humidity,    s_thresh.humi_low,   ALARM_HYSTERESIS_HUMI, 1);
    check_high(ALARM_ID_CO2_HIGH,  "co2_ppm",  (float)data->co2_ppm, s_thresh.co2_high, ALARM_HYSTERESIS_CO2, 2);
    check_high(ALARM_ID_PM25_HIGH, "pm2_5",    (float)data->pm2_5,   s_thresh.pm25_high, ALARM_HYSTERESIS_PM, 2);
    check_high(ALARM_ID_PM10_HIGH, "pm10",     (float)data->pm10,    s_thresh.pm10_high, ALARM_HYSTERESIS_PM, 1);
    check_high(ALARM_ID_PRES_HIGH, "pressure", data->pressure,   s_thresh.pressure_high, ALARM_HYSTERESIS_PRES, 1);
    check_low (ALARM_ID_PRES_LOW,  "pressure", data->pressure,   s_thresh.pressure_low,  ALARM_HYSTERESIS_PRES, 1);
}

uint32_t alarm_manager_get_active_bits(void)
{
    return s_active_bits;
}

esp_err_t alarm_manager_set_thresholds(const alarm_thresholds_t *thresh)
{
    if (!thresh) return ESP_ERR_INVALID_ARG;
    s_thresh = *thresh;
    save_thresholds();
    ESP_LOGI(TAG_ALARM, "报警阈值已更新");
    return ESP_OK;
}

void alarm_manager_get_thresholds(alarm_thresholds_t *thresh)
{
    if (thresh) *thresh = s_thresh;
}

void alarm_manager_register_callback(alarm_callback_t cb)
{
    s_callback = cb;
}

void alarm_task(void *arg)
{
    ESP_LOGI(TAG_ALARM, "报警检查任务启动");
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        env_data_t data;
        sensor_manager_get_latest(&data);
        alarm_manager_check(&data);

        /* 控制报警 LED 和蜂鸣器 */
        bool any_alarm = (s_active_bits != 0);
        gpio_set_level(LED_ALARM_PIN, any_alarm ? 1 : 0);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ALARM_CHECK_MS));
    }
}