#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio capture configuration
 *
 * Hardware: INMP441 I2S MEMS Microphone
 * - SCK  -> CONFIG_I2S_MIC_SCK_PIN  (default GPIO14)
 * - WS   -> CONFIG_I2S_MIC_WS_PIN   (default GPIO15)
 * - SD   -> CONFIG_I2S_MIC_SD_PIN   (default GPIO16)
 * - L/R  -> GND (left channel)
 * - VDD  -> 3.3V
 * - GND  -> GND
 */

#define AUDIO_CAPTURE_SAMPLE_RATE    CONFIG_AUDIO_SAMPLE_RATE_MIC   /* 16000 Hz */
#define AUDIO_CAPTURE_BITS           16                              /* 16-bit PCM */
#define AUDIO_CAPTURE_CHANNELS       1                              /* Mono */
#define AUDIO_CAPTURE_MAX_SECONDS    CONFIG_AUDIO_MAX_RECORD_SECONDS /* 10s */

/* Buffer size: 16000 samples/s * 2 bytes/sample * max_seconds */
#define AUDIO_CAPTURE_BUFFER_BYTES   (AUDIO_CAPTURE_SAMPLE_RATE * \
                                      (AUDIO_CAPTURE_BITS / 8) * \
                                      AUDIO_CAPTURE_MAX_SECONDS)

/* WAV header size */
#define WAV_HEADER_SIZE              44

/* Total WAV buffer size (header + PCM data) */
#define AUDIO_WAV_BUFFER_BYTES       (WAV_HEADER_SIZE + AUDIO_CAPTURE_BUFFER_BYTES)

/**
 * @brief Initialize I2S microphone driver
 * @return ESP_OK on success
 */
esp_err_t audio_capture_init(void);

/**
 * @brief Start recording audio
 *        Clears previous buffer and begins capturing from microphone.
 * @return ESP_OK on success
 */
esp_err_t audio_capture_start(void);

/**
 * @brief Stop recording audio
 * @return ESP_OK on success
 */
esp_err_t audio_capture_stop(void);

/**
 * @brief Get pointer to WAV-formatted audio buffer
 *        Buffer contains 44-byte WAV header followed by PCM data.
 *        Valid after audio_capture_stop() is called.
 * @return Pointer to WAV buffer (in PSRAM), or NULL if not ready
 */
uint8_t *audio_capture_get_wav_buffer(void);

/**
 * @brief Get total size of WAV buffer (header + PCM data)
 * @return Size in bytes
 */
size_t audio_capture_get_wav_size(void);

/**
 * @brief Check if currently recording
 * @return true if recording is in progress
 */
bool audio_capture_is_recording(void);

/**
 * @brief Deinitialize I2S microphone driver and free resources
 * @return ESP_OK on success
 */
esp_err_t audio_capture_deinit(void);

#ifdef __cplusplus
}
#endif