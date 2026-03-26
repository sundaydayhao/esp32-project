/**
 * @file audio_capture.c
 * @brief I2S microphone audio capture for INMP441
 *
 * Uses ESP-IDF v5.x I2S driver (i2s_std) to capture audio from
 * INMP441 MEMS microphone. Audio is stored in PSRAM as WAV format.
 *
 * INMP441 Wiring:
 *   VDD  -> 3.3V
 *   GND  -> GND
 *   L/R  -> GND  (selects left channel, data on WS low)
 *   SCK  -> GPIO_I2S_MIC_SCK
 *   WS   -> GPIO_I2S_MIC_WS
 *   SD   -> GPIO_I2S_MIC_SD
 */

#include "audio_capture.h"
#include "sdkconfig.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

static const char *TAG = "audio_capture";

/* I2S channel handle */
static i2s_chan_handle_t s_rx_chan = NULL;

/* Audio buffer in PSRAM */
static uint8_t *s_wav_buffer = NULL;
static size_t   s_pcm_bytes_written = 0;
static bool     s_is_recording = false;

/* Recording task handle */
static TaskHandle_t s_record_task_handle = NULL;

/* ------------------------------------------------------------------ */
/*  WAV Header Construction                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief Write a 44-byte PCM WAV header into the buffer
 */
static void write_wav_header(uint8_t *buf, uint32_t pcm_data_bytes,
                              uint32_t sample_rate, uint16_t channels,
                              uint16_t bits_per_sample)
{
    uint32_t byte_rate    = sample_rate * channels * (bits_per_sample / 8);
    uint16_t block_align  = channels * (bits_per_sample / 8);
    uint32_t chunk_size   = 36 + pcm_data_bytes;

    /* RIFF chunk */
    memcpy(buf + 0,  "RIFF", 4);
    buf[4]  = (chunk_size >>  0) & 0xFF;
    buf[5]  = (chunk_size >>  8) & 0xFF;
    buf[6]  = (chunk_size >> 16) & 0xFF;
    buf[7]  = (chunk_size >> 24) & 0xFF;
    memcpy(buf + 8,  "WAVE", 4);

    /* fmt sub-chunk */
    memcpy(buf + 12, "fmt ", 4);
    buf[16] = 16; buf[17] = 0; buf[18] = 0; buf[19] = 0; /* sub-chunk size = 16 */
    buf[20] = 1;  buf[21] = 0;                             /* PCM format = 1 */
    buf[22] = (channels >>  0) & 0xFF;
    buf[23] = (channels >>  8) & 0xFF;
    buf[24] = (sample_rate >>  0) & 0xFF;
    buf[25] = (sample_rate >>  8) & 0xFF;
    buf[26] = (sample_rate >> 16) & 0xFF;
    buf[27] = (sample_rate >> 24) & 0xFF;
    buf[28] = (byte_rate >>  0) & 0xFF;
    buf[29] = (byte_rate >>  8) & 0xFF;
    buf[30] = (byte_rate >> 16) & 0xFF;
    buf[31] = (byte_rate >> 24) & 0xFF;
    buf[32] = (block_align >> 0) & 0xFF;
    buf[33] = (block_align >> 8) & 0xFF;
    buf[34] = (bits_per_sample >> 0) & 0xFF;
    buf[35] = (bits_per_sample >> 8) & 0xFF;

    /* data sub-chunk */
    memcpy(buf + 36, "data", 4);
    buf[40] = (pcm_data_bytes >>  0) & 0xFF;
    buf[41] = (pcm_data_bytes >>  8) & 0xFF;
    buf[42] = (pcm_data_bytes >> 16) & 0xFF;
    buf[43] = (pcm_data_bytes >> 24) & 0xFF;
}

/* ------------------------------------------------------------------ */
/*  Silence Detection                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Calculate RMS amplitude of a 16-bit PCM buffer
 */
static uint32_t calc_rms(const int16_t *samples, size_t count)
{
    if (count == 0) return 0;
    uint64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t s = samples[i];
        sum += (uint64_t)(s * s);
    }
    return (uint32_t)(sum / count);
}

/* ------------------------------------------------------------------ */
/*  Recording Task                                                       */
/* ------------------------------------------------------------------ */

/**
 * @brief FreeRTOS task that reads I2S data into PSRAM buffer.
 *        Auto-stops on silence or buffer full.
 */
static void recording_task(void *arg)
{
    /* Temporary DMA read buffer (internal SRAM) */
    const size_t READ_CHUNK = 1024; /* bytes per I2S read */
    uint8_t *chunk_buf = heap_caps_malloc(READ_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!chunk_buf) {
        ESP_LOGE(TAG, "Failed to allocate DMA chunk buffer");
        s_is_recording = false;
        vTaskDelete(NULL);
        return;
    }

    uint8_t *pcm_ptr = s_wav_buffer + WAV_HEADER_SIZE;
    size_t   max_pcm = AUDIO_CAPTURE_BUFFER_BYTES;
    size_t   bytes_read = 0;

    /* Silence detection state */
    uint32_t silence_start_ms = 0;
    bool     speech_detected  = false;
    const uint32_t SILENCE_THRESH_SQ = (uint32_t)CONFIG_AUDIO_SILENCE_THRESHOLD *
                                        CONFIG_AUDIO_SILENCE_THRESHOLD;

    ESP_LOGI(TAG, "Recording started (max %d s)", AUDIO_CAPTURE_MAX_SECONDS);

    while (s_is_recording && s_pcm_bytes_written < max_pcm) {
        size_t actual = 0;
        esp_err_t ret = i2s_channel_read(s_rx_chan, chunk_buf, READ_CHUNK,
                                          &actual, pdMS_TO_TICKS(100));
        if (ret != ESP_OK || actual == 0) {
            continue;
        }

        /* Copy to PSRAM */
        size_t space = max_pcm - s_pcm_bytes_written;
        size_t to_copy = (actual < space) ? actual : space;
        memcpy(pcm_ptr + s_pcm_bytes_written, chunk_buf, to_copy);
        s_pcm_bytes_written += to_copy;

        /* Silence detection */
        uint32_t rms_sq = calc_rms((const int16_t *)chunk_buf, actual / 2);
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        if (rms_sq > SILENCE_THRESH_SQ) {
            speech_detected  = true;
            silence_start_ms = now_ms;
        } else if (speech_detected) {
            /* Check if silence has lasted long enough to auto-stop */
            if ((now_ms - silence_start_ms) >= CONFIG_AUDIO_SILENCE_DURATION_MS) {
                ESP_LOGI(TAG, "Auto-stop: silence detected after speech");
                break;
            }
        }
    }

    free(chunk_buf);

    /* Write WAV header with actual PCM size */
    write_wav_header(s_wav_buffer,
                     (uint32_t)s_pcm_bytes_written,
                     AUDIO_CAPTURE_SAMPLE_RATE,
                     AUDIO_CAPTURE_CHANNELS,
                     AUDIO_CAPTURE_BITS);

    s_is_recording = false;
    ESP_LOGI(TAG, "Recording stopped: %u bytes PCM captured", (unsigned)s_pcm_bytes_written);

    s_record_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

esp_err_t audio_capture_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S microphone (SCK=%d, WS=%d, SD=%d)",
             CONFIG_I2S_MIC_SCK_PIN, CONFIG_I2S_MIC_WS_PIN, CONFIG_I2S_MIC_SD_PIN);

    /* Allocate WAV buffer in PSRAM */
    s_wav_buffer = heap_caps_malloc(AUDIO_WAV_BUFFER_BYTES,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_wav_buffer) {
        ESP_LOGE(TAG, "Failed to allocate WAV buffer (%u bytes) in PSRAM",
                 (unsigned)AUDIO_WAV_BUFFER_BYTES);
        return ESP_ERR_NO_MEM;
    }
    memset(s_wav_buffer, 0, AUDIO_WAV_BUFFER_BYTES);

    /* Configure I2S channel */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx_chan));

    /* Configure standard I2S mode for INMP441 */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_CAPTURE_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT,   /* INMP441 outputs 24-bit in 32-bit slot */
                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_I2S_MIC_SCK_PIN,
            .ws   = CONFIG_I2S_MIC_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = CONFIG_I2S_MIC_SD_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_chan, &std_cfg));
    ESP_LOGI(TAG, "I2S microphone initialized OK");
    return ESP_OK;
}

esp_err_t audio_capture_start(void)
{
    if (s_is_recording) {
        ESP_LOGW(TAG, "Already recording");
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_wav_buffer) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Reset buffer */
    s_pcm_bytes_written = 0;
    memset(s_wav_buffer, 0, AUDIO_WAV_BUFFER_BYTES);
    s_is_recording = true;

    /* Enable I2S channel */
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_chan));

    /* Start recording task on core 1 */
    BaseType_t ret = xTaskCreatePinnedToCore(
        recording_task, "rec_task",
        4096, NULL, 5,
        &s_record_task_handle, 1);

    if (ret != pdPASS) {
        s_is_recording = false;
        i2s_channel_disable(s_rx_chan);
        ESP_LOGE(TAG, "Failed to create recording task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t audio_capture_stop(void)
{
    if (!s_is_recording) {
        /* May have already auto-stopped */
        return ESP_OK;
    }

    s_is_recording = false;

    /* Wait for recording task to finish */
    uint32_t timeout_ms = 500;
    while (s_record_task_handle != NULL && timeout_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout_ms -= 10;
    }

    i2s_channel_disable(s_rx_chan);
    return ESP_OK;
}

uint8_t *audio_capture_get_wav_buffer(void)
{
    if (s_is_recording || s_pcm_bytes_written == 0) {
        return NULL;
    }
    return s_wav_buffer;
}

size_t audio_capture_get_wav_size(void)
{
    return WAV_HEADER_SIZE + s_pcm_bytes_written;
}

bool audio_capture_is_recording(void)
{
    return s_is_recording;
}

esp_err_t audio_capture_deinit(void)
{
    audio_capture_stop();

    if (s_rx_chan) {
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
    }
    if (s_wav_buffer) {
        heap_caps_free(s_wav_buffer);
        s_wav_buffer = NULL;
    }
    return ESP_OK;
}