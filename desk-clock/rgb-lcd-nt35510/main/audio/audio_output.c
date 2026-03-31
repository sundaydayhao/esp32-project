#include "audio_output.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "config.h"
#include "net/weather_client.h"

#define AUDIO_DMA_DESC_NUM          6
#define AUDIO_DMA_FRAME_NUM         240
#define AUDIO_TONE_CHUNK_FRAMES     256
#define AUDIO_PCM_CHUNK_SAMPLES     256
#define AUDIO_SILENCE_TAIL_MS       30
#define AUDIO_IO_TIMEOUT_MS         1000

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

#define NUMBER_TOKEN_ASSET_LIST(X) \
    X(number_00) \
    X(number_01) \
    X(number_02) \
    X(number_03) \
    X(number_04) \
    X(number_05) \
    X(number_06) \
    X(number_07) \
    X(number_08) \
    X(number_09) \
    X(number_10)

#define DECLARE_WAV_ASSET(name) \
    extern const uint8_t name##_wav_start[] asm("_binary_" #name "_wav_start"); \
    extern const uint8_t name##_wav_end[] asm("_binary_" #name "_wav_end");

#define WAV_ASSET(name) { name##_wav_start, name##_wav_end }
#define WAV_ASSET_ENTRY(name) { name##_wav_start, name##_wav_end },

DECLARE_WAV_ASSET(prefix_beijing_time)
DECLARE_WAV_ASSET(suffix_point)
DECLARE_WAV_ASSET(suffix_exact)
DECLARE_WAV_ASSET(phrase_today_weather)
DECLARE_WAV_ASSET(phrase_current_temperature)
DECLARE_WAV_ASSET(phrase_weather_not_ready)
DECLARE_WAV_ASSET(conjunction_to)
DECLARE_WAV_ASSET(suffix_degree)
DECLARE_WAV_ASSET(prefix_below_zero)
DECLARE_WAV_ASSET(weather_sunny)
DECLARE_WAV_ASSET(weather_cloudy)
DECLARE_WAV_ASSET(weather_overcast)
DECLARE_WAV_ASSET(weather_rain_snow)
DECLARE_WAV_ASSET(weather_thunder_rain)
DECLARE_WAV_ASSET(weather_rain)
DECLARE_WAV_ASSET(weather_snow)
DECLARE_WAV_ASSET(weather_sand_dust)
DECLARE_WAV_ASSET(weather_haze)
DECLARE_WAV_ASSET(weather_unknown)
NUMBER_TOKEN_ASSET_LIST(DECLARE_WAV_ASSET)

static const wav_asset_t s_number_token_assets[] = {
    NUMBER_TOKEN_ASSET_LIST(WAV_ASSET_ENTRY)
};

typedef enum {
    WEATHER_SPEECH_SUNNY = 0,
    WEATHER_SPEECH_CLOUDY,
    WEATHER_SPEECH_OVERCAST,
    WEATHER_SPEECH_RAIN_SNOW,
    WEATHER_SPEECH_THUNDER_RAIN,
    WEATHER_SPEECH_RAIN,
    WEATHER_SPEECH_SNOW,
    WEATHER_SPEECH_SAND_DUST,
    WEATHER_SPEECH_HAZE,
    WEATHER_SPEECH_UNKNOWN,
} weather_speech_t;

static const wav_asset_t s_weather_condition_assets[] = {
    [WEATHER_SPEECH_SUNNY] = WAV_ASSET(weather_sunny),
    [WEATHER_SPEECH_CLOUDY] = WAV_ASSET(weather_cloudy),
    [WEATHER_SPEECH_OVERCAST] = WAV_ASSET(weather_overcast),
    [WEATHER_SPEECH_RAIN_SNOW] = WAV_ASSET(weather_rain_snow),
    [WEATHER_SPEECH_THUNDER_RAIN] = WAV_ASSET(weather_thunder_rain),
    [WEATHER_SPEECH_RAIN] = WAV_ASSET(weather_rain),
    [WEATHER_SPEECH_SNOW] = WAV_ASSET(weather_snow),
    [WEATHER_SPEECH_SAND_DUST] = WAV_ASSET(weather_sand_dust),
    [WEATHER_SPEECH_HAZE] = WAV_ASSET(weather_haze),
    [WEATHER_SPEECH_UNKNOWN] = WAV_ASSET(weather_unknown),
};

static i2s_chan_handle_t s_tx_handle = NULL;
static StaticSemaphore_t s_audio_mutex_buffer;
static SemaphoreHandle_t s_audio_mutex = NULL;
static bool s_audio_ready = false;
static uint32_t s_output_sample_rate = AUDIO_I2S_SAMPLE_RATE_HZ;

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

static void audio_output_ensure_mutex(void)
{
    if (s_audio_mutex == NULL) {
        s_audio_mutex = xSemaphoreCreateMutexStatic(&s_audio_mutex_buffer);
    }
}

static esp_err_t audio_output_lock(void)
{
    audio_output_ensure_mutex();
    ESP_RETURN_ON_FALSE(s_audio_mutex != NULL, ESP_ERR_NO_MEM, TAG_AUDIO, "create audio mutex failed");
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_audio_mutex, portMAX_DELAY) == pdTRUE,
                        ESP_FAIL, TAG_AUDIO, "take audio mutex failed");
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
    ESP_RETURN_ON_FALSE(s_audio_ready && s_tx_handle != NULL, ESP_ERR_INVALID_STATE, TAG_AUDIO, "audio not ready");
    ESP_RETURN_ON_FALSE(sample_rate > 0, ESP_ERR_INVALID_ARG, TAG_AUDIO, "invalid sample rate");

    if (sample_rate == s_output_sample_rate) {
        return ESP_OK;
    }

    const i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_tx_handle, &clk_cfg),
                        TAG_AUDIO, "reconfig i2s clock failed");
    s_output_sample_rate = sample_rate;
    return ESP_OK;
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

    ESP_RETURN_ON_FALSE(info->data != NULL, ESP_ERR_INVALID_ARG, TAG_AUDIO, "wav data chunk missing");
    ESP_RETURN_ON_FALSE(info->audio_format == 1, ESP_ERR_NOT_SUPPORTED, TAG_AUDIO, "wav must be PCM");
    ESP_RETURN_ON_FALSE(info->channels == 1, ESP_ERR_NOT_SUPPORTED, TAG_AUDIO, "wav must be mono");
    ESP_RETURN_ON_FALSE(info->bits_per_sample == 16, ESP_ERR_NOT_SUPPORTED, TAG_AUDIO, "wav must be 16-bit");
    ESP_RETURN_ON_FALSE(info->sample_rate > 0, ESP_ERR_NOT_SUPPORTED, TAG_AUDIO, "wav sample rate missing");
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
            TAG_AUDIO, "write silence failed");
        written_frames += bytes_written / (2 * sizeof(int16_t));
    }

    return ESP_OK;
}

static esp_err_t play_wav_info_segment(const wav_info_t *wav, size_t sample_offset, size_t sample_count)
{
    const int16_t *mono = (const int16_t *)wav->data;
    int16_t frame_buf[AUDIO_PCM_CHUNK_SAMPLES * 2] = {0};

    ESP_RETURN_ON_FALSE(wav->sample_rate == s_output_sample_rate,
                        ESP_ERR_INVALID_STATE, TAG_AUDIO, "wav sample rate does not match configured i2s clock");
    ESP_RETURN_ON_FALSE((sample_offset + sample_count) <= (wav->size / sizeof(int16_t)),
                        ESP_ERR_INVALID_ARG, TAG_AUDIO, "wav sample range out of bounds");

    for (size_t played_samples = 0; played_samples < sample_count; ) {
        const size_t chunk_samples = min_size(AUDIO_PCM_CHUNK_SAMPLES, sample_count - played_samples);
        for (size_t i = 0; i < chunk_samples; i++) {
            const int16_t sample = apply_pcm_gain(mono[sample_offset + played_samples + i], AUDIO_SPEECH_GAIN_PCT);
            frame_buf[i * 2] = sample;
            frame_buf[i * 2 + 1] = sample;
        }

        size_t bytes_written = 0;
        ESP_RETURN_ON_ERROR(
            i2s_channel_write(s_tx_handle, frame_buf, chunk_samples * 2 * sizeof(int16_t),
                              &bytes_written, AUDIO_IO_TIMEOUT_MS),
            TAG_AUDIO, "write wav failed");
        played_samples += bytes_written / (2 * sizeof(int16_t));
    }

    return ESP_OK;
}

static esp_err_t play_wav_info(const wav_info_t *wav)
{
    return play_wav_info_segment(wav, 0, wav->size / sizeof(int16_t));
}

static esp_err_t play_wav_asset(const wav_asset_t *asset)
{
    wav_info_t wav = {0};
    const size_t wav_size = (size_t)(asset->end - asset->start);

    ESP_RETURN_ON_ERROR(parse_wav(asset->start, wav_size, &wav), TAG_AUDIO, "parse wav failed");
    return play_wav_info(&wav);
}

static const wav_asset_t *get_number_token_asset(int value)
{
    if (value < 0) {
        return NULL;
    }

    if (value < (int)(sizeof(s_number_token_assets) / sizeof(s_number_token_assets[0]))) {
        return &s_number_token_assets[value];
    }

    return NULL;
}
static weather_speech_t weather_speech_from_icon_code(const char *icon_code)
{
    int code = icon_code ? atoi(icon_code) : 0;

    if (code == 100 || code == 150) {
        return WEATHER_SPEECH_SUNNY;
    }
    if ((code >= 101 && code <= 103) || (code >= 151 && code <= 153)) {
        return WEATHER_SPEECH_CLOUDY;
    }
    if (code == 104 || code == 154) {
        return WEATHER_SPEECH_OVERCAST;
    }
    if (code == 312 || code == 313) {
        return WEATHER_SPEECH_RAIN_SNOW;
    }
    if (code >= 302 && code <= 304) {
        return WEATHER_SPEECH_THUNDER_RAIN;
    }
    if (code >= 300 && code <= 399) {
        return WEATHER_SPEECH_RAIN;
    }
    if (code >= 400 && code <= 499) {
        return WEATHER_SPEECH_SNOW;
    }
    if (code == 507 || code == 508) {
        return WEATHER_SPEECH_SAND_DUST;
    }
    if ((code >= 500 && code <= 515) || code == 502) {
        return WEATHER_SPEECH_HAZE;
    }

    return WEATHER_SPEECH_UNKNOWN;
}

static const wav_asset_t *get_weather_condition_asset(const char *icon_code)
{
    const weather_speech_t speech = weather_speech_from_icon_code(icon_code);
    return &s_weather_condition_assets[speech];
}

static const char *weather_summary_icon_code(const weather_data_t *weather)
{
    if (weather == NULL) {
        return NULL;
    }

    if (weather->forecast_valid && weather->forecast[0].icon_day[0]) {
        return weather->forecast[0].icon_day;
    }

    if (weather->now_valid && weather->icon_code[0]) {
        return weather->icon_code;
    }

    return NULL;
}

static esp_err_t play_segment_gap(void)
{
    return write_silence_ms(AUDIO_SEGMENT_GAP_MS);
}

static esp_err_t play_number_value(int value, bool leading_zero)
{
    const wav_asset_t *asset = NULL;
    esp_err_t ret;

    ESP_RETURN_ON_FALSE(value >= 0 && value < 100, ESP_ERR_INVALID_ARG, TAG_AUDIO, "unsupported number");

    if (leading_zero && value < 10) {
        asset = get_number_token_asset(0);
        ESP_RETURN_ON_FALSE(asset != NULL, ESP_ERR_INVALID_STATE, TAG_AUDIO, "missing zero asset");
        ret = play_wav_asset(asset);
        if (ret != ESP_OK) {
            return ret;
        }

        ret = play_segment_gap();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (value <= 10) {
        asset = get_number_token_asset(value);
        ESP_RETURN_ON_FALSE(asset != NULL, ESP_ERR_INVALID_STATE, TAG_AUDIO, "missing number asset");
        return play_wav_asset(asset);
    }

    if (value < 20) {
        ret = play_wav_asset(get_number_token_asset(10));
        if (ret != ESP_OK) {
            return ret;
        }

        if ((value % 10) == 0) {
            return ESP_OK;
        }

        ret = play_segment_gap();
        if (ret != ESP_OK) {
            return ret;
        }

        return play_wav_asset(get_number_token_asset(value % 10));
    }

    ret = play_wav_asset(get_number_token_asset(value / 10));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = play_segment_gap();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = play_wav_asset(get_number_token_asset(10));
    if (ret != ESP_OK) {
        return ret;
    }

    if ((value % 10) == 0) {
        return ESP_OK;
    }

    ret = play_segment_gap();
    if (ret != ESP_OK) {
        return ret;
    }

    return play_wav_asset(get_number_token_asset(value % 10));
}

static esp_err_t play_temperature_value(int value)
{
    const wav_asset_t below_zero_asset = WAV_ASSET(prefix_below_zero);
    esp_err_t ret;
    int abs_value = value;

    if (value < 0) {
        abs_value = -value;
        ret = play_wav_asset(&below_zero_asset);
        if (ret != ESP_OK) {
            return ret;
        }

        ret = play_segment_gap();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    return play_number_value(abs_value, false);
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

static esp_err_t audio_output_announce_time_locked(int hour, int minute, bool include_minute)
{
    const wav_asset_t prefix_asset = WAV_ASSET(prefix_beijing_time);
    const wav_asset_t point_asset = WAV_ASSET(suffix_point);
    const wav_asset_t exact_asset = WAV_ASSET(suffix_exact);
    bool tx_enabled = false;
    esp_err_t ret;

    ESP_RETURN_ON_FALSE(hour >= 0 && hour < 24, ESP_ERR_INVALID_ARG, TAG_AUDIO, "invalid hour");
    ESP_RETURN_ON_FALSE(minute >= 0 && minute < 60, ESP_ERR_INVALID_ARG, TAG_AUDIO, "invalid minute");

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

    ret = play_number_value(hour, false);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = write_silence_ms(AUDIO_SEGMENT_GAP_MS);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = play_wav_asset(&point_asset);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    if (!include_minute || minute == 0) {
        ret = write_silence_ms(AUDIO_SEGMENT_GAP_MS);
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_wav_asset(&exact_asset);
        if (ret != ESP_OK) {
            goto cleanup;
        }
    } else if (include_minute) {
        ret = write_silence_ms(AUDIO_SEGMENT_GAP_MS);
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_number_value(minute, minute < 10);
        if (ret != ESP_OK) {
            goto cleanup;
        }
    }

    ret = write_silence_ms(AUDIO_SILENCE_TAIL_MS);

cleanup:
    return audio_output_finish_speech_locked(tx_enabled, ret);
}

static esp_err_t audio_output_announce_weather_locked(const weather_data_t *weather)
{
    const wav_asset_t today_weather_asset = WAV_ASSET(phrase_today_weather);
    const wav_asset_t current_temperature_asset = WAV_ASSET(phrase_current_temperature);
    const wav_asset_t weather_not_ready_asset = WAV_ASSET(phrase_weather_not_ready);
    const wav_asset_t to_asset = WAV_ASSET(conjunction_to);
    const wav_asset_t degree_asset = WAV_ASSET(suffix_degree);
    const wav_asset_t *condition_asset = NULL;
    const char *condition_code = NULL;
    bool tx_enabled = false;
    esp_err_t ret;

    ESP_RETURN_ON_FALSE(weather != NULL, ESP_ERR_INVALID_ARG, TAG_AUDIO, "weather is null");

    ret = audio_output_begin_speech_locked(&tx_enabled);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    if (!weather->now_valid && !weather->forecast_valid) {
        ret = play_wav_asset(&weather_not_ready_asset);
        if (ret == ESP_OK) {
            ret = write_silence_ms(AUDIO_SILENCE_TAIL_MS);
        }
        goto cleanup;
    }

    condition_code = weather_summary_icon_code(weather);
    condition_asset = get_weather_condition_asset(condition_code);

    ret = play_wav_asset(&today_weather_asset);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = play_segment_gap();
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = play_wav_asset(condition_asset);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    if (weather->forecast_valid) {
        ret = play_segment_gap();
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_temperature_value(weather->temp_low);
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_segment_gap();
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_wav_asset(&to_asset);
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_segment_gap();
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_temperature_value(weather->temp_high);
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_segment_gap();
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_wav_asset(&degree_asset);
        if (ret != ESP_OK) {
            goto cleanup;
        }
    } else if (weather->now_valid) {
        ret = play_segment_gap();
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_wav_asset(&current_temperature_asset);
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_segment_gap();
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_temperature_value(weather->temp_now);
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_segment_gap();
        if (ret != ESP_OK) {
            goto cleanup;
        }

        ret = play_wav_asset(&degree_asset);
        if (ret != ESP_OK) {
            goto cleanup;
        }
    }

    ret = write_silence_ms(AUDIO_SILENCE_TAIL_MS);

cleanup:
    return audio_output_finish_speech_locked(tx_enabled, ret);
}

esp_err_t audio_output_init(void)
{
    audio_output_ensure_mutex();

    if (s_audio_ready) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = AUDIO_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = AUDIO_DMA_FRAME_NUM;
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_handle, NULL), TAG_AUDIO, "alloc i2s tx failed");

    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(s_output_sample_rate),
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
    ESP_LOGI(TAG_AUDIO, "MAX98357A I2S ready: BCLK=%d WS=%d DIN=%d rate=%lu",
             AUDIO_I2S_BCLK_PIN, AUDIO_I2S_WS_PIN, AUDIO_I2S_DOUT_PIN,
             (unsigned long)s_output_sample_rate);
    return ESP_OK;
}

esp_err_t audio_output_deinit(void)
{
    if (s_tx_handle != NULL) {
        const esp_err_t ret = i2s_del_channel(s_tx_handle);
        if (ret != ESP_OK) {
            return ret;
        }
        s_tx_handle = NULL;
    }

    s_audio_ready = false;
    s_output_sample_rate = AUDIO_I2S_SAMPLE_RATE_HZ;
    return ESP_OK;
}

bool audio_output_is_ready(void)
{
    return s_audio_ready;
}

esp_err_t audio_output_play_tone(uint32_t freq_hz, uint32_t duration_ms, uint8_t volume_pct)
{
    esp_err_t ret;

    ESP_RETURN_ON_FALSE(s_audio_ready && s_tx_handle != NULL, ESP_ERR_INVALID_STATE, TAG_AUDIO, "audio not ready");
    ESP_RETURN_ON_FALSE(freq_hz > 0, ESP_ERR_INVALID_ARG, TAG_AUDIO, "invalid frequency");
    ESP_RETURN_ON_FALSE(duration_ms > 0, ESP_ERR_INVALID_ARG, TAG_AUDIO, "invalid duration");

    if (volume_pct > 100) {
        volume_pct = 100;
    }

    ret = audio_output_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = audio_output_set_sample_rate(AUDIO_I2S_SAMPLE_RATE_HZ);
    if (ret == ESP_OK) {
        const uint32_t total_frames = (AUDIO_I2S_SAMPLE_RATE_HZ * duration_ms) / 1000;
        const uint32_t tail_frames = (AUDIO_I2S_SAMPLE_RATE_HZ * AUDIO_SILENCE_TAIL_MS) / 1000;
        uint32_t half_period_frames = AUDIO_I2S_SAMPLE_RATE_HZ / (freq_hz * 2);
        const int16_t amplitude = (int16_t)((32767 * volume_pct) / 100 / 2);
        int16_t frame_buf[AUDIO_TONE_CHUNK_FRAMES * 2] = {0};
        uint32_t phase_frames = 0;
        bool tx_enabled = false;

        if (half_period_frames == 0) {
            half_period_frames = 1;
        }

        ret = audio_output_enable_tx();
        if (ret == ESP_OK) {
            tx_enabled = true;

            for (uint32_t written_frames = 0; ret == ESP_OK && written_frames < total_frames; ) {
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
                ret = i2s_channel_write(s_tx_handle, frame_buf, chunk_frames * 2 * sizeof(int16_t),
                                        &bytes_written, AUDIO_IO_TIMEOUT_MS);
                if (ret == ESP_OK) {
                    written_frames += bytes_written / (2 * sizeof(int16_t));
                }
            }

            for (uint32_t flushed_frames = 0; ret == ESP_OK && flushed_frames < tail_frames; ) {
                const size_t chunk_frames = min_size(AUDIO_TONE_CHUNK_FRAMES, tail_frames - flushed_frames);
                memset(frame_buf, 0, chunk_frames * 2 * sizeof(int16_t));

                size_t bytes_written = 0;
                ret = i2s_channel_write(s_tx_handle, frame_buf, chunk_frames * 2 * sizeof(int16_t),
                                        &bytes_written, AUDIO_IO_TIMEOUT_MS);
                if (ret == ESP_OK) {
                    flushed_frames += bytes_written / (2 * sizeof(int16_t));
                }
            }
        }

        if (tx_enabled) {
            const esp_err_t disable_ret = audio_output_disable_tx();
            if (ret == ESP_OK && disable_ret != ESP_OK) {
                ret = disable_ret;
            }
        }
    }

    audio_output_unlock();
    return ret;
}

esp_err_t audio_output_play_test_tone(void)
{
    return audio_output_play_tone(AUDIO_TONE_FREQ_HZ, AUDIO_TONE_DURATION_MS, AUDIO_TONE_VOLUME_PCT);
}

esp_err_t audio_output_play_beijing_hour_announcement(int hour)
{
    esp_err_t ret = audio_output_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = audio_output_announce_time_locked(hour, 0, false);
    audio_output_unlock();
    return ret;
}

esp_err_t audio_output_play_beijing_time_announcement(int hour, int minute)
{
    esp_err_t ret = audio_output_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = audio_output_announce_time_locked(hour, minute, true);
    audio_output_unlock();
    return ret;
}

esp_err_t audio_output_play_weather_announcement(const weather_data_t *weather)
{
    esp_err_t ret = audio_output_lock();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = audio_output_announce_weather_locked(weather);
    audio_output_unlock();
    return ret;
}
