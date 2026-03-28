#include "audio_output.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"

#include "config.h"

#define AUDIO_DMA_DESC_NUM          6
#define AUDIO_DMA_FRAME_NUM         240
#define AUDIO_TONE_CHUNK_FRAMES     256
#define AUDIO_PCM_CHUNK_SAMPLES     256
#define AUDIO_SILENCE_TAIL_MS       30

typedef struct {
    const uint8_t *data;
    size_t size;
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
} wav_info_t;

static i2s_chan_handle_t s_tx_handle = NULL;
static bool s_audio_ready = false;

extern const uint8_t prefix_beijing_time_wav_start[] asm("_binary_prefix_beijing_time_wav_start");
extern const uint8_t prefix_beijing_time_wav_end[] asm("_binary_prefix_beijing_time_wav_end");
extern const uint8_t suffix_oclock_wav_start[] asm("_binary_suffix_oclock_wav_start");
extern const uint8_t suffix_oclock_wav_end[] asm("_binary_suffix_oclock_wav_end");
extern const uint8_t hour_08_wav_start[] asm("_binary_hour_08_wav_start");
extern const uint8_t hour_08_wav_end[] asm("_binary_hour_08_wav_end");
extern const uint8_t hour_09_wav_start[] asm("_binary_hour_09_wav_start");
extern const uint8_t hour_09_wav_end[] asm("_binary_hour_09_wav_end");
extern const uint8_t hour_10_wav_start[] asm("_binary_hour_10_wav_start");
extern const uint8_t hour_10_wav_end[] asm("_binary_hour_10_wav_end");
extern const uint8_t hour_11_wav_start[] asm("_binary_hour_11_wav_start");
extern const uint8_t hour_11_wav_end[] asm("_binary_hour_11_wav_end");
extern const uint8_t hour_12_wav_start[] asm("_binary_hour_12_wav_start");
extern const uint8_t hour_12_wav_end[] asm("_binary_hour_12_wav_end");
extern const uint8_t hour_13_wav_start[] asm("_binary_hour_13_wav_start");
extern const uint8_t hour_13_wav_end[] asm("_binary_hour_13_wav_end");
extern const uint8_t hour_14_wav_start[] asm("_binary_hour_14_wav_start");
extern const uint8_t hour_14_wav_end[] asm("_binary_hour_14_wav_end");
extern const uint8_t hour_15_wav_start[] asm("_binary_hour_15_wav_start");
extern const uint8_t hour_15_wav_end[] asm("_binary_hour_15_wav_end");
extern const uint8_t hour_16_wav_start[] asm("_binary_hour_16_wav_start");
extern const uint8_t hour_16_wav_end[] asm("_binary_hour_16_wav_end");
extern const uint8_t hour_17_wav_start[] asm("_binary_hour_17_wav_start");
extern const uint8_t hour_17_wav_end[] asm("_binary_hour_17_wav_end");
extern const uint8_t hour_18_wav_start[] asm("_binary_hour_18_wav_start");
extern const uint8_t hour_18_wav_end[] asm("_binary_hour_18_wav_end");
extern const uint8_t hour_19_wav_start[] asm("_binary_hour_19_wav_start");
extern const uint8_t hour_19_wav_end[] asm("_binary_hour_19_wav_end");
extern const uint8_t hour_20_wav_start[] asm("_binary_hour_20_wav_start");
extern const uint8_t hour_20_wav_end[] asm("_binary_hour_20_wav_end");

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

static esp_err_t audio_output_enable_tx(void)
{
    return i2s_channel_enable(s_tx_handle);
}

static esp_err_t audio_output_disable_tx(void)
{
    return i2s_channel_disable(s_tx_handle);
}

static esp_err_t parse_wav(const uint8_t *wav, size_t wav_size, wav_info_t *info)
{
    ESP_RETURN_ON_FALSE(wav != NULL && info != NULL, ESP_ERR_INVALID_ARG, TAG_AUDIO, "invalid wav args");
    ESP_RETURN_ON_FALSE(wav_size >= 44, ESP_ERR_INVALID_ARG, TAG_AUDIO, "wav too small");
    ESP_RETURN_ON_FALSE(memcmp(wav, "RIFF", 4) == 0 && memcmp(wav + 8, "WAVE", 4) == 0,
                        ESP_ERR_INVALID_ARG, TAG_AUDIO, "invalid wav header");

    memset(info, 0, sizeof(*info));
    size_t offset = 12;
    while (offset + 8 <= wav_size) {
        const uint8_t *chunk = wav + offset;
        uint32_t chunk_size = read_le32(chunk + 4);
        size_t next_offset = offset + 8 + chunk_size + (chunk_size & 0x1U);
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

    ESP_RETURN_ON_FALSE(info->data != NULL, ESP_ERR_INVALID_ARG, TAG_AUDIO, "wav data chunk missing");
    ESP_RETURN_ON_FALSE(info->audio_format == 1, ESP_ERR_NOT_SUPPORTED, TAG_AUDIO, "wav must be PCM");
    ESP_RETURN_ON_FALSE(info->channels == 1, ESP_ERR_NOT_SUPPORTED, TAG_AUDIO, "wav must be mono");
    ESP_RETURN_ON_FALSE(info->bits_per_sample == 16, ESP_ERR_NOT_SUPPORTED, TAG_AUDIO, "wav must be 16-bit");
    ESP_RETURN_ON_FALSE(info->sample_rate == AUDIO_I2S_SAMPLE_RATE_HZ, ESP_ERR_NOT_SUPPORTED, TAG_AUDIO, "unexpected wav sample rate");
    return ESP_OK;
}

static esp_err_t write_silence_ms(uint32_t duration_ms)
{
    const uint32_t total_frames = (AUDIO_I2S_SAMPLE_RATE_HZ * duration_ms) / 1000;
    int16_t frame_buf[AUDIO_PCM_CHUNK_SAMPLES * 2] = {0};

    for (uint32_t written_frames = 0; written_frames < total_frames; ) {
        const size_t chunk_frames = min_size(AUDIO_PCM_CHUNK_SAMPLES, total_frames - written_frames);
        size_t bytes_written = 0;
        ESP_RETURN_ON_ERROR(
            i2s_channel_write(s_tx_handle, frame_buf, chunk_frames * 2 * sizeof(int16_t), &bytes_written, 1000),
            TAG_AUDIO, "write silence failed");
        written_frames += bytes_written / (2 * sizeof(int16_t));
    }

    return ESP_OK;
}

static esp_err_t play_wav_segment(const uint8_t *wav_start, const uint8_t *wav_end)
{
    wav_info_t wav = {0};
    size_t wav_size = (size_t)(wav_end - wav_start);
    ESP_RETURN_ON_ERROR(parse_wav(wav_start, wav_size, &wav), TAG_AUDIO, "parse wav failed");

    const int16_t *mono = (const int16_t *)wav.data;
    const size_t total_samples = wav.size / sizeof(int16_t);
    int16_t frame_buf[AUDIO_PCM_CHUNK_SAMPLES * 2] = {0};

    for (size_t sample_offset = 0; sample_offset < total_samples; ) {
        const size_t chunk_samples = min_size(AUDIO_PCM_CHUNK_SAMPLES, total_samples - sample_offset);
        for (size_t i = 0; i < chunk_samples; i++) {
            int16_t sample = mono[sample_offset + i];
            frame_buf[i * 2] = sample;
            frame_buf[i * 2 + 1] = sample;
        }

        size_t bytes_written = 0;
        ESP_RETURN_ON_ERROR(
            i2s_channel_write(s_tx_handle, frame_buf, chunk_samples * 2 * sizeof(int16_t), &bytes_written, 1000),
            TAG_AUDIO, "write wav failed");
        sample_offset += bytes_written / (2 * sizeof(int16_t));
    }

    return ESP_OK;
}

esp_err_t audio_output_init(void)
{
    if (s_audio_ready) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = AUDIO_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = AUDIO_DMA_FRAME_NUM;
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_handle, NULL), TAG_AUDIO, "alloc i2s tx failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_I2S_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_I2S_BCLK_PIN,
            .ws = AUDIO_I2S_WS_PIN,
            .dout = AUDIO_I2S_DOUT_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_handle, &std_cfg), TAG_AUDIO, "init i2s std failed");

    s_audio_ready = true;
    ESP_LOGI(TAG_AUDIO, "MAX98357A I2S ready: BCLK=%d WS=%d DIN=%d rate=%d",
             AUDIO_I2S_BCLK_PIN, AUDIO_I2S_WS_PIN, AUDIO_I2S_DOUT_PIN, AUDIO_I2S_SAMPLE_RATE_HZ);
    return ESP_OK;
}

esp_err_t audio_output_deinit(void)
{
    if (s_tx_handle != NULL) {
        esp_err_t ret = i2s_del_channel(s_tx_handle);
        if (ret != ESP_OK) {
            return ret;
        }
        s_tx_handle = NULL;
    }

    s_audio_ready = false;
    return ESP_OK;
}

bool audio_output_is_ready(void)
{
    return s_audio_ready;
}

esp_err_t audio_output_play_tone(uint32_t freq_hz, uint32_t duration_ms, uint8_t volume_pct)
{
    ESP_RETURN_ON_FALSE(s_audio_ready && s_tx_handle != NULL, ESP_ERR_INVALID_STATE, TAG_AUDIO, "audio not ready");
    ESP_RETURN_ON_FALSE(freq_hz > 0, ESP_ERR_INVALID_ARG, TAG_AUDIO, "invalid frequency");
    ESP_RETURN_ON_FALSE(duration_ms > 0, ESP_ERR_INVALID_ARG, TAG_AUDIO, "invalid duration");

    if (volume_pct > 100) {
        volume_pct = 100;
    }

    const uint32_t total_frames = (AUDIO_I2S_SAMPLE_RATE_HZ * duration_ms) / 1000;
    const uint32_t tail_frames = (AUDIO_I2S_SAMPLE_RATE_HZ * AUDIO_SILENCE_TAIL_MS) / 1000;
    uint32_t half_period_frames = AUDIO_I2S_SAMPLE_RATE_HZ / (freq_hz * 2);
    if (half_period_frames == 0) {
        half_period_frames = 1;
    }

    const int16_t amplitude = (int16_t)((32767 * volume_pct) / 100 / 2);
    int16_t frame_buf[AUDIO_TONE_CHUNK_FRAMES * 2] = {0};
    uint32_t phase_frames = 0;

    ESP_RETURN_ON_ERROR(audio_output_enable_tx(), TAG_AUDIO, "enable i2s tx failed");

    for (uint32_t written_frames = 0; written_frames < total_frames; ) {
        const size_t chunk_frames = min_size(AUDIO_TONE_CHUNK_FRAMES, total_frames - written_frames);
        for (size_t i = 0; i < chunk_frames; i++) {
            const int16_t sample = (phase_frames < half_period_frames) ? amplitude : (int16_t)-amplitude;
            frame_buf[i * 2] = sample;
            frame_buf[i * 2 + 1] = sample;

            phase_frames++;
            if (phase_frames >= (half_period_frames * 2)) {
                phase_frames = 0;
            }
        }

        size_t bytes_written = 0;
        const size_t bytes_to_write = chunk_frames * 2 * sizeof(int16_t);
        ESP_RETURN_ON_ERROR(
            i2s_channel_write(s_tx_handle, frame_buf, bytes_to_write, &bytes_written, 1000),
            TAG_AUDIO, "write tone failed");
        written_frames += bytes_written / (2 * sizeof(int16_t));
    }

    for (uint32_t flushed_frames = 0; flushed_frames < tail_frames; ) {
        const size_t chunk_frames = min_size(AUDIO_TONE_CHUNK_FRAMES, tail_frames - flushed_frames);
        for (size_t i = 0; i < chunk_frames * 2; i++) {
            frame_buf[i] = 0;
        }

        size_t bytes_written = 0;
        const size_t bytes_to_write = chunk_frames * 2 * sizeof(int16_t);
        ESP_RETURN_ON_ERROR(
            i2s_channel_write(s_tx_handle, frame_buf, bytes_to_write, &bytes_written, 1000),
            TAG_AUDIO, "write silence failed");
        flushed_frames += bytes_written / (2 * sizeof(int16_t));
    }

    ESP_RETURN_ON_ERROR(audio_output_disable_tx(), TAG_AUDIO, "disable i2s tx failed");
    return ESP_OK;
}

esp_err_t audio_output_play_test_tone(void)
{
    return audio_output_play_tone(AUDIO_TONE_FREQ_HZ, AUDIO_TONE_DURATION_MS, AUDIO_TONE_VOLUME_PCT);
}

esp_err_t audio_output_play_beijing_hour_announcement(int hour)
{
    const uint8_t *hour_start = NULL;
    const uint8_t *hour_end = NULL;

    switch (hour) {
    case 8:
        hour_start = hour_08_wav_start;
        hour_end = hour_08_wav_end;
        break;
    case 9:
        hour_start = hour_09_wav_start;
        hour_end = hour_09_wav_end;
        break;
    case 10:
        hour_start = hour_10_wav_start;
        hour_end = hour_10_wav_end;
        break;
    case 11:
        hour_start = hour_11_wav_start;
        hour_end = hour_11_wav_end;
        break;
    case 12:
        hour_start = hour_12_wav_start;
        hour_end = hour_12_wav_end;
        break;
    case 13:
        hour_start = hour_13_wav_start;
        hour_end = hour_13_wav_end;
        break;
    case 14:
        hour_start = hour_14_wav_start;
        hour_end = hour_14_wav_end;
        break;
    case 15:
        hour_start = hour_15_wav_start;
        hour_end = hour_15_wav_end;
        break;
    case 16:
        hour_start = hour_16_wav_start;
        hour_end = hour_16_wav_end;
        break;
    case 17:
        hour_start = hour_17_wav_start;
        hour_end = hour_17_wav_end;
        break;
    case 18:
        hour_start = hour_18_wav_start;
        hour_end = hour_18_wav_end;
        break;
    case 19:
        hour_start = hour_19_wav_start;
        hour_end = hour_19_wav_end;
        break;
    case 20:
        hour_start = hour_20_wav_start;
        hour_end = hour_20_wav_end;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(audio_output_init(), TAG_AUDIO, "audio init failed");
    ESP_RETURN_ON_ERROR(audio_output_enable_tx(), TAG_AUDIO, "enable i2s tx failed");
    ESP_RETURN_ON_ERROR(play_wav_segment(prefix_beijing_time_wav_start, prefix_beijing_time_wav_end),
                        TAG_AUDIO, "play prefix failed");
    ESP_RETURN_ON_ERROR(write_silence_ms(AUDIO_SEGMENT_GAP_MS), TAG_AUDIO, "play gap failed");
    ESP_RETURN_ON_ERROR(play_wav_segment(hour_start, hour_end), TAG_AUDIO, "play hour failed");
    ESP_RETURN_ON_ERROR(write_silence_ms(AUDIO_SEGMENT_GAP_MS), TAG_AUDIO, "play gap failed");
    ESP_RETURN_ON_ERROR(play_wav_segment(suffix_oclock_wav_start, suffix_oclock_wav_end),
                        TAG_AUDIO, "play suffix failed");
    ESP_RETURN_ON_ERROR(write_silence_ms(AUDIO_SILENCE_TAIL_MS), TAG_AUDIO, "tail silence failed");
    ESP_RETURN_ON_ERROR(audio_output_disable_tx(), TAG_AUDIO, "disable i2s tx failed");
    ESP_RETURN_ON_ERROR(audio_output_deinit(), TAG_AUDIO, "audio deinit failed");
    return ESP_OK;
}
