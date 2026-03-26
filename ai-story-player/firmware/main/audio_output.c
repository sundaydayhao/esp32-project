/**
 * 音频输出模块
 * MAX98357A I2S 数字功放驱动 + MP3 解码
 *
 * 注意：MP3 解码使用 ESP-ADF 的 mp3_decoder 组件
 * 或者使用轻量级的 libhelix-mp3 解码器
 */

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#include "config.h"
#include "audio_output.h"

static const char *TAG = "AUDIO_OUT";

static i2s_chan_handle_t s_tx_chan = NULL;
static bool s_is_playing = false;
static uint8_t s_volume = 80;  // 默认音量 80%
static SemaphoreHandle_t s_play_mutex = NULL;

// MP3 解码输出缓冲区
#define PCM_BUF_SIZE    (4096)
static int16_t s_pcm_buf[PCM_BUF_SIZE];

esp_err_t audio_output_init(void) {
    s_play_mutex = xSemaphoreCreateMutex();
    if (!s_play_mutex) {
        ESP_LOGE(TAG, "创建互斥锁失败");
        return ESP_ERR_NO_MEM;
    }

    // 配置 I2S 发送通道
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        SPK_I2S_PORT, I2S_ROLE_MASTER
    );
    chan_cfg.auto_clear = true;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL));

    // 配置标准 I2S 模式
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SPK_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_MONO
        ),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_BCLK_PIN,
            .ws   = SPK_LRC_PIN,
            .dout = SPK_DIN_PIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));

    ESP_LOGI(TAG, "扬声器初始化完成，采样率: %d Hz", SPK_SAMPLE_RATE);
    return ESP_OK;
}

/**
 * 应用音量调节
 */
static void apply_volume(int16_t *samples, size_t count, uint8_t volume) {
    if (volume >= 100) return;
    float scale = (float)volume / 100.0f;
    for (size_t i = 0; i < count; i++) {
        samples[i] = (int16_t)((float)samples[i] * scale);
    }
}

esp_err_t audio_output_play_pcm(const int16_t *pcm_data, size_t pcm_len,
                                  uint32_t sample_rate) {
    if (!pcm_data || pcm_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_play_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_is_playing = true;

    // 如果采样率不同，重新配置 I2S
    if (sample_rate != SPK_SAMPLE_RATE) {
        i2s_channel_disable(s_tx_chan);
        i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
        i2s_channel_reconfig_std_clock(s_tx_chan, &clk_cfg);
        i2s_channel_enable(s_tx_chan);
    }

    // 分块写入，应用音量
    size_t samples_total = pcm_len / sizeof(int16_t);
    size_t offset = 0;

    while (offset < samples_total && s_is_playing) {
        size_t chunk = (samples_total - offset < PCM_BUF_SIZE) ?
                       (samples_total - offset) : PCM_BUF_SIZE;

        memcpy(s_pcm_buf, pcm_data + offset, chunk * sizeof(int16_t));
        apply_volume(s_pcm_buf, chunk, s_volume);

        size_t bytes_written = 0;
        i2s_channel_write(s_tx_chan, s_pcm_buf, chunk * sizeof(int16_t),
                          &bytes_written, pdMS_TO_TICKS(1000));
        offset += chunk;
    }

    s_is_playing = false;
    xSemaphoreGive(s_play_mutex);
    return ESP_OK;
}

/**
 * 简单的 MP3 帧头解析（用于跳过 ID3 标签）
 */
static size_t skip_id3_tag(const uint8_t *data, size_t len) {
    if (len < 10) return 0;
    // 检查 ID3 标签
    if (data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
        uint32_t tag_size = ((data[6] & 0x7F) << 21) |
                            ((data[7] & 0x7F) << 14) |
                            ((data[8] & 0x7F) << 7)  |
                            (data[9] & 0x7F);
        tag_size += 10;  // 加上头部大小
        return (tag_size < len) ? tag_size : len;
    }
    return 0;
}

esp_err_t audio_output_play_mp3(const uint8_t *mp3_data, size_t mp3_len) {
    if (!mp3_data || mp3_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "播放 MP3，大小: %d bytes", mp3_len);

    /**
     * 注意：完整的 MP3 解码需要集成解码库
     * 推荐方案：
     * 1. 使用 ESP-ADF (Audio Development Framework)
     *    - 包含完整的 MP3 解码器
     *    - https://github.com/espressif/esp-adf
     *
     * 2. 使用 libhelix-mp3（轻量级）
     *    - 集成到 components/ 目录
     *
     * 3. 后端直接发送 PCM 数据（推荐简化方案）
     *    - 后端将 MP3 转换为 PCM 后发送
     *    - ESP32-S3 直接播放 PCM
     *
     * 以下为使用 ESP-ADF 的伪代码示例：
     */

    // 跳过 ID3 标签
    size_t offset = skip_id3_tag(mp3_data, mp3_len);
    const uint8_t *mp3_start = mp3_data + offset;
    size_t mp3_actual_len = mp3_len - offset;

    // TODO: 集成 MP3 解码器
    // 示例（使用 helix MP3 解码器）：
    /*
    HMP3Decoder decoder = MP3InitDecoder();
    if (!decoder) return ESP_ERR_NO_MEM;

    unsigned char *read_ptr = (unsigned char *)mp3_start;
    int bytes_left = mp3_actual_len;

    while (bytes_left > 0 && s_is_playing) {
        int offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (offset < 0) break;

        read_ptr += offset;
        bytes_left -= offset;

        MP3FrameInfo frame_info;
        int err = MP3Decode(decoder, &read_ptr, &bytes_left,
                            s_pcm_buf, 0);
        if (err != ERR_MP3_NONE) break;

        MP3GetLastFrameInfo(decoder, &frame_info);
        size_t pcm_bytes = frame_info.outputSamps * sizeof(int16_t);
        apply_volume(s_pcm_buf, frame_info.outputSamps, s_volume);

        size_t bytes_written = 0;
        i2s_channel_write(s_tx_chan, s_pcm_buf, pcm_bytes,
                          &bytes_written, pdMS_TO_TICKS(1000));
    }

    MP3FreeDecoder(decoder);
    */

    // 临时方案：直接将 MP3 数据写入（不解码，仅用于测试）
    // 实际使用时需要替换为真正的 MP3 解码
    ESP_LOGW(TAG, "MP3 解码器未集成，请参考注释集成 ESP-ADF 或 libhelix-mp3");

    return ESP_OK;
}

void audio_output_stop(void) {
    s_is_playing = false;
    ESP_LOGI(TAG, "停止播放");
}

bool audio_output_is_playing(void) {
    return s_is_playing;
}

void audio_output_set_volume(uint8_t volume) {
    s_volume = (volume > 100) ? 100 : volume;
    ESP_LOGI(TAG, "音量设置为: %d%%", s_volume);
}

void audio_output_deinit(void) {
    audio_output_stop();
    if (s_tx_chan) {
        i2s_channel_disable(s_tx_chan);
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
    }
    if (s_play_mutex) {
        vSemaphoreDelete(s_play_mutex);
        s_play_mutex = NULL;
    }
}