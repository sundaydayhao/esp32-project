/**
 * @file audio_player.h
 * @brief Audio player interface for SD card music playback
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_MAX_FILENAME_LEN 128
#define AUDIO_MAX_PLAYLIST_SIZE 100

typedef enum {
    AUDIO_STATE_STOPPED = 0,
    AUDIO_STATE_PLAYING,
    AUDIO_STATE_PAUSED,
} audio_state_t;

typedef struct {
    char filename[AUDIO_MAX_FILENAME_LEN];
    char title[64];
    uint32_t duration_sec;
    uint32_t position_sec;
} audio_track_info_t;

typedef struct {
    audio_state_t state;
    uint8_t volume;
    int current_track;
    int total_tracks;
    audio_track_info_t current_info;
} audio_player_status_t;

/**
 * @brief Initialize audio player and SD card
 * @return ESP_OK on success
 */
esp_err_t audio_player_init(void);

/**
 * @brief Deinitialize audio player
 */
void audio_player_deinit(void);

/**
 * @brief Scan SD card for audio files
 * @return ESP_OK on success
 */
esp_err_t audio_player_scan_files(void);

/**
 * @brief Start playing current track
 * @return ESP_OK on success
 */
esp_err_t audio_player_play(void);

/**
 * @brief Pause playback
 * @return ESP_OK on success
 */
esp_err_t audio_player_pause(void);

/**
 * @brief Stop playback
 * @return ESP_OK on success
 */
esp_err_t audio_player_stop(void);

/**
 * @brief Play next track
 * @return ESP_OK on success
 */
esp_err_t audio_player_next(void);

/**
 * @brief Play previous track
 * @return ESP_OK on success
 */
esp_err_t audio_player_prev(void);

/**
 * @brief Set volume (0-100)
 * @param volume Volume level
 * @return ESP_OK on success
 */
esp_err_t audio_player_set_volume(uint8_t volume);

/**
 * @brief Get current player status
 * @param status Pointer to status structure
 * @return ESP_OK on success
 */
esp_err_t audio_player_get_status(audio_player_status_t *status);

/**
 * @brief Check if SD card is mounted
 * @return true if mounted
 */
bool audio_player_is_sd_mounted(void);

#ifdef __cplusplus
}
#endif