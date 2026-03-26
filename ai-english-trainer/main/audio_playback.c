/**
 * @file audio_playback.c
 * @brief I2S speaker audio playback for MAX98357A
 *
 * Plays 16-bit PCM audio (from OpenAI TTS) via I2S to MAX98357A amplifier.
 * Supports software volume control and stop-on-demand.
 *
 * MAX98357A Wiring:
 *   VIN  -> 3.3V (or 5V for louder output)
 *   GND  -> GND
 *   SD   -> leave floating (chip always on)
 *   BCLK -> CONFIG_I2S_SPK_BCLK_PIN
 *   LRC  -> CONFIG_I2S_SPK_LRC_PIN
 *   DIN  -> CONFIG_I2S_SPK_DIN_PIN
 */

#include "audio_playback.h"
#include "sdkconfig.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "audio_playback";

/* I2S channel handle */
static i2s_chan_handle_t s_tx_chan = NULL;

/* Playback state */
static volatile bool s_is_playing = false;
static volatile bool s_stop_request = false;

/* Volume: 0-100, default 80 */
static uint8_t s_volume = 80;

/* Write chunk size */
#define WRITE_CHUNK_SAMPLES  512                          /* samples per write */
#define WRITE_CHUNK_BYTES    (WRITE_CHUNK_SAMPLES * 2)   /* 16-bit = 2 bytes/sample */

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

esp_err_t audio_playback_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S speaker (BCLK=%d, LRC=%d, DIN=%d)",
             CONFIG_I2S_SPK_BCLK_PIN, CONFIG_I2S_SPK_LRC_PIN, CONFIG_I2S_SPK_DIN_PIN);

    /* Configure I2S channel (use I2S_NUM_1 to avoid conflict with mic on I2S_NUM_0) */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL));

    /* Configure standard I2S mode for MAX98357A */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_PLAYBACK_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT,
                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_I2S_SPK_BCLK_PIN,
            .ws   = CONFIG_I2S_SPK_LRC_PIN,
            .dout = CONFIG_I2S_SPK_DIN_PIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &std_cfg));
    ESP_LOGI(TAG, "I2S speaker initialized OK");
    return ESP_OK;
}

esp_err_t audio_playback_play_pcm(const uint8_t *pcm_data, size_t pcm_bytes)
{
    if (!s_tx_chan) {
        ESP_LOGE(TAG, "Speaker not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!pcm_data || pcm_bytes == 0) {
        ESP_LOGW(TAG, "No audio data to play");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Playing %u bytes PCM at %d Hz", (unsigned)pcm_bytes, AUDIO_PLAYBACK_SAMPLE_RATE);

    /* Allocate a small working buffer in internal SRAM for volume-scaled output */
    int16_t *work_buf = heap_caps_malloc(WRITE_CHUNK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!work_buf) {
        ESP_LOGE(TAG, "Failed to allocate playback work buffer");
        return ESP_ERR_NO_MEM;
    }

    s_is_playing    = true;
    s_stop_request  = false;

    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));

    const int16_t *src     = (const int16_t *)pcm_data;
    size_t         samples_total = pcm_bytes / 2;
    size_t         offset        = 0;

    while (offset < samples_total && !s_stop_request) {
        size_t chunk_samples = samples_total - offset;
        if (chunk_samples > WRITE_CHUNK_SAMPLES) {
            chunk_samples = WRITE_CHUNK_SAMPLES;
        }

        /* Apply software volume scaling */
        for (size_t i = 0; i < chunk_samples; i++) {
            int32_t scaled = ((int32_t)src[offset + i] * s_volume) / 100;
            /* Clamp to int16 range */
            if (scaled >  32767) scaled =  32767;
            if (scaled < -32768) scaled = -32768;
            work_buf[i] = (int16_t)scaled;
        }

        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(s_tx_chan,
                                           work_buf,
                                           chunk_samples * 2,
                                           &bytes_written,
                                           pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "I2S write error: %s", esp_err_to_name(ret));
            break;
        }

        offset += chunk_samples;
    }

    /* Flush remaining DMA data */
    vTaskDelay(pdMS_TO_TICKS(50));

    i2s_channel_disable(s_tx_chan);
    s_is_playing   = false;
    s_stop_request = false;

    heap_caps_free(work_buf);
    ESP_LOGI(TAG, "Playback complete");
    return ESP_OK;
}

esp_err_t audio_playback_stop(void)
{
    if (s_is_playing) {
        s_stop_request = true;
        /* Wait for playback to acknowledge stop */
        uint32_t timeout_ms = 500;
        while (s_is_playing && timeout_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            timeout_ms -= 10;
        }
    }
    return ESP_OK;
}

bool audio_playback_is_playing(void)
{
    return s_is_playing;
}

void audio_playback_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    s_volume = volume;
    ESP_LOGI(TAG, "Volume set to %d%%", volume);
}

esp_err_t audio_playback_deinit(void)
{
    audio_playback_stop();

    if (s_tx_chan) {
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
    }
    return ESP_OK;
}