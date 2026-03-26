#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OpenAI API Client
 *
 * Provides three API calls:
 *  1. Whisper STT  - converts WAV audio to English transcript
 *  2. GPT Chat     - AI foreign teacher responds with correction + score
 *  3. TTS          - converts teacher response text to PCM audio
 *
 * All network calls use HTTPS (mbedTLS).
 * API key is configured via menuconfig (CONFIG_OPENAI_API_KEY).
 */

/* Maximum transcript length from Whisper */
#define OPENAI_TRANSCRIPT_MAX_LEN   512

/* Maximum GPT response JSON length */
#define OPENAI_GPT_RESPONSE_MAX_LEN 1024

/* Maximum TTS audio size (10s @ 24kHz 16-bit mono = 480KB) */
#define OPENAI_TTS_AUDIO_MAX_BYTES  (24000 * 2 * 12)   /* 12 seconds */

/**
 * @brief Structured response from GPT teacher
 */
typedef struct {
    char response[256];       /**< Teacher's conversational reply */
    char correction[256];     /**< Pronunciation/grammar correction (empty if none) */
    char encouragement[128];  /**< Encouraging message */
    int  score;               /**< Pronunciation score 1-10 */
} openai_teacher_response_t;

/**
 * @brief Initialize OpenAI client
 *        Sets up HTTP client configuration and TLS.
 * @return ESP_OK on success
 */
esp_err_t openai_client_init(void);

/**
 * @brief Transcribe audio using OpenAI Whisper API
 *
 * Sends WAV audio to Whisper and returns English transcript.
 *
 * @param wav_data      Pointer to WAV file data (header + PCM)
 * @param wav_size      Total WAV file size in bytes
 * @param transcript    Output buffer for transcript text
 * @param transcript_len Size of transcript buffer
 * @return ESP_OK on success, ESP_FAIL on API error
 */
esp_err_t openai_whisper_transcribe(const uint8_t *wav_data, size_t wav_size,
                                     char *transcript, size_t transcript_len);

/**
 * @brief Send transcript to GPT teacher and get structured response
 *
 * GPT acts as an encouraging English foreign teacher for children.
 * Returns pronunciation correction, score, and encouragement.
 *
 * @param transcript    Child's speech transcript from Whisper
 * @param response      Output: structured teacher response
 * @return ESP_OK on success
 */
esp_err_t openai_gpt_teacher_chat(const char *transcript,
                                   openai_teacher_response_t *response);

/**
 * @brief Convert text to speech using OpenAI TTS API
 *
 * Returns raw PCM audio (24kHz, 16-bit, mono).
 * Caller must free the returned buffer with heap_caps_free().
 *
 * @param text          Text to synthesize
 * @param audio_out     Output: pointer to allocated PCM buffer (in PSRAM)
 * @param audio_len_out Output: size of PCM buffer in bytes
 * @return ESP_OK on success
 */
esp_err_t openai_tts_synthesize(const char *text,
                                 uint8_t **audio_out,
                                 size_t *audio_len_out);

/**
 * @brief Deinitialize OpenAI client
 */
void openai_client_deinit(void);

#ifdef __cplusplus
}
#endif