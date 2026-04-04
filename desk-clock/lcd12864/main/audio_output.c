#include "audio_output.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define AUDIO_DMA_DESC_NUM          6
#define AUDIO_DMA_FRAME_NUM         240
#define AUDIO_PCM_CHUNK_SAMPLES     256
#define AUDIO_IO_TIMEOUT_MS         1000
#define AUDIO_SEGMENT_GAP_MS        80
#define AUDIO_SILENCE_TAIL_MS       30
#define AUDIO_SPEECH_GAIN_PCT       180
#define AUDIO_SPEECH_SAMPLE_RATE_HZ 8000

typedef struct {
    const uint8_t *data;
    size_t size;
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
} wav_info_t;

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
} wav_asset_t;

#define HOUR_ASSET_LIST(X) \
    X(hour_00) \
    X(hour_01) \
    X(hour_02) \
    X(hour_03) \
    X(hour_04) \
    X(hour_05) \
    X(hour_06) \
    X(hour_07) \
    X(hour_08) \
    X(hour_09) \
    X(hour_10) \
    X(hour_11) \
    X(hour_12) \
    X(hour_13) \
    X(hour_14) \
    X(hour_15) \
    X(hour_16) \
    X(hour_17) \
    X(hour_18) \
    X(hour_19) \
    X(hour_20) \
    X(hour_21) \
    X(hour_22) \
    X(hour_23)

#define DECLARE_WAV_ASSET(name) \
    extern const uint8_t name##_wav_start[] asm("_binary_" #name "_wav_start"); \
    extern const uint8_t name##_wav_end[] asm("_binary_" #name "_wav_end");

#define WAV_ASSET_ENTRY(name) { name##_wav_start, name##_wav_end },

static const char *TAG = "audio_output";

DECLARE_WAV_ASSET(prefix_beijing_time)
DECLARE_WAV_ASSET(suffix_exact_hour)
HOUR_ASSET_LIST(DECLARE_WAV_ASSET)

static const wav_asset_t s_hour_assets[] = {
    HOUR_ASSET_LIST(WAV_ASSET_ENTRY)
};

static i2s_chan_handle_t s_tx_handle;
static StaticSemaphore_t s_audio_mutex_buffer;
static SemaphoreHandle_t s_audio_mutex;
static bool s_audio_ready;
static uint32_t s_output_sample_rate = AUDIO_SPEECH_SAMPLE_RATE_HZ;

static size_t min_size(size_t a, size_t b)
{
    return a < b ? a : b;
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
}

static int16_t apply_pcm_gain(int16_t sample, uint32_t gain_pct)
{
    int32_t scaled = ((int32_t)sample * (int32_t)gain_pct) / 100;

    if (scaled > INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)scaled;
}

static void ensure_audio_mutex(void)
{
    if (s_audio_mutex == NULL) {
        s_audio_mutex = xSemaphoreCreateMutexStatic(&s_audio_mutex_buffer);
    }
}

static esp_err_t audio_output_lock(void)
{
    ensure_audio_mutex();
    ESP_RETURN_ON_FALSE(s_audio_mutex != NULL, ESP_ERR_NO_MEM, TAG, "create audio mutex failed");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_audio_mutex, portMAX_DELAY) == pdTRUE,
                        ESP_FAIL, TAG, "take audio mutex failed");
    return ESP_OK;
}

static void audio_output_unlock(void)
{
    if (s_audio_mutex != NULL) {
        (void)xSemaphoreGive(s_audio_mutex);
    }
}

static esp_err_t audio_output_enable_tx(void)
{
    return i2s_channel_enable(s_tx_handle);
}

static esp_err_t audio_output_disable_tx(void)
{
    return i2s_channel_disable(s_tx_handle);
}

static esp_err_t audio_output_set_sample_rate(uint32_t sample_rate)
{
    ESP_RETURN_ON_FALSE(s_audio_ready && s_tx_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "audio not ready");
    ESP_RETURN_ON_FALSE(sample_rate > 0, ESP_ERR_INVALID_ARG, TAG, "invalid sample rate");

    if (sample_rate == s_output_sample_rate) {
        return ESP_OK;
    }

    const i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_tx_handle, &clk_cfg),
                        TAG, "reconfig i2s clock failed");
    s_output_sample_rate = sample_rate;
    return ESP_OK;
}

static esp_err_t parse_wav(const uint8_t *wav, size_t wav_size, wav_info_t *info)
{
    ESP_RETURN_ON_FALSE(wav != NULL && info != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid wav args");
    ESP_RETURN_ON_FALSE(wav_size >= 44, ESP_ERR_INVALID_ARG, TAG, "wav too small");
    ESP_RETURN_ON_FALSE(memcmp(wav, "RIFF", 4) == 0 && memcmp(wav + 8, "WAVE", 4) == 0,
                        ESP_ERR_INVALID_ARG, TAG, "invalid wav header");

    memset(info, 0, sizeof(*info));
    size_t offset = 12;
    while (offset + 8 <= wav_size) {
        const uint8_t *chunk = wav + offset;
        const uint32_t chunk_size = read_le32(chunk + 4);
        const size_t next_offset = offset + 8 + chunk_size + (chunk_size & 0x1U);
        if (next_offset > wav_size) {
            break;
        }

        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            info->audio_format = read_le16(chunk + 8);
            info->channels = read_le16(chunk + 10);
            info->sample_rate = read_le32(chunk + 12);
            info->bits_per_sample = read_le16(chunk + 22);
        } else if (memcmp(chunk, "data", 4) == 0) {
            info->data = chunk + 8;
            info->size = chunk_size;
        }

        offset = next_offset;
    }

    ESP_RETURN_ON_FALSE(info->data != NULL, ESP_ERR_INVALID_ARG, TAG, "wav data missing");
    ESP_RETURN_ON_FALSE(info->audio_format == 1, ESP_ERR_NOT_SUPPORTED, TAG, "wav must be PCM");
    ESP_RETURN_ON_FALSE(info->channels == 1, ESP_ERR_NOT_SUPPORTED, TAG, "wav must be mono");
    ESP_RETURN_ON_FALSE(info->bits_per_sample == 16, ESP_ERR_NOT_SUPPORTED, TAG, "wav must be 16-bit");
    ESP_RETURN_ON_FALSE(info->sample_rate > 0, ESP_ERR_NOT_SUPPORTED, TAG, "wav sample rate missing");
    return ESP_OK;
}

static esp_err_t write_silence_ms(uint32_t duration_ms)
{
    const uint32_t total_frames = (s_output_sample_rate * duration_ms) / 1000;
    int16_t frame_buf[AUDIO_PCM_CHUNK_SAMPLES * 2] = {0};

    for (uint32_t written_frames = 0; written_frames < total_frames; ) {
        const size_t chunk_frames = min_size(AUDIO_PCM_CHUNK_SAMPLES, total_frames - written_frames);
        size_t bytes_written = 0;
        ESP_RETURN_ON_ERROR(
            i2s_channel_write(s_tx_handle, frame_buf, chunk_frames * 2 * sizeof(int16_t),
                              &bytes_written, AUDIO_IO_TIMEOUT_MS),
            TAG, "write silence failed");
        written_frames += bytes_written / (2 * sizeof(int16_t));
    }

    return ESP_OK;
}

static esp_err_t play_wav_info(const wav_info_t *wav)
{
    const int16_t *mono = (const int16_t *)wav->data;
    const size_t total_samples = wav->size / sizeof(int16_t);
    int16_t frame_buf[AUDIO_PCM_CHUNK_SAMPLES * 2] = {0};

    ESP_RETURN_ON_FALSE(wav->sample_rate == s_output_sample_rate,
                        ESP_ERR_INVALID_STATE, TAG, "wav sample rate mismatch");

    for (size_t sample_offset = 0; sample_offset < total_samples; ) {
        const size_t chunk_samples = min_size(AUDIO_PCM_CHUNK_SAMPLES, total_samples - sample_offset);

        for (size_t i = 0; i < chunk_samples; i++) {
            const int16_t sample = apply_pcm_gain(mono[sample_offset + i], AUDIO_SPEECH_GAIN_PCT);
            frame_buf[i * 2] = sample;
            frame_buf[i * 2 + 1] = sample;
        }

        size_t bytes_written = 0;
        ESP_RETURN_ON_ERROR(
            i2s_channel_write(s_tx_handle, frame_buf, chunk_samples * 2 * sizeof(int16_t),
                              &bytes_written, AUDIO_IO_TIMEOUT_MS),
            TAG, "write wav failed");
        sample_offset += bytes_written / (2 * sizeof(int16_t));
    }

    return ESP_OK;
}

static esp_err_t play_wav_asset(const wav_asset_t *asset)
{
    wav_info_t wav = {0};
    const size_t wav_size = (size_t)(asset->end - asset->start);

    ESP_RETURN_ON_ERROR(parse_wav(asset->start, wav_size, &wav), TAG, "parse wav failed");
    return play_wav_info(&wav);
}

static const wav_asset_t *get_hour_asset(int hour)
{
    if (hour < 0 || hour >= (int)(sizeof(s_hour_assets) / sizeof(s_hour_assets[0]))) {
        return NULL;
    }
    return &s_hour_assets[hour];
}

static esp_err_t audio_output_begin_speech_locked(bool *tx_enabled)
{
    esp_err_t ret = audio_output_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = audio_output_set_sample_rate(AUDIO_SPEECH_SAMPLE_RATE_HZ);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = audio_output_enable_tx();
    if (ret != ESP_OK) {
        return ret;
    }

    *tx_enabled = true;
    return ESP_OK;
}

static esp_err_t audio_output_finish_speech_locked(bool tx_enabled, esp_err_t ret)
{
    if (tx_enabled) {
        const esp_err_t disable_ret = audio_output_disable_tx();
        if (ret == ESP_OK && disable_ret != ESP_OK) {
            ret = disable_ret;
        }
    }

    if (s_audio_ready) {
        const esp_err_t deinit_ret = audio_output_deinit();
        if (ret == ESP_OK && deinit_ret != ESP_OK) {
            ret = deinit_ret;
        }
    }

    return ret;
}

static esp_err_t audio_output_announce_hour_locked(int hour)
{
    const wav_asset_t prefix_asset = { prefix_beijing_time_wav_start, prefix_beijing_time_wav_end };
    const wav_asset_t exact_hour_asset = { suffix_exact_hour_wav_start, suffix_exact_hour_wav_end };
    const wav_asset_t *hour_asset = get_hour_asset(hour);
    bool tx_enabled = false;
    esp_err_t ret;

    ESP_RETURN_ON_FALSE(hour_asset != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid hour");

    ret = audio_output_begin_speech_locked(&tx_enabled);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = play_wav_asset(&prefix_asset);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = write_silence_ms(AUDIO_SEGMENT_GAP_MS);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = play_wav_asset(hour_asset);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = write_silence_ms(AUDIO_SEGMENT_GAP_MS);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = play_wav_asset(&exact_hour_asset);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = write_silence_ms(AUDIO_SILENCE_TAIL_MS);

cleanup:
    return audio_output_finish_speech_locked(tx_enabled, ret);
}

esp_err_t audio_output_init(void)
{
    ensure_audio_mutex();

    if (s_audio_ready) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = AUDIO_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = AUDIO_DMA_FRAME_NUM;
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_handle, NULL), TAG, "alloc i2s tx failed");

    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(s_output_sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)CONFIG_AUDIO_I2S_BCLK_PIN,
            .ws = (gpio_num_t)CONFIG_AUDIO_I2S_WS_PIN,
            .dout = (gpio_num_t)CONFIG_AUDIO_I2S_DOUT_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_handle, &std_cfg), TAG, "init i2s std failed");

    s_audio_ready = true;
    ESP_LOGI(TAG, "I2S ready: BCLK=%d WS=%d DOUT=%d rate=%lu",
             CONFIG_AUDIO_I2S_BCLK_PIN, CONFIG_AUDIO_I2S_WS_PIN, CONFIG_AUDIO_I2S_DOUT_PIN,
             (unsigned long)s_output_sample_rate);
    return ESP_OK;
}

esp_err_t audio_output_deinit(void)
{
    if (s_tx_handle != NULL) {
        ESP_RETURN_ON_ERROR(i2s_del_channel(s_tx_handle), TAG, "delete i2s channel failed");
        s_tx_handle = NULL;
    }

    s_audio_ready = false;
    s_output_sample_rate = AUDIO_SPEECH_SAMPLE_RATE_HZ;
    return ESP_OK;
}

bool audio_output_is_ready(void)
{
    return s_audio_ready;
}

esp_err_t audio_output_play_beijing_hour_announcement(int hour)
{
    esp_err_t ret = audio_output_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = audio_output_announce_hour_locked(hour);
    audio_output_unlock();
    return ret;
}
