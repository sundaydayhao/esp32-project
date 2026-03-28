/**
 * @file signal_generator.h
 * @brief ESP32-S3 Signal Generator - Header File
 *
 * Generates sine, square, triangle, and sawtooth waveforms
 * using LEDC PWM with dynamic duty cycle updates via esp_timer.
 *
 * Hardware:
 *   - Output pin: GPIO_NUM_1 (default, configurable)
 *   - PWM carrier: 78.125 kHz, 10-bit resolution
 *   - Sample rate: 40 kHz
 *   - Frequency range: 1 Hz ~ 20 kHz
 *
 * Usage:
 *   signal_generator_init();
 *   signal_generator_set_wave(WAVE_SINE);
 *   signal_generator_set_frequency(1000.0f);
 *   signal_generator_set_amplitude(0.8f);
 *   signal_generator_start();
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/ledc.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Configuration ──────────────────────────────────────────────── */

/** GPIO pin used for PWM output (connect RC low-pass filter for analog) */
#define SIGGEN_OUTPUT_GPIO      1

/** LEDC PWM carrier frequency in Hz.
 *  78,125 Hz is the highest APB-clocked rate that still supports 10-bit duty.
 */
#define SIGGEN_LEDC_FREQ_HZ     78125

/** LEDC duty resolution bits (10-bit = 1024 levels) */
#define SIGGEN_LEDC_DUTY_RES_BITS 10U

/** LEDC duty resolution enum used by the driver */
#define SIGGEN_LEDC_DUTY_BITS   LEDC_TIMER_10_BIT

/** Maximum duty value (2^10 - 1 = 1023) */
#define SIGGEN_DUTY_MAX         ((1U << SIGGEN_LEDC_DUTY_RES_BITS) - 1U)

/** Sample rate for waveform generation in Hz */
#define SIGGEN_SAMPLE_RATE_HZ   40000

/** Minimum output frequency in Hz */
#define SIGGEN_FREQ_MIN         1.0f

/** Maximum output frequency in Hz (Nyquist limit) */
#define SIGGEN_FREQ_MAX         20000.0f

/** Sine lookup table size (power of 2 for fast modulo) */
#define SIGGEN_SINE_TABLE_SIZE  1024

/* ─── Types ──────────────────────────────────────────────────────── */

/**
 * @brief Waveform type enumeration
 */
typedef enum {
    WAVE_SINE      = 0,  /**< Sinusoidal wave                  */
    WAVE_SQUARE    = 1,  /**< Square wave (50% duty cycle)     */
    WAVE_TRIANGLE  = 2,  /**< Triangle wave (linear ramp up/down) */
    WAVE_SAWTOOTH  = 3,  /**< Sawtooth wave (linear ramp up)   */
    WAVE_MAX             /**< Sentinel – do not use            */
} wave_type_t;

/**
 * @brief Signal generator configuration snapshot
 */
typedef struct {
    wave_type_t wave_type;   /**< Current waveform type          */
    float       frequency;   /**< Output frequency in Hz         */
    float       amplitude;   /**< Amplitude 0.0 (off) – 1.0 (full) */
    float       offset;      /**< DC offset  0.0 – 1.0           */
    bool        running;     /**< true = timer active            */
} siggen_config_t;

/* ─── API ────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise LEDC peripheral and build sine lookup table.
 *         Must be called once before any other function.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t signal_generator_init(void);

/**
 * @brief  Start waveform output (enables the sample timer).
 * @return ESP_OK on success.
 */
esp_err_t signal_generator_start(void);

/**
 * @brief  Stop waveform output (disables the sample timer, output = 0).
 * @return ESP_OK on success.
 */
esp_err_t signal_generator_stop(void);

/**
 * @brief  Select waveform type.
 * @param  wave  One of WAVE_SINE, WAVE_SQUARE, WAVE_TRIANGLE, WAVE_SAWTOOTH.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if wave is out of range.
 */
esp_err_t signal_generator_set_wave(wave_type_t wave);

/**
 * @brief  Set output frequency.
 * @param  freq_hz  Frequency in Hz [SIGGEN_FREQ_MIN .. SIGGEN_FREQ_MAX].
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range.
 */
esp_err_t signal_generator_set_frequency(float freq_hz);

/**
 * @brief  Set output amplitude.
 * @param  amplitude  0.0 (no output) to 1.0 (full scale).
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range.
 */
esp_err_t signal_generator_set_amplitude(float amplitude);

/**
 * @brief  Set DC offset.
 * @param  offset  0.0 to 1.0 (0.5 = mid-rail, no DC offset).
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range.
 */
esp_err_t signal_generator_set_offset(float offset);

/**
 * @brief  Get a snapshot of the current configuration.
 * @param  cfg  Pointer to caller-allocated siggen_config_t.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if cfg is NULL.
 */
esp_err_t signal_generator_get_config(siggen_config_t *cfg);

/**
 * @brief  Return a human-readable name for a waveform type.
 * @param  wave  Waveform type.
 * @return Constant string, e.g. "Sine".
 */
const char *signal_generator_wave_name(wave_type_t wave);

#ifdef __cplusplus
}
#endif
