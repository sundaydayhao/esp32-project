#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_TITLE_MAX_LEN   128
#define AUDIO_ARTIST_MAX_LEN  128
#define AUDIO_ALBUM_MAX_LEN   128
#define AUDIO_URL_MAX_LEN     512

/**
 * @brief Audio source type
 */
typedef enum {
    AUDIO_SOURCE_LOCAL = 0,   /*!< Local file from SPIFFS */
    AUDIO_SOURCE_HTTP,        /*!< HTTP/HTTPS stream or file */
} audio_source_t;

/**
 * @brief Audio player state
 */
typedef enum {
    AUDIO_STATE_IDLE = 0,
    AUDIO_STATE_PLAYING,
    AUDIO_STATE_PAUSED,
    AUDIO_STATE_STOPPED,
    AUDIO_STATE_ERROR,
} audio_state_t;

/**
 * @brief Track information
 */
typedef struct {
    char title[AUDIO_TITLE_MAX_LEN];
    char artist[AUDIO_ARTIST_MAX_LEN];
    char album[AUDIO_ALBUM_MAX_LEN];
    char url[AUDIO_URL_MAX_LEN];
    audio_source_t source;
    uint32_t duration_sec;   /*!< Duration in seconds (0 if unknown) */
    uint32_t sample_rate;
    uint8_t  channels;
    uint32_t bitrate;
} audio_track_t;

/**
 * @brief Player status (returned to web clients)
 */
typedef struct {
    audio_state_t state;
    audio_track_t current_track;
    uint32_t      position_sec;   /*!< Current playback position */
    uint8_t       volume;         /*!< 0-100 */
} audio_status_t;

/**
 * @brief Initialize the audio player (I2S, buffers, tasks)
 */
esp_err_t audio_player_init(void);

/**
 * @brief Play a track from URL (HTTP) or local path (SPIFFS)
 * @param url   HTTP URL or local path like "/spiffs/song.mp3"
 * @param title Optional track title (can be NULL)
 * @param artist Optional artist name (can be NULL)
 */
esp_err_t audio_player_play(const char *url, const char *title, const char *artist);

/**
 * @brief Pause playback
 */
esp_err_t audio_player_pause(void);

/**
 * @brief Resume paused playback
 */
esp_err_t audio_player_resume(void);

/**
 * @brief Stop playback
 */
esp_err_t audio_player_stop(void);

/**
 * @brief Set volume (0-100)
 */
esp_err_t audio_player_set_volume(uint8_t volume);

/**
 * @brief Get current player status
 */
void audio_player_get_status(audio_status_t *status);

/**
 * @brief Get current audio state
 */
audio_state_t audio_player_get_state(void);

#ifdef __cplusplus
}
#endif