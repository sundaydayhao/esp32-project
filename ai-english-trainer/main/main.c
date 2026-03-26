/**
 * @file main.c
 * @brief AI English Trainer - Main Application
 *
 * ESP32-S3 based AI English conversation trainer for children.
 * Acts as a foreign English teacher with real-time:
 *   - Speech recognition (OpenAI Whisper)
 *   - Pronunciation scoring & correction (GPT-4)
 *   - Encouraging voice feedback (OpenAI TTS)
 *
 * ============================================================
 * HARDWARE CONNECTIONS (ESP32-S3-DevKitC-1)
 * ============================================================
 *
 * INMP441 Microphone:
 *   VDD  -> 3.3V
 *   GND  -> GND
 *   L/R  -> GND
 *   SCK  -> GPIO14
 *   WS   -> GPIO15
 *   SD   -> GPIO16
 *
 * MAX98357A Speaker Amplifier:
 *   VIN  -> 5V (or 3.3V)
 *   GND  -> GND
 *   BCLK -> GPIO17
 *   LRC  -> GPIO18
 *   DIN  -> GPIO8
 *
 * Push-to-Talk Button:
 *   One side -> GPIO0 (BOOT button)
 *   Other side -> GND
 *
 * WS2812B RGB LED:
 *   DIN  -> GPIO48 (built-in on DevKitC-1)
 *
 * ============================================================
 * USAGE
 * ============================================================
 *
 * 1. Configure via: idf.py menuconfig -> "AI English Trainer Configuration"
 *    - Set WiFi SSID and password
 *    - Set OpenAI API key
 *    - Adjust GPIO pins if needed
 *
 * 2. Build and flash: idf.py build flash monitor
 *
 * 3. Operation:
 *    - Power on -> LED breathes green (ready)
 *    - Press and HOLD button -> LED blinks red (recording)
 *    - Release button -> LED pulses yellow (AI processing)
 *    - Teacher responds -> LED pulses blue (playing audio)
 *    - Repeat!
 *
 * ============================================================
 * STATE MACHINE
 * ============================================================
 *
 *   IDLE ──[button press]──> RECORDING
 *   RECORDING ──[button release / silence / timeout]──> PROCESSING
 *   PROCESSING ──[AI done]──> PLAYING
 *   PLAYING ──[audio done]──> IDLE
 *   Any state ──[error]──> ERROR ──[3s]──> IDLE
 *
 * ============================================================
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

#include "audio_capture.h"
#include "audio_playback.h"
#include "wifi_manager.h"
#include "openai_client.h"
#include "button_handler.h"
#include "led_indicator.h"

static const char *TAG = "main";

/* ------------------------------------------------------------------ */
/*  Application State Machine                                           */
/* ------------------------------------------------------------------ */

typedef enum {
    APP_STATE_INIT,         /* Initializing hardware and WiFi */
    APP_STATE_IDLE,         /* Ready, waiting for button press */
    APP_STATE_RECORDING,    /* Recording child's speech */
    APP_STATE_PROCESSING,   /* Sending to AI, waiting for response */
    APP_STATE_PLAYING,      /* Playing teacher's audio response */
    APP_STATE_ERROR,        /* Error occurred */
} app_state_t;

static volatile app_state_t s_app_state = APP_STATE_INIT;

/* Event group for state transitions */
static EventGroupHandle_t s_app_events = NULL;
#define EVT_BTN_PRESSED   BIT0
#define EVT_BTN_RELEASED  BIT1
#define EVT_BTN_LONG      BIT2
#define EVT_REC_DONE      BIT3
#define EVT_ERROR         BIT4

/* ------------------------------------------------------------------ */
/*  Button Callback                                                     */
/* ------------------------------------------------------------------ */

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_PRESSED:
            xEventGroupSetBits(s_app_events, EVT_BTN_PRESSED);
            break;
        case BUTTON_EVENT_RELEASED:
            xEventGroupSetBits(s_app_events, EVT_BTN_RELEASED);
            break;
        case BUTTON_EVENT_LONG_PRESS:
            xEventGroupSetBits(s_app_events, EVT_BTN_LONG);
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Build TTS text from teacher response                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Combine teacher response fields into a single TTS string
 *
 * Format: "[response] [correction if any] [encouragement]"
 * Score is announced verbally.
 */
static void build_tts_text(const openai_teacher_response_t *teacher,
                            char *out_buf, size_t buf_size)
{
    char score_text[64];

    /* Announce score */
    if (teacher->score >= 9) {
        snprintf(score_text, sizeof(score_text),
                 "Your pronunciation score is %d out of 10! Perfect!", teacher->score);
    } else if (teacher->score >= 7) {
        snprintf(score_text, sizeof(score_text),
                 "Your pronunciation score is %d out of 10! Very good!", teacher->score);
    } else if (teacher->score >= 5) {
        snprintf(score_text, sizeof(score_text),
                 "Your pronunciation score is %d out of 10! Good try!", teacher->score);
    } else {
        snprintf(score_text, sizeof(score_text),
                 "Your pronunciation score is %d out of 10! Keep practicing!", teacher->score);
    }

    /* Build full response */
    if (strlen(teacher->correction) > 0) {
        snprintf(out_buf, buf_size,
                 "%s %s %s %s",
                 teacher->response,
                 score_text,
                 teacher->correction,
                 teacher->encouragement);
    } else {
        snprintf(out_buf, buf_size,
                 "%s %s %s",
                 teacher->response,
                 score_text,
                 teacher->encouragement);
    }
}

/* ------------------------------------------------------------------ */
/*  Print teacher response to console                                   */
/* ------------------------------------------------------------------ */

static void print_teacher_response(const char *transcript,
                                    const openai_teacher_response_t *teacher)
{
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║         AI ENGLISH TEACHER RESPONSE      ║");
    ESP_LOGI(TAG, "╠══════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ Child said:    \"%s\"", transcript);
    ESP_LOGI(TAG, "║ Score:         %d/10", teacher->score);
    ESP_LOGI(TAG, "║ Response:      %s", teacher->response);
    if (strlen(teacher->correction) > 0) {
        ESP_LOGI(TAG, "║ Correction:    %s", teacher->correction);
    }
    ESP_LOGI(TAG, "║ Encouragement: %s", teacher->encouragement);
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
}

/* ------------------------------------------------------------------ */
/*  Main Application Task                                               */
/* ------------------------------------------------------------------ */

static void app_main_task(void *arg)
{
    esp_err_t err;

    /* Buffers */
    char transcript[OPENAI_TRANSCRIPT_MAX_LEN] = {0};
    char tts_text[512] = {0};
    openai_teacher_response_t teacher_response = {0};
    uint8_t *tts_audio = NULL;
    size_t   tts_audio_len = 0;

    ESP_LOGI(TAG, "AI English Trainer starting...");
    ESP_LOGI(TAG, "Free heap: %u bytes, PSRAM: %u bytes",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* ---- Step 1: Initialize LED ---- */
    ESP_LOGI(TAG, "[1/6] Initializing LED...");
    led_indicator_init();
    led_indicator_set_state(LED_STATE_WIFI_CONNECT);

    /* ---- Step 2: Connect WiFi ---- */
    ESP_LOGI(TAG, "[2/6] Connecting to WiFi '%s'...", CONFIG_WIFI_SSID);
    err = wifi_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connection failed! Check SSID/password in menuconfig.");
        led_indicator_set_state(LED_STATE_ERROR);
        /* Keep retrying every 10 seconds */
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            err = wifi_manager_init();
            if (err == ESP_OK) break;
        }
    }
    ESP_LOGI(TAG, "WiFi connected!");

    /* ---- Step 3: Initialize OpenAI client ---- */
    ESP_LOGI(TAG, "[3/6] Initializing OpenAI client...");
    openai_client_init();

    /* ---- Step 4: Initialize audio capture (microphone) ---- */
    ESP_LOGI(TAG, "[4/6] Initializing microphone...");
    err = audio_capture_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Microphone init failed: %s", esp_err_to_name(err));
        led_indicator_set_state(LED_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }

    /* ---- Step 5: Initialize audio playback (speaker) ---- */
    ESP_LOGI(TAG, "[5/6] Initializing speaker...");
    err = audio_playback_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Speaker init failed: %s", esp_err_to_name(err));
        led_indicator_set_state(LED_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }
    audio_playback_set_volume(85);

    /* ---- Step 6: Initialize button ---- */
    ESP_LOGI(TAG, "[6/6] Initializing button...");
    err = button_handler_init(on_button_event);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Button init failed: %s", esp_err_to_name(err));
        led_indicator_set_state(LED_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }

    /* ---- Ready! ---- */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   AI ENGLISH TRAINER READY!              ║");
    ESP_LOGI(TAG, "║                                          ║");
    ESP_LOGI(TAG, "║  Press and HOLD button to speak          ║");
    ESP_LOGI(TAG, "║  Release button when done speaking       ║");
    ESP_LOGI(TAG, "║  Long press (3s) to adjust volume        ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    s_app_state = APP_STATE_IDLE;
    led_indicator_set_state(LED_STATE_IDLE);

    /* ================================================================
     * Main State Machine Loop
     * ================================================================ */
    while (1) {
        switch (s_app_state) {

            /* --------------------------------------------------------
             * IDLE: Wait for button press
             * -------------------------------------------------------- */
            case APP_STATE_IDLE: {
                /* Wait for button press event */
                EventBits_t bits = xEventGroupWaitBits(
                    s_app_events,
                    EVT_BTN_PRESSED | EVT_BTN_LONG,
                    pdTRUE,   /* clear bits */
                    pdFALSE,  /* any bit */
                    portMAX_DELAY);

                if (bits & EVT_BTN_LONG) {
                    /* Long press in idle: cycle volume */
                    static uint8_t vol_levels[] = {50, 70, 85, 100};
                    static int vol_idx = 2;
                    vol_idx = (vol_idx + 1) % 4;
                    audio_playback_set_volume(vol_levels[vol_idx]);
                    ESP_LOGI(TAG, "Volume: %d%%", vol_levels[vol_idx]);
                    /* Flash LED to indicate volume change */
                    led_indicator_set_state(LED_STATE_SUCCESS);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    led_indicator_set_state(LED_STATE_IDLE);
                    break;
                }

                if (bits & EVT_BTN_PRESSED) {
                    ESP_LOGI(TAG, "--- Starting recording ---");
                    s_app_state = APP_STATE_RECORDING;
                }
                break;
            }

            /* --------------------------------------------------------
             * RECORDING: Capture audio while button held
             * -------------------------------------------------------- */
            case APP_STATE_RECORDING: {
                led_indicator_set_state(LED_STATE_RECORDING);

                /* Start audio capture */
                err = audio_capture_start();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start recording");
                    s_app_state = APP_STATE_ERROR;
                    break;
                }

                ESP_LOGI(TAG, "Recording... (release button to stop)");

                /* Wait for button release OR auto-stop (silence detection) */
                while (1) {
                    EventBits_t bits = xEventGroupWaitBits(
                        s_app_events,
                        EVT_BTN_RELEASED | EVT_BTN_LONG,
                        pdTRUE,
                        pdFALSE,
                        pdMS_TO_TICKS(100));

                    if (bits & (EVT_BTN_RELEASED | EVT_BTN_LONG)) {
                        ESP_LOGI(TAG, "Button released - stopping recording");
                        break;
                    }

                    /* Check if auto-stopped by silence detection */
                    if (!audio_capture_is_recording()) {
                        ESP_LOGI(TAG, "Auto-stopped by silence detection");
                        break;
                    }
                }

                /* Stop recording */
                audio_capture_stop();

                /* Check if we got any audio */
                uint8_t *wav_buf = audio_capture_get_wav_buffer();
                size_t   wav_size = audio_capture_get_wav_size();

                if (!wav_buf || wav_size <= WAV_HEADER_SIZE + 1000) {
                    ESP_LOGW(TAG, "Recording too short, ignoring");
                    s_app_state = APP_STATE_IDLE;
                    led_indicator_set_state(LED_STATE_IDLE);
                    break;
                }

                ESP_LOGI(TAG, "Recorded %u bytes of audio", (unsigned)wav_size);
                s_app_state = APP_STATE_PROCESSING;
                break;
            }

            /* --------------------------------------------------------
             * PROCESSING: Send to AI, get response
             * -------------------------------------------------------- */
            case APP_STATE_PROCESSING: {
                led_indicator_set_state(LED_STATE_PROCESSING);

                uint8_t *wav_buf  = audio_capture_get_wav_buffer();
                size_t   wav_size = audio_capture_get_wav_size();

                /* Step A: Whisper STT */
                ESP_LOGI(TAG, "Step 1/3: Transcribing speech with Whisper...");
                memset(transcript, 0, sizeof(transcript));
                err = openai_whisper_transcribe(wav_buf, wav_size,
                                                transcript, sizeof(transcript));
                if (err != ESP_OK || strlen(transcript) == 0) {
                    ESP_LOGW(TAG, "Transcription failed or empty - try again");
                    s_app_state = APP_STATE_IDLE;
                    led_indicator_set_state(LED_STATE_IDLE);
                    break;
                }

                /* Step B: GPT Teacher Chat */
                ESP_LOGI(TAG, "Step 2/3: Getting teacher response from GPT...");
                memset(&teacher_response, 0, sizeof(teacher_response));
                err = openai_gpt_teacher_chat(transcript, &teacher_response);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "GPT teacher chat failed");
                    s_app_state = APP_STATE_ERROR;
                    break;
                }

                /* Print response to console */
                print_teacher_response(transcript, &teacher_response);

                /* Step C: TTS Synthesis */
                ESP_LOGI(TAG, "Step 3/3: Synthesizing teacher voice with TTS...");
                build_tts_text(&teacher_response, tts_text, sizeof(tts_text));
                ESP_LOGI(TAG, "TTS text: \"%s\"", tts_text);

                /* Free previous TTS audio if any */
                if (tts_audio) {
                    heap_caps_free(tts_audio);
                    tts_audio = NULL;
                    tts_audio_len = 0;
                }

                err = openai_tts_synthesize(tts_text, &tts_audio, &tts_audio_len);
                if (err != ESP_OK || !tts_audio) {
                    ESP_LOGE(TAG, "TTS synthesis failed");
                    s_app_state = APP_STATE_ERROR;
                    break;
                }

                s_app_state = APP_STATE_PLAYING;
                break;
            }

            /* --------------------------------------------------------
             * PLAYING: Play teacher's audio response
             * -------------------------------------------------------- */
            case APP_STATE_PLAYING: {
                led_indicator_set_state(LED_STATE_PLAYING);

                ESP_LOGI(TAG, "Playing teacher response (%u bytes PCM)...",
                         (unsigned)tts_audio_len);

                err = audio_playback_play_pcm(tts_audio, tts_audio_len);

                /* Free TTS audio buffer */
                if (tts_audio) {
                    heap_caps_free(tts_audio);
                    tts_audio = NULL;
                    tts_audio_len = 0;
                }

                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Playback error: %s", esp_err_to_name(err));
                }

                /* Brief success flash */
                led_indicator_set_state(LED_STATE_SUCCESS);
                vTaskDelay(pdMS_TO_TICKS(1500));

                /* Back to idle */
                s_app_state = APP_STATE_IDLE;
                led_indicator_set_state(LED_STATE_IDLE);

                ESP_LOGI(TAG, "--- Ready for next input ---");
                ESP_LOGI(TAG, "Free heap: %u bytes",
                         (unsigned)esp_get_free_heap_size());
                break;
            }

            /* --------------------------------------------------------
             * ERROR: Show error, recover after delay
             * -------------------------------------------------------- */
            case APP_STATE_ERROR: {
                ESP_LOGE(TAG, "Error state - recovering in 3 seconds...");
                led_indicator_set_state(LED_STATE_ERROR);

                /* Free any allocated audio */
                if (tts_audio) {
                    heap_caps_free(tts_audio);
                    tts_audio = NULL;
                    tts_audio_len = 0;
                }

                vTaskDelay(pdMS_TO_TICKS(3000));

                /* Check WiFi and reconnect if needed */
                if (!wifi_manager_is_connected()) {
                    ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
                    led_indicator_set_state(LED_STATE_WIFI_CONNECT);
                    wifi_manager_init();
                }

                s_app_state = APP_STATE_IDLE;
                led_indicator_set_state(LED_STATE_IDLE);
                break;
            }

            default:
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  app_main                                                            */
/* ------------------------------------------------------------------ */

void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║       AI ENGLISH TRAINER v1.0            ║");
    ESP_LOGI(TAG, "║   ESP32-S3 + OpenAI Whisper/GPT/TTS      ║");
    ESP_LOGI(TAG, "║   For children aged 5-10                 ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    /* Create event group */
    s_app_events = xEventGroupCreate();
    if (!s_app_events) {
        ESP_LOGE(TAG, "Failed to create event group");
        esp_restart();
    }

    /* Create main application task with large stack (for HTTP + JSON) */
    BaseType_t ret = xTaskCreatePinnedToCore(
        app_main_task,
        "app_main",
        16384,      /* 16KB stack - needed for HTTP + JSON + TLS */
        NULL,
        5,
        NULL,
        0           /* Run on core 0 */
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create main task");
        esp_restart();
    }
}