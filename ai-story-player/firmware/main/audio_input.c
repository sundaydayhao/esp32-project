/**
 * 音频输入模块
 * INMP441 I2S 数字麦克风驱动
 */

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#include "config.h"
#include "audio_input.h"

static const char *TAG = "AUDIO_IN";

static i2s_chan_handle_t s_rx_chan = NULL;
static bool s_is_recording = false;

#define FRAME_SAMPLES   MIC_BUFFER_SIZE
static int32_t s_raw_buf[FRAME_SAMPLES];

esp_err_t audio_input_init(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        MIC_I2S_PORT, I2S_ROLE_MASTER
    );
    chan_cfg.auto_clear = true;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT,
            I2S_SLOT_MODE_MONO
        ),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_SCK_PIN,
            .ws   = MIC_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = MIC_SD_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_chan));

    ESP_LOGI(TAG, "麦克风初始化完成，采样率: %d Hz", MIC_SAMPLE_RATE);
    return ESP_OK;
}

void audio_input_start(void) {
    s_is_recording = true;
    ESP_LOGI(TAG, "开始录音");
}

void audio_input_stop(void) {
    s_is_recording = false;
    ESP_LOGI(TAG, "停止录音");
}

size_t audio_input_read_frame(int16_t *buffer, size_t samples) {
    size_t bytes_read = 0;

    esp_err_t ret = i2s_channel_read(
        s_rx_chan,
        s_raw_buf,
        samples * sizeof(int32_t),
        &bytes_read,
        pdMS_TO_TICKS(100)
    );

    if (ret != ESP_OK) {
        return 0;
    }

    size_t samples_read = bytes_read / sizeof(int32_t);

    // INMP441: 有效数据在高 24 位，取高 16 位
    for (size_t i = 0; i < samples_read; i++) {
        buffer[i] = (int16_t)(s_raw_buf[i] >> 16);
    }

    return samples_read;
}

float audio_calc_rms(const int16_t *samples, size_t count) {
    if (count == 0) return 0.0f;

    float sum = 0.0f;
    for (size_t i = 0; i < count; i++) {
        float s = (float)samples[i];
        sum += s * s;
    }
    return sqrtf(sum / (float)count);
}

bool audio_input_record(uint8_t **out_buffer, size_t *out_size,
                         uint32_t max_ms, float silence_threshold,
                         uint32_t silence_ms) {
    if (!out_buffer || !out_size) {
        return false;
    }

    size_t max_samples = (MIC_SAMPLE_RATE * max_ms) / 1000;
    size_t max_bytes = max_samples * sizeof(int16_t);

    int16_t *record_buf = (int16_t *)malloc(max_bytes);
    if (!record_buf) {
        ESP_LOGE(TAG, "分配录音缓冲区失败，大小: %d bytes", max_bytes);
        return false;
    }

    size_t total_samples = 0;
    uint32_t silence_count = 0;
    // 每帧 20ms 的样本数
    size_t frame_samples = (MIC_SAMPLE_RATE * 20) / 1000;
    uint32_t silence_frames_threshold = silence_ms / 20;
    bool has_voice = false;

    int16_t *frame_buf = (int16_t *)malloc(frame_samples * sizeof(int16_t));
    if (!frame_buf) {
        free(record_buf);
        return false;
    }

    ESP_LOGI(TAG, "录音开始，最大时长: %d ms，静音阈值: %.1f", max_ms, silence_threshold);

    while (total_samples < max_samples && s_is_recording) {
        size_t remaining = max_samples - total_samples;
        size_t to_read = (remaining < frame_samples) ? remaining : frame_samples;

        size_t read = audio_input_read_frame(frame_buf, to_read);
        if (read == 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        // 复制到录音缓冲区
        memcpy(record_buf + total_samples, frame_buf, read * sizeof(int16_t));
        total_samples += read;

        // 计算 RMS 能量
        float rms = audio_calc_rms(frame_buf, read);

        if (rms > silence_threshold) {
            has_voice = true;
            silence_count = 0;
        } else {
            silence_count++;
        }

        // 检测到足够长的静音且已有语音，停止录音
        if (has_voice && silence_count >= silence_frames_threshold) {
            ESP_LOGI(TAG, "检测到静音，停止录音");
            break;
        }
    }

    free(frame_buf);

    if (total_samples == 0 || !has_voice) {
        ESP_LOGW(TAG, "未检测到语音");
        free(record_buf);
        *out_buffer = NULL;
        *out_size = 0;
        return false;
    }

    *out_buffer = (uint8_t *)record_buf;
    *out_size = total_samples * sizeof(int16_t);

    ESP_LOGI(TAG, "录音完成，%d 样本，%.1f 秒",
             total_samples, (float)total_samples / MIC_SAMPLE_RATE);
    return true;
}

void audio_input_deinit(void) {
    if (s_rx_chan) {
        i2s_channel_disable(s_rx_chan);
        i2s_del_channel(s_rx_chan);
        s_rx_chan = NULL;
    }
}