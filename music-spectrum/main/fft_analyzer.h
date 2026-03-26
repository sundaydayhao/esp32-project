#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FFT_SIZE        512          /* Must be power of 2 */
#define FFT_BANDS       64           /* Number of spectrum bands sent to web */

/**
 * @brief FFT spectrum data (one frame)
 */
typedef struct {
    float bands[FFT_BANDS];   /*!< Magnitude for each frequency band (0.0 - 1.0) */
    uint32_t timestamp_ms;    /*!< FreeRTOS tick time in ms */
} fft_spectrum_t;

/**
 * @brief Callback type for spectrum data ready
 */
typedef void (*fft_spectrum_cb_t)(const fft_spectrum_t *spectrum, void *user_data);

/**
 * @brief Initialize the FFT analyzer
 */
esp_err_t fft_analyzer_init(void);

/**
 * @brief Feed PCM samples to the FFT analyzer
 *        Stereo interleaved: L, R, L, R, ...
 *        Internally uses only the left channel for analysis.
 * @param samples  Pointer to int16_t PCM samples
 * @param count    Total number of samples (channels * frames)
 */
void fft_analyzer_feed(const int16_t *samples, int count);

/**
 * @brief Register a callback to receive spectrum data
 */
void fft_analyzer_set_callback(fft_spectrum_cb_t cb, void *user_data);

/**
 * @brief Get the latest computed spectrum (non-blocking)
 * @return true if new data was available
 */
bool fft_analyzer_get_spectrum(fft_spectrum_t *out);

#ifdef __cplusplus
}
#endif