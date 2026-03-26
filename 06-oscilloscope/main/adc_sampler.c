#include "adc_sampler.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "adc_sampler";

/*
 * Keep the sampler task cooperative enough for the CPU1 idle task to run.
 * esp_rom_delay_us() is a busy wait, so without an explicit delay the task
 * can monopolize the core long enough to trip the task watchdog.
 */
#define ADC_SCHEDULER_YIELD_INTERVAL_US      20000
#define ADC_SCHEDULER_CHECK_INTERVAL_SAMPLES 16

/* ADC handles */
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_ch0_handle = NULL;
static adc_cali_handle_t adc1_cali_ch1_handle = NULL;

/* Double buffer for lock-free reading */
static adc_frame_t frame_buf[2];
static volatile int write_idx = 0;
static volatile int read_idx  = 1;
static volatile bool new_frame_ready = false;

static SemaphoreHandle_t frame_mutex = NULL;
static TaskHandle_t sampler_task_handle = NULL;

static volatile uint32_t sample_rate_hz = ADC_SAMPLE_RATE_HZ;

static inline void adc_sampler_maybe_yield(uint32_t sample_idx,
                                           int64_t *last_scheduler_yield_us,
                                           int64_t *last_time_us)
{
    if (((sample_idx + 1U) % ADC_SCHEDULER_CHECK_INTERVAL_SAMPLES) != 0U) {
        return;
    }

    int64_t now = esp_timer_get_time();
    if ((now - *last_scheduler_yield_us) < ADC_SCHEDULER_YIELD_INTERVAL_US) {
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1));

    now = esp_timer_get_time();
    *last_scheduler_yield_us = now;
    *last_time_us = now;
}

/* ------------------------------------------------------------------ */
/* ADC calibration                                                      */
/* ------------------------------------------------------------------ */
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel,
                                  adc_atten_t atten,
                                  adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id  = unit,
            .chan     = channel,
            .atten    = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id  = unit,
            .atten    = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration success (unit %d, ch %d)", unit, channel);
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "ADC calibration not supported, using raw values");
    } else {
        ESP_LOGE(TAG, "ADC calibration failed: %s", esp_err_to_name(ret));
    }
    return calibrated;
}

/* ------------------------------------------------------------------ */
/* Sampler task                                                         */
/* ------------------------------------------------------------------ */
static void adc_sampler_task(void *pvParameters)
{
    int raw_ch1, raw_ch2;
    uint32_t sample_idx = 0;
    int64_t last_time_us = esp_timer_get_time();
    int64_t last_scheduler_yield_us = last_time_us;

    ESP_LOGI(TAG, "ADC sampler task started, rate=%lu Hz", sample_rate_hz);

    while (1) {
        adc_frame_t *wbuf = &frame_buf[write_idx];
        uint32_t rate_hz = sample_rate_hz;
        uint32_t period_us = 1000000UL / rate_hz;

        /* Sample ADC_BUFFER_SIZE points */
        for (sample_idx = 0; sample_idx < ADC_BUFFER_SIZE; sample_idx++) {
            /* Read CH1 */
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_0, &raw_ch1));
            /* Read CH2 */
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_1, &raw_ch2));

            wbuf->ch1[sample_idx] = (uint16_t)raw_ch1;
            wbuf->ch2[sample_idx] = (uint16_t)raw_ch2;

            /* Rate limiting: delay between samples */
            if (period_us > 0U) {
                int64_t now = esp_timer_get_time();
                int64_t elapsed = now - last_time_us;
                int64_t wait = (int64_t)period_us - elapsed;
                if (wait > 0) {
                    esp_rom_delay_us((uint32_t)wait);
                    last_time_us = esp_timer_get_time();
                } else {
                    last_time_us = now;
                }
            }

            adc_sampler_maybe_yield(sample_idx,
                                    &last_scheduler_yield_us,
                                    &last_time_us);
        }

        /* Calculate actual sample rate */
        int64_t now_us = esp_timer_get_time();
        static int64_t frame_start_us = 0;
        if (frame_start_us > 0) {
            int64_t frame_duration = now_us - frame_start_us;
            if (frame_duration > 0) {
                wbuf->sample_rate = (uint32_t)((int64_t)ADC_BUFFER_SIZE * 1000000LL / frame_duration);
            }
        }
        frame_start_us = now_us;
        wbuf->timestamp_ms = (uint32_t)(now_us / 1000);

        /* Swap buffers */
        xSemaphoreTake(frame_mutex, portMAX_DELAY);
        int tmp = write_idx;
        write_idx = read_idx;
        read_idx = tmp;
        new_frame_ready = true;
        xSemaphoreGive(frame_mutex);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */
void adc_sampler_init(void)
{
    /* Create mutex */
    frame_mutex = xSemaphoreCreateMutex();
    assert(frame_mutex != NULL);

    /* Initialize ADC1 */
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    /* Configure CH1 (ADC1_CH0 = GPIO1) */
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_0, &chan_config));

    /* Configure CH2 (ADC1_CH1 = GPIO2) */
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_1, &chan_config));

    /* Calibration */
    adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_0, ADC_ATTEN_DB_12, &adc1_cali_ch0_handle);
    adc_calibration_init(ADC_UNIT_1, ADC_CHANNEL_1, ADC_ATTEN_DB_12, &adc1_cali_ch1_handle);

    /* Initialize frame buffers */
    memset(frame_buf, 0, sizeof(frame_buf));
    frame_buf[0].sample_rate = sample_rate_hz;
    frame_buf[1].sample_rate = sample_rate_hz;

    /* Start sampler task on core 1 */
    xTaskCreatePinnedToCore(
        adc_sampler_task,
        "adc_sampler",
        4096,
        NULL,
        5,
        &sampler_task_handle,
        1   /* Run on core 1, HTTP server on core 0 */
    );

    ESP_LOGI(TAG, "ADC sampler initialized. CH1=GPIO%d, CH2=GPIO%d",
             ADC_CH1_GPIO, ADC_CH2_GPIO);
}

esp_err_t adc_sampler_get_frame(adc_frame_t *frame)
{
    if (!new_frame_ready) {
        return ESP_ERR_NOT_FOUND;
    }

    xSemaphoreTake(frame_mutex, portMAX_DELAY);
    memcpy(frame, &frame_buf[read_idx], sizeof(adc_frame_t));
    new_frame_ready = false;
    xSemaphoreGive(frame_mutex);

    return ESP_OK;
}

void adc_sampler_set_rate(uint32_t rate_hz)
{
    if (rate_hz < 100) rate_hz = 100;
    if (rate_hz > ADC_MAX_SAMPLE_RATE_HZ) rate_hz = ADC_MAX_SAMPLE_RATE_HZ;
    sample_rate_hz = rate_hz;
    ESP_LOGI(TAG, "Sample rate set to %lu Hz", rate_hz);
}

uint32_t adc_sampler_get_rate(void)
{
    return sample_rate_hz;
}
