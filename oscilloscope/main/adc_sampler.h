#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/* ADC configuration */
#define ADC_SAMPLE_RATE_HZ      1000     /* 1 kSPS default to reduce sustained traffic */
#define ADC_MAX_SAMPLE_RATE_HZ  10000    /* hard cap to keep sampling and WS load bounded */
#define ADC_BUFFER_SIZE         256      /* samples per buffer, smaller WS payload */
#define ADC_CHANNEL_COUNT       2        /* CH1 and CH2 */

/* ADC channels: GPIO1 = ADC1_CH0, GPIO2 = ADC1_CH1 on ESP32-S3 */
#define ADC_CH1_GPIO            1
#define ADC_CH2_GPIO            2

/* Voltage reference (mV) */
#define ADC_VREF_MV             3300

/**
 * @brief ADC sample data structure sent over WebSocket
 */
typedef struct {
    uint16_t ch1[ADC_BUFFER_SIZE];  /* Channel 1 raw ADC values (0-4095) */
    uint16_t ch2[ADC_BUFFER_SIZE];  /* Channel 2 raw ADC values (0-4095) */
    uint32_t sample_rate;           /* Actual sample rate in Hz */
    uint32_t timestamp_ms;          /* Timestamp in milliseconds */
} adc_frame_t;

/**
 * @brief Initialize ADC sampler
 */
void adc_sampler_init(void);

/**
 * @brief Get the latest ADC frame (non-blocking)
 * @param frame Pointer to frame structure to fill
 * @return ESP_OK if new data available, ESP_ERR_NOT_FOUND if no new data
 */
esp_err_t adc_sampler_get_frame(adc_frame_t *frame);

/**
 * @brief Set the sample rate
 * @param rate_hz Desired sample rate in Hz (100 - 10000)
 */
void adc_sampler_set_rate(uint32_t rate_hz);

/**
 * @brief Get current sample rate
 */
uint32_t adc_sampler_get_rate(void);
