/**
 * @file signal_generator.c
 * @brief ESP32-S3 Signal Generator – Implementation
 *
 * Waveform generation strategy
 * ─────────────────────────────
 *  A periodic esp_timer fires at SIGGEN_SAMPLE_RATE_HZ (40 kHz).
 *  Each callback advances a 32-bit phase accumulator and maps the
 *  current phase to a LEDC duty value according to the selected
 *  waveform.  The LEDC peripheral runs a PWM carrier at 80 kHz
 *  (10-bit resolution).  An external RC low-pass filter on the
 *  output pin converts the PWM to a smooth analog voltage.
 *
 *  Suggested RC filter:  R = 1 kΩ, C = 100 nF  → fc ≈ 1.6 kHz
 *  For higher fidelity:  R = 1 kΩ, C = 10 nF   → fc ≈ 16 kHz
 *
 *  Phase accumulator
 *  ─────────────────
 *  phase_acc is a 32-bit fixed-point value in [0, 2^32).
 *  It wraps naturally on overflow (unsigned arithmetic).
 *  phase_inc = (freq_hz / sample_rate) * 2^32
 *
 *  Mapping phase → table index (sine):
 *    index = phase_acc >> (32 - log2(TABLE_SIZE))
 *          = phase_acc >> 22   (for TABLE_SIZE = 1024)
 */

#include "signal_generator.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "soc/soc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "siggen";

/* ─── Internal state ─────────────────────────────────────────────── */

/** Pre-computed sine lookup table, values in [0, SIGGEN_DUTY_MAX] */
static uint16_t s_sine_table[SIGGEN_SINE_TABLE_SIZE];

/** Current configuration (protected by critical section for floats) */
static volatile wave_type_t s_wave      = WAVE_SINE;
static volatile float       s_amplitude = 0.8f;
static volatile float       s_offset    = 0.5f;
static volatile bool        s_running   = false;

/**
 * Phase accumulator – 32-bit unsigned, wraps at 2^32.
 * Updated only inside the timer ISR / callback, so no mutex needed.
 */
static volatile uint32_t s_phase_acc = 0;

/**
 * Phase increment per sample = (freq / sample_rate) * 2^32
 * Written atomically as a 32-bit value.
 */
static volatile uint32_t s_phase_inc = 0;

/** esp_timer handle */
static esp_timer_handle_t s_timer = NULL;

/* ─── Helpers ────────────────────────────────────────────────────── */

/**
 * @brief Clamp a float to [lo, hi].
 */
static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/**
 * @brief Convert a normalised waveform sample [-1, +1] to a LEDC duty.
 *
 * duty = (sample * amplitude + offset) * DUTY_MAX
 * Clamped to [0, DUTY_MAX].
 */
static inline uint32_t sample_to_duty(float sample)
{
    float amp    = s_amplitude;   /* read volatile once */
    float offset = s_offset;
    float v      = sample * amp * 0.5f + offset;
    v = clampf(v, 0.0f, 1.0f);
    return (uint32_t)(v * (float)SIGGEN_DUTY_MAX + 0.5f);
}

/* ─── Timer callback (runs at SIGGEN_SAMPLE_RATE_HZ) ─────────────── */

static void IRAM_ATTR siggen_timer_cb(void *arg)
{
    /* Advance phase accumulator */
    s_phase_acc += s_phase_inc;

    /* Map 32-bit phase to table index (top N bits) */
    uint32_t idx = s_phase_acc >> (32 - 10);   /* 10 = log2(1024) */

    float sample = 0.0f;

    switch (s_wave) {

    /* ── Sine ──────────────────────────────────────────────────── */
    case WAVE_SINE:
        /* sine_table stores values in [0, DUTY_MAX]; convert to [-1, +1] */
        sample = ((float)s_sine_table[idx] / (float)(SIGGEN_DUTY_MAX / 2)) - 1.0f;
        break;

    /* ── Square ────────────────────────────────────────────────── */
    case WAVE_SQUARE:
        sample = (s_phase_acc < 0x80000000UL) ? 1.0f : -1.0f;
        break;

    /* ── Triangle ──────────────────────────────────────────────── */
    case WAVE_TRIANGLE:
        /*
         * First half  [0, 0x7FFFFFFF]: ramp  -1 → +1
         * Second half [0x80000000, 0xFFFFFFFF]: ramp +1 → -1
         */
        if (s_phase_acc < 0x80000000UL) {
            sample = ((float)s_phase_acc / (float)0x40000000UL) - 1.0f;
        } else {
            sample = 3.0f - ((float)s_phase_acc / (float)0x40000000UL);
        }
        break;

    /* ── Sawtooth ──────────────────────────────────────────────── */
    case WAVE_SAWTOOTH:
        /* Ramp from -1 to +1 over the full period */
        sample = ((float)s_phase_acc / (float)0x80000000UL) - 1.0f;
        break;

    default:
        sample = 0.0f;
        break;
    }

    uint32_t duty = sample_to_duty(sample);

    /* Update LEDC duty without fade (immediate) */
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* ─── Sine table initialisation ──────────────────────────────────── */

static void build_sine_table(void)
{
    for (int i = 0; i < SIGGEN_SINE_TABLE_SIZE; i++) {
        double angle = 2.0 * M_PI * i / SIGGEN_SINE_TABLE_SIZE;
        /* Map [-1, +1] → [0, DUTY_MAX] */
        s_sine_table[i] = (uint16_t)((sin(angle) + 1.0) * 0.5 * SIGGEN_DUTY_MAX + 0.5);
    }
}

/* ─── Public API ─────────────────────────────────────────────────── */

esp_err_t signal_generator_init(void)
{
    ESP_LOGI(TAG, "Initialising signal generator on GPIO %d", SIGGEN_OUTPUT_GPIO);

    uint32_t duty_steps = 1U << SIGGEN_LEDC_DUTY_RES_BITS;
    uint64_t required_clock_hz = (uint64_t)SIGGEN_LEDC_FREQ_HZ * duty_steps;

    if (required_clock_hz > APB_CLK_FREQ) {
        ESP_LOGE(TAG,
                 "Invalid LEDC config: freq=%d Hz, duty_bits=%u requires %" PRIu64
                 " Hz clock, APB provides %d Hz",
                 SIGGEN_LEDC_FREQ_HZ,
                 SIGGEN_LEDC_DUTY_RES_BITS,
                 required_clock_hz,
                 APB_CLK_FREQ);
        return ESP_ERR_INVALID_ARG;
    }

    /* Build sine lookup table */
    build_sine_table();

    /* ── Configure LEDC timer ──────────────────────────────────── */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = SIGGEN_LEDC_DUTY_BITS,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = SIGGEN_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_USE_APB_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }

    /* ── Configure LEDC channel ────────────────────────────────── */
    ledc_channel_config_t ch_cfg = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = SIGGEN_OUTPUT_GPIO,
        .duty           = 0,
        .hpoint         = 0,
    };
    err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return err;
    }

    /* ── Create periodic esp_timer ─────────────────────────────── */
    esp_timer_create_args_t timer_args = {
        .callback        = siggen_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "siggen",
    };
    err = esp_timer_create(&timer_args, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Set default frequency */
    err = signal_generator_set_frequency(1000.0f);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "signal_generator_set_frequency failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Signal generator ready");
    return ESP_OK;
}

esp_err_t signal_generator_start(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "Already running");
        return ESP_OK;
    }

    s_phase_acc = 0;
    s_running   = true;

    /* Period in microseconds = 1 000 000 / sample_rate */
    uint64_t period_us = 1000000ULL / SIGGEN_SAMPLE_RATE_HZ;
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, period_us));

    ESP_LOGI(TAG, "Started: wave=%s  freq=%.1f Hz  amp=%.2f  offset=%.2f",
             signal_generator_wave_name(s_wave), 
             (double)(s_phase_inc / (double)0x100000000LL * SIGGEN_SAMPLE_RATE_HZ),
             (double)s_amplitude, (double)s_offset);
    return ESP_OK;
}

esp_err_t signal_generator_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(esp_timer_stop(s_timer));
    s_running   = false;
    s_phase_acc = 0;

    /* Drive output to zero */
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    ESP_LOGI(TAG, "Stopped");
    return ESP_OK;
}

esp_err_t signal_generator_set_wave(wave_type_t wave)
{
    if (wave >= WAVE_MAX) {
        ESP_LOGE(TAG, "Invalid wave type: %d", wave);
        return ESP_ERR_INVALID_ARG;
    }
    s_wave = wave;
    ESP_LOGI(TAG, "Wave type: %s", signal_generator_wave_name(wave));
    return ESP_OK;
}

esp_err_t signal_generator_set_frequency(float freq_hz)
{
    if (freq_hz < SIGGEN_FREQ_MIN || freq_hz > SIGGEN_FREQ_MAX) {
        ESP_LOGE(TAG, "Frequency %.1f Hz out of range [%.0f, %.0f]",
                 (double)freq_hz, (double)SIGGEN_FREQ_MIN, (double)SIGGEN_FREQ_MAX);
        return ESP_ERR_INVALID_ARG;
    }

    /* phase_inc = (freq / sample_rate) * 2^32 */
    double inc_d = ((double)freq_hz / (double)SIGGEN_SAMPLE_RATE_HZ) * 4294967296.0;
    s_phase_inc  = (uint32_t)inc_d;

    ESP_LOGI(TAG, "Frequency: %.2f Hz  (phase_inc=0x%08" PRIx32 ")",
             (double)freq_hz, s_phase_inc);
    return ESP_OK;
}

esp_err_t signal_generator_set_amplitude(float amplitude)
{
    if (amplitude < 0.0f || amplitude > 1.0f) {
        ESP_LOGE(TAG, "Amplitude %.2f out of range [0.0, 1.0]", (double)amplitude);
        return ESP_ERR_INVALID_ARG;
    }
    s_amplitude = amplitude;
    ESP_LOGI(TAG, "Amplitude: %.2f (%.0f%%)", (double)amplitude, (double)(amplitude * 100.0f));
    return ESP_OK;
}

esp_err_t signal_generator_set_offset(float offset)
{
    if (offset < 0.0f || offset > 1.0f) {
        ESP_LOGE(TAG, "Offset %.2f out of range [0.0, 1.0]", (double)offset);
        return ESP_ERR_INVALID_ARG;
    }
    s_offset = offset;
    ESP_LOGI(TAG, "DC offset: %.2f", (double)offset);
    return ESP_OK;
}

esp_err_t signal_generator_get_config(siggen_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    cfg->wave_type  = s_wave;
    cfg->amplitude  = s_amplitude;
    cfg->offset     = s_offset;
    cfg->running    = s_running;
    /* Recover frequency from phase_inc */
    cfg->frequency  = (float)((double)s_phase_inc / 4294967296.0 * SIGGEN_SAMPLE_RATE_HZ);
    return ESP_OK;
}

const char *signal_generator_wave_name(wave_type_t wave)
{
    switch (wave) {
    case WAVE_SINE:     return "Sine";
    case WAVE_SQUARE:   return "Square";
    case WAVE_TRIANGLE: return "Triangle";
    case WAVE_SAWTOOTH: return "Sawtooth";
    default:            return "Unknown";
    }
}
