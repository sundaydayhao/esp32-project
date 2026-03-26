#include "audio_player.h"
#include "fft_analyzer.h"
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "minimp3.h"

static const char *TAG = "audio_player";

/* ── I2S ─────────────────────────────────────────────────────────────── */
#define I2S_NUM             I2S_NUM_0
#define I2S_BCK_IO          CONFIG_I2S_BCK_PIN
#define I2S_WS_IO           CONFIG_I2S_WS_PIN
#define I2S_DO_IO           CONFIG_I2S_DOUT_PIN
#define SAMPLE_RATE         CONFIG_AUDIO_SAMPLE_RATE

/* ── Buffers ─────────────────────────────────────────────────────────── */
#define INPUT_RINGBUF_SIZE  (64 * 1024)   /* 64 KB compressed data  */
#define PCM_RINGBUF_SIZE    (32 * 1024)   /* 32 KB decoded PCM      */
#define HTTP_READ_SIZE      (4 * 1024)    /* HTTP read chunk        */
#define MP3_FRAME_SIZE      MINIMP3_MAX_SAMPLES_PER_FRAME

/* ── Tasks ───────────────────────────────────────────────────────────── */
#define SOURCE_TASK_STACK   (8 * 1024)
#define DECODE_TASK_STACK   (8 * 1024)
#define OUTPUT_TASK_STACK   (4 * 1024)
#define SOURCE_TASK_PRIO    5
#define DECODE_TASK_PRIO    6
#define OUTPUT_TASK_PRIO    7

/* ── State ───────────────────────────────────────────────────────────── */
static audio_status_t   s_status;
static SemaphoreHandle_t s_status_mutex = NULL;
static SemaphoreHandle_t s_ctrl_sem     = NULL;   /* signals state change */

static RingbufHandle_t  s_input_rb  = NULL;
static RingbufHandle_t  s_pcm_rb    = NULL;

static i2s_chan_handle_t s_i2s_tx   = NULL;

static TaskHandle_t s_source_task  = NULL;
static TaskHandle_t s_decode_task  = NULL;
static TaskHandle_t s_output_task  = NULL;

static volatile bool s_stop_request = false;

/* ── Volume ──────────────────────────────────────────────────────────── */
static uint8_t s_volume = 80;   /* 0-100 */

/* ── Helpers ─────────────────────────────────────────────────────────── */
static void set_state(audio_state_t st)
{
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_status.state = st;
    xSemaphoreGive(s_status_mutex);
}

static void apply_volume(int16_t *samples, int count)
{
    if (s_volume >= 100) return;
    float gain = s_volume / 100.0f;
    for (int i = 0; i < count; i++) {
        samples[i] = (int16_t)(samples[i] * gain);
    }
}

/* ── I2S init ────────────────────────────────────────────────────────── */
static esp_err_t i2s_init(uint32_t sample_rate)
{
    if (s_i2s_tx) {
        i2s_channel_disable(s_i2s_tx);
        i2s_del_channel(s_i2s_tx);
        s_i2s_tx = NULL;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_i2s_tx, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_IO,
            .ws   = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_tx));
    ESP_LOGI(TAG, "I2S initialized: %lu Hz", (unsigned long)sample_rate);
    return ESP_OK;
}

/* ── Source task: HTTP ───────────────────────────────────────────────── */
static esp_err_t http_source_run(const char *url)
{
    esp_http_client_config_t config = {
        .url            = url,
        .timeout_ms     = 10000,
        .buffer_size    = HTTP_READ_SIZE,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
    esp_http_client_fetch_headers(client);

    uint8_t *buf = malloc(HTTP_READ_SIZE);
    if (!buf) {
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    while (!s_stop_request) {
        int read_len = esp_http_client_read(client, (char *)buf, HTTP_READ_SIZE);
        if (read_len < 0) {
            ESP_LOGE(TAG, "HTTP read error");
            break;
        }
        if (read_len == 0) break;   /* EOF */

        /* Push to ring buffer (block until space available or stop) */
        BaseType_t sent = xRingbufferSend(s_input_rb, buf, read_len,
                                          pdMS_TO_TICKS(2000));
        if (sent != pdTRUE) {
            ESP_LOGW(TAG, "Input ring buffer full, dropping data");
        }
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}

/* ── Source task: local file ─────────────────────────────────────────── */
static esp_err_t local_source_run(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open file: %s", path);
        return ESP_FAIL;
    }

    uint8_t *buf = malloc(HTTP_READ_SIZE);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }

    while (!s_stop_request) {
        size_t n = fread(buf, 1, HTTP_READ_SIZE, f);
        if (n == 0) break;
        xRingbufferSend(s_input_rb, buf, n, pdMS_TO_TICKS(2000));
    }

    free(buf);
    fclose(f);
    return ESP_OK;
}

/* ── Source task ─────────────────────────────────────────────────────── */
static void source_task(void *arg)
{
    audio_track_t *track = (audio_track_t *)arg;
    ESP_LOGI(TAG, "Source task started: %s", track->url);

    if (track->source == AUDIO_SOURCE_HTTP) {
        http_source_run(track->url);
    } else {
        local_source_run(track->url);
    }

    /* Signal end-of-stream by sending a zero-length sentinel */
    uint8_t eos = 0;
    xRingbufferSend(s_input_rb, &eos, 0, pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "Source task done");
    s_source_task = NULL;
    vTaskDelete(NULL);
}

/* ── Decode task ─────────────────────────────────────────────────────── */
static void decode_task(void *arg)
{
    mp3dec_t mp3d;
    mp3dec_init(&mp3d);

    /* Accumulate compressed data here */
    uint8_t *stream_buf = malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * 4);
    int16_t *pcm_buf    = malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t) * 2);
    if (!stream_buf || !pcm_buf) {
        ESP_LOGE(TAG, "OOM in decode task");
        goto done;
    }

    size_t stream_len = 0;
    bool   eos        = false;

    while (!s_stop_request && !eos) {
        /* Refill stream buffer from ring buffer */
        while (stream_len < (size_t)(MINIMP3_MAX_SAMPLES_PER_FRAME * 4) && !eos) {
            size_t item_size = 0;
            uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(
                s_input_rb, &item_size,
                pdMS_TO_TICKS(500),
                MINIMP3_MAX_SAMPLES_PER_FRAME * 4 - stream_len);

            if (item == NULL) {
                /* Check if source task finished */
                if (s_source_task == NULL) eos = true;
                break;
            }
            if (item_size == 0) {
                vRingbufferReturnItem(s_input_rb, item);
                eos = true;
                break;
            }
            memcpy(stream_buf + stream_len, item, item_size);
            stream_len += item_size;
            vRingbufferReturnItem(s_input_rb, item);
        }

        if (stream_len == 0) break;

        /* Decode one MP3 frame */
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&mp3d, stream_buf, (int)stream_len,
                                          pcm_buf, &info);

        if (info.frame_bytes == 0) {
            /* No sync found, skip a byte */
            if (stream_len > 1) {
                memmove(stream_buf, stream_buf + 1, stream_len - 1);
                stream_len--;
            } else {
                stream_len = 0;
            }
            continue;
        }

        /* Consume decoded bytes */
        memmove(stream_buf, stream_buf + info.frame_bytes,
                stream_len - info.frame_bytes);
        stream_len -= info.frame_bytes;

        if (samples <= 0) continue;

        /* Update track info from first frame */
        xSemaphoreTake(s_status_mutex, portMAX_DELAY);
        if (s_status.current_track.sample_rate == 0) {
            s_status.current_track.sample_rate = info.hz;
            s_status.current_track.channels    = info.channels;
            s_status.current_track.bitrate     = info.bitrate_kbps;
            xSemaphoreGive(s_status_mutex);
            i2s_init(info.hz);
        } else {
            xSemaphoreGive(s_status_mutex);
        }

        /* Convert mono to stereo if needed */
        int total_samples = samples * info.channels;
        if (info.channels == 1) {
            /* Expand mono to stereo in-place (work backwards) */
            for (int i = samples - 1; i >= 0; i--) {
                pcm_buf[i * 2 + 1] = pcm_buf[i];
                pcm_buf[i * 2]     = pcm_buf[i];
            }
            total_samples = samples * 2;
        }

        apply_volume(pcm_buf, total_samples);

        /* Feed FFT analyzer */
        fft_analyzer_feed(pcm_buf, total_samples);

        /* Push PCM to output ring buffer */
        size_t pcm_bytes = total_samples * sizeof(int16_t);
        xRingbufferSend(s_pcm_rb, pcm_buf, pcm_bytes, pdMS_TO_TICKS(1000));
    }

done:
    free(stream_buf);
    free(pcm_buf);
    ESP_LOGI(TAG, "Decode task done");
    s_decode_task = NULL;
    vTaskDelete(NULL);
}

/* ── Output task ─────────────────────────────────────────────────────── */
static void output_task(void *arg)
{
    uint32_t position_samples = 0;

    while (!s_stop_request) {
        size_t item_size = 0;
        void *item = xRingbufferReceive(s_pcm_rb, &item_size, pdMS_TO_TICKS(500));

        if (item == NULL) {
            /* Check if decode task finished */
            if (s_decode_task == NULL) break;
            continue;
        }

        /* Handle pause */
        while (!s_stop_request) {
            xSemaphoreTake(s_status_mutex, portMAX_DELAY);
            audio_state_t st = s_status.state;
            xSemaphoreGive(s_status_mutex);
            if (st != AUDIO_STATE_PAUSED) break;
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        size_t bytes_written = 0;
        i2s_channel_write(s_i2s_tx, item, item_size, &bytes_written,
                          pdMS_TO_TICKS(1000));
        vRingbufferReturnItem(s_pcm_rb, item);

        /* Update position */
        xSemaphoreTake(s_status_mutex, portMAX_DELAY);
        uint32_t sr = s_status.current_track.sample_rate;
        xSemaphoreGive(s_status_mutex);
        if (sr > 0) {
            position_samples += item_size / (2 * sizeof(int16_t));
            xSemaphoreTake(s_status_mutex, portMAX_DELAY);
            s_status.position_sec = position_samples / sr;
            xSemaphoreGive(s_status_mutex);
        }
    }

    set_state(AUDIO_STATE_STOPPED);
    ESP_LOGI(TAG, "Output task done");
    s_output_task = NULL;
    vTaskDelete(NULL);
}

/* ── Public API ──────────────────────────────────────────────────────── */
esp_err_t audio_player_init(void)
{
    s_status_mutex = xSemaphoreCreateMutex();
    s_ctrl_sem     = xSemaphoreCreateBinary();
    if (!s_status_mutex || !s_ctrl_sem) return ESP_ERR_NO_MEM;

    memset(&s_status, 0, sizeof(s_status));
    s_status.state  = AUDIO_STATE_IDLE;
    s_status.volume = s_volume;

    s_input_rb = xRingbufferCreate(INPUT_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    s_pcm_rb   = xRingbufferCreate(PCM_RINGBUF_SIZE,   RINGBUF_TYPE_BYTEBUF);
    if (!s_input_rb || !s_pcm_rb) return ESP_ERR_NO_MEM;

    /* Initialize I2S with default sample rate; will be reconfigured on first frame */
    i2s_init(SAMPLE_RATE);

    ESP_LOGI(TAG, "Audio player initialized");
    return ESP_OK;
}

esp_err_t audio_player_stop(void)
{
    s_stop_request = true;
    /* Wait for tasks to finish */
    for (int i = 0; i < 50; i++) {
        if (!s_source_task && !s_decode_task && !s_output_task) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    s_stop_request = false;

    /* Flush ring buffers */
    size_t item_size;
    void *item;
    while ((item = xRingbufferReceive(s_input_rb, &item_size, 0)) != NULL)
        vRingbufferReturnItem(s_input_rb, item);
    while ((item = xRingbufferReceive(s_pcm_rb, &item_size, 0)) != NULL)
        vRingbufferReturnItem(s_pcm_rb, item);

    set_state(AUDIO_STATE_STOPPED);
    return ESP_OK;
}

esp_err_t audio_player_play(const char *url, const char *title, const char *artist)
{
    if (!url) return ESP_ERR_INVALID_ARG;

    /* Stop any current playback */
    if (s_status.state == AUDIO_STATE_PLAYING ||
        s_status.state == AUDIO_STATE_PAUSED) {
        audio_player_stop();
    }

    /* Populate track info */
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    memset(&s_status.current_track, 0, sizeof(audio_track_t));
    strncpy(s_status.current_track.url, url, AUDIO_URL_MAX_LEN - 1);
    if (title)  strncpy(s_status.current_track.title,  title,  AUDIO_TITLE_MAX_LEN - 1);
    else        strncpy(s_status.current_track.title,  "Unknown", AUDIO_TITLE_MAX_LEN - 1);
    if (artist) strncpy(s_status.current_track.artist, artist, AUDIO_ARTIST_MAX_LEN - 1);
    else        strncpy(s_status.current_track.artist, "Unknown", AUDIO_ARTIST_MAX_LEN - 1);

    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        s_status.current_track.source = AUDIO_SOURCE_HTTP;
    } else {
        s_status.current_track.source = AUDIO_SOURCE_LOCAL;
    }
    s_status.position_sec = 0;
    s_status.state = AUDIO_STATE_PLAYING;
    xSemaphoreGive(s_status_mutex);

    /* Start pipeline tasks */
    xTaskCreate(source_task, "audio_src",  SOURCE_TASK_STACK,
                &s_status.current_track, SOURCE_TASK_PRIO, &s_source_task);
    xTaskCreate(decode_task, "audio_dec",  DECODE_TASK_STACK,
                NULL, DECODE_TASK_PRIO, &s_decode_task);
    xTaskCreate(output_task, "audio_out",  OUTPUT_TASK_STACK,
                NULL, OUTPUT_TASK_PRIO, &s_output_task);

    ESP_LOGI(TAG, "Playing: %s", url);
    return ESP_OK;
}

esp_err_t audio_player_pause(void)
{
    if (s_status.state != AUDIO_STATE_PLAYING) return ESP_ERR_INVALID_STATE;
    set_state(AUDIO_STATE_PAUSED);
    return ESP_OK;
}

esp_err_t audio_player_resume(void)
{
    if (s_status.state != AUDIO_STATE_PAUSED) return ESP_ERR_INVALID_STATE;
    set_state(AUDIO_STATE_PLAYING);
    return ESP_OK;
}

esp_err_t audio_player_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    s_volume = volume;
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_status.volume = volume;
    xSemaphoreGive(s_status_mutex);
    return ESP_OK;
}

void audio_player_get_status(audio_status_t *status)
{
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    memcpy(status, &s_status, sizeof(audio_status_t));
    xSemaphoreGive(s_status_mutex);
}

audio_state_t audio_player_get_state(void)
{
    return s_status.state;
}