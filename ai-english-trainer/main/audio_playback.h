#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio playback configuration
 *
 * Hardware: MAX98357A I2S Class-D Amplifier
 * - BCLK -> CONFIG_I2S_SPK_BCLK_PIN  (default GPIO17)
 * - LRC  -> CONFIG_I2S_SPK_LRC_PIN   (default GPIO18)
 * - DIN  -> CONFIG_I2S_SPK_DIN_PIN   (default GPIO8)
 * - VIN  -> 3.3V or 5V
 * - GND  -> GND
 * - SD   -> leave floating (always on) or connect to GPIO for mute control
 *
 * OpenAI TTS PCM format: 24000 Hz, 16-bit, mono, little-endian
 */

#define AUDIO_PLAYBACK_SAMPLE_RATE   24000   /* OpenAI TTS PCM output rate */
#define AUDIO_PLAYBACK_BITS          16
#define AUDIO_PLAYBACK_CHANNELS      1

/**
 * @brief Initialize I2S speaker driver
 * @return ESP_OK on success
 */
esp_err_t audio_playback_init(void);

/**
 * @brief Play raw PCM audio data (blocking until complete)
 *
 * @param pcm_data  Pointer to 16-bit signed PCM samples
 * @param pcm_bytes Total number of bytes (not samples)
 * @return ESP_OK on success
 */
esp_err_t audio_playback_play_pcm(const uint8_t *pcm_data, size_t pcm_bytes);

/**
 * @brief Stop playback immediately
 * @return ESP_OK on success
 */
esp_err_t audio_playback_stop(void);

/**
 * @brief Check if playback is currently active
 * @return true if playing
 */
bool audio_playback_is_playing(void);

/**
 * @brief Set speaker volume (0-100)
 *        Implemented as software gain scaling on PCM data.
 * @param volume 0 = mute, 100 = full volume
 */
void audio_playback_set_volume(uint8_t volume);

/**
 * @brief Deinitialize I2S speaker driver
 * @return ESP_OK on success
 */
esp_err_t audio_playback_deinit(void);

#ifdef __cplusplus
}
#endif