/**
 * @file main.c
 * @brief ADC 模拟读取示例
 *
 * 功能：
 *   - 使用 ADC1 读取模拟电压值
 *   - 使用校准功能提高精度
 *   - 读取电位器或传感器的模拟值
 *
 * 硬件连接：
 *   电位器中间脚 → GPIO34 (ADC1_CH6)
 *   电位器一端   → 3.3V
 *   电位器另一端 → GND
 *
 * 注意：GPIO34 是仅输入引脚，适合 ADC 使用
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "ADC";

/* ADC 配置 */
#define ADC_CHANNEL     ADC_CHANNEL_6   // GPIO34
#define ADC_ATTEN       ADC_ATTEN_DB_12 // 衰减 12dB，量程 0~3.3V
#define ADC_UNIT        ADC_UNIT_1      // 使用 ADC1

/* 多次采样取平均，减少噪声 */
#define ADC_SAMPLES     64

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool do_calibration = false;

/**
 * @brief 初始化 ADC 校准
 */
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel,
                                  adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    /* 使用曲线拟合校准方案（ESP32-S2/S3/C3 支持） */
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
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    /* 使用线性拟合校准方案（ESP32 支持） */
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
    if (calibrated) {
        ESP_LOGI(TAG, "ADC 校准成功");
    } else {
        ESP_LOGW(TAG, "ADC 校准不支持，使用原始值");
    }

    return calibrated;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ADC 示例启动");

    /* 初始化 ADC oneshot 模式 */
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    /* 配置 ADC 通道 */
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,  // 默认位宽（12位）
        .atten    = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config));

    /* 初始化校准 */
    do_calibration = adc_calibration_init(ADC_UNIT, ADC_CHANNEL, ADC_ATTEN,
                                           &adc_cali_handle);

    ESP_LOGI(TAG, "ADC1 通道%d (GPIO34) 初始化完成", ADC_CHANNEL);

    while (1) {
        int adc_raw = 0;
        int voltage = 0;

        /* 多次采样取平均 */
        int sum = 0;
        for (int i = 0; i < ADC_SAMPLES; i++) {
            int raw;
            ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw));
            sum += raw;
        }
        adc_raw = sum / ADC_SAMPLES;

        /* 转换为电压值 */
        if (do_calibration) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage));
            ESP_LOGI(TAG, "ADC 原始值: %4d, 电压: %4d mV (%.2f V)",
                     adc_raw, voltage, voltage / 1000.0f);
        } else {
            /* 无校准时的简单换算（不精确） */
            voltage = (adc_raw * 3300) / 4095;
            ESP_LOGI(TAG, "ADC 原始值: %4d, 估算电压: %4d mV (%.2f V)",
                     adc_raw, voltage, voltage / 1000.0f);
        }

        /* 显示百分比（0~100%） */
        int percent = (adc_raw * 100) / 4095;
        ESP_LOGI(TAG, "百分比: %3d%% [%s]",
                 percent,
                 percent > 75 ? "████████" :
                 percent > 50 ? "██████  " :
                 percent > 25 ? "████    " : "██      ");

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}