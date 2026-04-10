#include "power/battery_monitor.h"

#include <string.h>

#include "config.h"
#include "esp_log.h"

#if BATTERY_MONITOR_ENABLED
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#endif

static const char *TAG = "BATTERY";
static bool s_init_done = false;

#if BATTERY_MONITOR_ENABLED
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_cali_handle = NULL;
static adc_unit_t s_adc_unit = ADC_UNIT_1;
static adc_channel_t s_adc_channel = ADC_CHANNEL_0;
static bool s_monitor_ready = false;
static bool s_cali_ready = false;

static adc_atten_t battery_monitor_adc_atten(void)
{
#if BATTERY_ADC_ATTEN_DB >= 12
    return ADC_ATTEN_DB_12;
#elif BATTERY_ADC_ATTEN_DB >= 6
    return ADC_ATTEN_DB_6;
#elif BATTERY_ADC_ATTEN_DB >= 3
    return ADC_ATTEN_DB_2_5;
#else
    return ADC_ATTEN_DB_0;
#endif
}

static uint32_t battery_monitor_full_scale_mv(adc_atten_t atten)
{
    switch (atten) {
    case ADC_ATTEN_DB_0:
        return 950U;
    case ADC_ATTEN_DB_2_5:
        return 1250U;
    case ADC_ATTEN_DB_6:
        return 1750U;
    case ADC_ATTEN_DB_12:
    default:
        return 3100U;
    }
}
#endif

esp_err_t battery_monitor_init(void)
{
    if (s_init_done) {
#if BATTERY_MONITOR_ENABLED
        return s_monitor_ready ? ESP_OK : ESP_FAIL;
#else
        return ESP_OK;
#endif
    }

    s_init_done = true;

#if !BATTERY_MONITOR_ENABLED
    ESP_LOGI(TAG, "Battery monitor disabled for %s", DISPLAY_BOARD_NAME);
    return ESP_OK;
#else
    const adc_atten_t atten = battery_monitor_adc_atten();

    esp_err_t ret = adc_oneshot_io_to_channel(BATTERY_ADC_GPIO, &s_adc_unit, &s_adc_channel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Battery ADC GPIO%d is not a valid ADC pad: %s",
                 BATTERY_ADC_GPIO, esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {0};
    unit_cfg.unit_id = s_adc_unit;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    ret = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Create ADC oneshot unit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ret = adc_oneshot_config_channel(s_adc_handle, s_adc_channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Config ADC channel failed: %s", esp_err_to_name(ret));
        (void)adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return ret;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = s_adc_unit,
        .chan = s_adc_channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
    if (ret == ESP_OK) {
        s_cali_ready = true;
    } else {
        ESP_LOGW(TAG, "ADC calibration unavailable, using approximate conversion: %s",
                 esp_err_to_name(ret));
    }
#else
    ESP_LOGW(TAG, "ADC calibration scheme unsupported, using approximate conversion");
#endif

    s_monitor_ready = true;
    ESP_LOGI(TAG, "Battery monitor ready on GPIO%d (ADC%d channel %d)",
             BATTERY_ADC_GPIO, s_adc_unit == ADC_UNIT_2 ? 2 : 1, (int)s_adc_channel);
    return ESP_OK;
#endif
}

bool battery_monitor_get_status(battery_status_t *status)
{
    if (!status) {
        return false;
    }

    memset(status, 0, sizeof(*status));

    if (!s_init_done) {
        (void)battery_monitor_init();
    }

#if !BATTERY_MONITOR_ENABLED
    return false;
#else
    if (!s_monitor_ready || !s_adc_handle) {
        return false;
    }

    const adc_atten_t atten = battery_monitor_adc_atten();
    uint64_t sensed_mv_sum = 0;
    uint32_t sample_count = 0;

    for (int i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
        int sensed_mv = 0;
        esp_err_t ret = ESP_OK;

        if (s_cali_ready && s_cali_handle) {
            ret = adc_oneshot_get_calibrated_result(s_adc_handle, s_cali_handle,
                                                    s_adc_channel, &sensed_mv);
        } else {
            int raw = 0;
            ret = adc_oneshot_read(s_adc_handle, s_adc_channel, &raw);
            if (ret == ESP_OK) {
                sensed_mv = (int)(((int64_t)raw * battery_monitor_full_scale_mv(atten)) / 4095);
            }
        }

        if (ret == ESP_OK && sensed_mv > 0) {
            sensed_mv_sum += (uint32_t)sensed_mv;
            sample_count++;
        }
    }

    if (sample_count == 0) {
        return false;
    }

    uint32_t sensed_mv = (uint32_t)(sensed_mv_sum / sample_count);
    uint32_t battery_mv = (sensed_mv * BATTERY_ADC_DIVIDER_NUM) / BATTERY_ADC_DIVIDER_DEN;
    uint32_t span_mv = BATTERY_FULL_MV > BATTERY_EMPTY_MV ? (BATTERY_FULL_MV - BATTERY_EMPTY_MV) : 1U;
    uint32_t level = 0;

    if (battery_mv < BATTERY_VOLTAGE_MIN_VALID_MV || battery_mv > BATTERY_VOLTAGE_MAX_VALID_MV) {
        return false;
    }

    if (battery_mv <= BATTERY_EMPTY_MV) {
        level = 0;
    } else if (battery_mv >= BATTERY_FULL_MV) {
        level = 100;
    } else {
        level = ((battery_mv - BATTERY_EMPTY_MV) * 100U) / span_mv;
    }

    status->available = true;
    status->charging = false;
    status->level_percent = (uint8_t)level;
    status->voltage_mv = (uint16_t)battery_mv;
    return true;
#endif
}
