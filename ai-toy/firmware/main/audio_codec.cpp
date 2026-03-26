/**
 * @file audio_codec.cpp
 * @brief I2S音频编解码器驱动
 * 
 * 麦克风: INMP441 (I2S数字麦克风)
 * 扬声器: MAX98357A (I2S数字功放)
 */

#include "audio_codec.h"
#include "app_config.h"

#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <string.h>
#include <math.h>

static const char *TAG = "AUDIO";

// I2S 通道句柄
static i2s_chan_handle_t s_mic_chan = NULL;
static i2s_chan_handle_t s_spk_chan = NULL;

// 任务句柄
static TaskHandle_t s_capture_task_handle = NULL;
static TaskHandle_t s_play_task_handle = NULL;

// 状态标志
static volatile bool s_capturing = false;
static volatile bool s_playing = false;
static volatile uint8_t s_volume = 80;  // 默认音量80%

// 音频队列
static QueueHandle_t s_capture_queue = NULL;
static QueueHandle_t s_play_queue = NULL;

// 当前音频电平
static volatile int16_t s_audio_level = 0;

// 播放队列大小
#define PLAY_QUEUE_SIZE     30

/**
 * @brief 计算音频RMS电平
 */
static int16_t calculate_rms(const int16_t *samples, size_t count)
{
    if (count == 0) return 0;

    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += (int64_t)samples[i] * samples[i];
    }
    return (int16_t)sqrt((double)sum / count);
}

/**
 * @brief 应用音量增益
 */
static void apply_volume(int16_t *samples, size_t count, uint8_t volume)
{
    if (volume == 100) return;

    float gain = volume / 100.0f;
    for (size_t i = 0; i < count; i++) {
        int32_t val = (int32_t)(samples[i] * gain);
        if (val > 32767)  val = 32767;
        if (val < -32768) val = -32768;
        samples[i] = (int16_t)val;
    }
}

/**
 * @brief 音频采集任务
 */
static void audio_capture_task(void *arg)
{
    ESP_LOGI(TAG, "音频采集任务启动");

    size_t buf_size = AUDIO_FRAME_BYTES;
    uint8_t *buf = (uint8_t *)malloc(buf_size);
    if (!buf) {
        ESP_LOGE(TAG, "分配采集缓冲区失败");
        vTaskDelete(NULL);
        return;
    }

    size_t bytes_read = 0;

    while (1) {
        if (!s_capturing) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 从I2S读取音频数据
        esp_err_t ret = i2s_channel_read(s_mic_chan, buf, buf_size, &bytes_read, pdMS_TO_TICKS(100));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "I2S读取失败: %s", esp_err_to_name(ret));
            continue;
        }

        if (bytes_read == 0) continue;

        // 计算音频电平 (用于VAD)
        int16_t *samples = (int16_t *)buf;
        size_t sample_count = bytes_read / sizeof(int16_t);
        s_audio_level = calculate_rms(samples, sample_count);

#if DEBUG_AUDIO_LEVEL
        if (s_audio_level > 500) {
            ESP_LOGD(TAG, "音频电平: %d", s_audio_level);
        }
#endif

        // 发送到采集队列
        if (s_capture_queue) {
            audio_packet_t packet = {
                .data = (uint8_t *)malloc(bytes_read),
                .size = bytes_read,
                .timestamp = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
            };

            if (packet.data) {
                memcpy(packet.data, buf, bytes_read);
                if (xQueueSend(s_capture_queue, &packet, 0) != pdTRUE) {
                    // 队列满，丢弃数据
                    free(packet.data);
                    ESP_LOGD(TAG, "采集队列满，丢弃数据");
                }
            }
        }
    }

    free(buf);
    vTaskDelete(NULL);
}

/**
 * @brief 音频播放任务
 */
static void audio_play_task(void *arg)
{
    ESP_LOGI(TAG, "音频播放任务启动");

    audio_packet_t packet;

    while (1) {
        if (xQueueReceive(s_play_queue, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_playing = true;

            // 应用音量
            int16_t *samples = (int16_t *)packet.data;
            size_t sample_count = packet.size / sizeof(int16_t);
            apply_volume(samples, sample_count, s_volume);

            // 写入I2S播放
            size_t bytes_written = 0;
            esp_err_t ret = i2s_channel_write(s_spk_chan, packet.data, packet.size,
                                               &bytes_written, pdMS_TO_TICKS(1000));
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "I2S写入失败: %s", esp_err_to_name(ret));
            }

            free(packet.data);

            if (uxQueueMessagesWaiting(s_play_queue) == 0) {
                s_playing = false;
            }
        } else {
            s_playing = false;
        }
    }
}

// ============================================================
// 公共接口实现
// ============================================================

esp_err_t audio_codec_init(QueueHandle_t capture_queue)
{
    ESP_LOGI(TAG, "初始化音频编解码器");

    s_capture_queue = capture_queue;

    // ---- 初始化麦克风 I2S (RX) ----
    i2s_chan_config_t mic_chan_cfg = {
        .id = (i2s_port_t)MIC_I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = MIC_DMA_BUF_COUNT,
        .dma_frame_num = MIC_DMA_BUF_LEN,
        .auto_clear = false,
    };

    esp_err_t ret = i2s_new_channel(&mic_chan_cfg, NULL, &s_mic_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建麦克风I2S通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t mic_std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = MIC_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = (i2s_data_bit_width_t)MIC_BITS_PER_SAMPLE,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = MIC_BITS_PER_SAMPLE,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)MIC_I2S_SCK_PIN,
            .ws   = (gpio_num_t)MIC_I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)MIC_I2S_SD_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_mic_chan, &mic_std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化麦克风I2S标准模式失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // ---- 初始化扬声器 I2S (TX) ----
    i2s_chan_config_t spk_chan_cfg = {
        .id = (i2s_port_t)SPK_I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = SPK_DMA_BUF_COUNT,
        .dma_frame_num = SPK_DMA_BUF_LEN,
        .auto_clear = true,
    };

    ret = i2s_new_channel(&spk_chan_cfg, &s_spk_chan, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建扬声器I2S通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t spk_std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = SPK_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = (i2s_data_bit_width_t)SPK_BITS_PER_SAMPLE,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = SPK_BITS_PER_SAMPLE,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)SPK_I2S_BCK_PIN,
            .ws   = (gpio_num_t)SPK_I2S_WS_PIN,
            .dout = (gpio_num_t)SPK_I2S_DATA_PIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_spk_chan, &spk_std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化扬声器I2S标准模式失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 使能I2S通道
    ret = i2s_channel_enable(s_mic_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "使能麦克风I2S通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(s_spk_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "使能扬声器I2S通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 创建播放队列
    s_play_queue = xQueueCreate(PLAY_QUEUE_SIZE, sizeof(audio_packet_t));
    if (!s_play_queue) {
        ESP_LOGE(TAG, "创建播放队列失败");
        return ESP_ERR_NO_MEM;
    }

    // 创建采集任务
    xTaskCreatePinnedToCore(audio_capture_task, "audio_cap", TASK_STACK_AUDIO_CAPTURE,
                             NULL, TASK_PRIORITY_AUDIO_CAPTURE, &s_capture_task_handle, 1);

    // 创建播放任务
    xTaskCreatePinnedToCore(audio_play_task, "audio_play", TASK_STACK_AUDIO_PLAY,
                             NULL, TASK_PRIORITY_AUDIO_PLAY, &s_play_task_handle, 1);

    ESP_LOGI(TAG, "音频编解码器初始化完成");
    ESP_LOGI(TAG, "  麦克风: I2S%d, SCK=%d, WS=%d, SD=%d, %dHz",
             MIC_I2S_PORT, MIC_I2S_SCK_PIN, MIC_I2S_WS_PIN, MIC_I2S_SD_PIN, MIC_SAMPLE_RATE);
    ESP_LOGI(TAG, "  扬声器: I2S%d, BCK=%d, WS=%d, DATA=%d, %dHz",
             SPK_I2S_PORT, SPK_I2S_BCK_PIN, SPK_I2S_WS_PIN, SPK_I2S_DATA_PIN, SPK_SAMPLE_RATE);

    return ESP_OK;
}

esp_err_t audio_codec_start_capture(void)
{
    ESP_LOGI(TAG, "开始音频采集");
    s_capturing = true;
    return ESP_OK;
}

esp_err_t audio_codec_stop_capture(void)
{
    ESP_LOGI(TAG, "停止音频采集");
    s_capturing = false;
    return ESP_OK;
}

esp_err_t audio_codec_play(const uint8_t *data, size_t size)
{
    if (!data || size == 0) return ESP_ERR_INVALID_ARG;
    if (!s_play_queue) return ESP_ERR_INVALID_STATE;

    audio_packet_t packet = {
        .data = (uint8_t *)malloc(size),
        .size = size,
        .timestamp = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
    };

    if (!packet.data) {
        ESP_LOGE(TAG, "分配播放缓冲区失败");
        return ESP_ERR_NO_MEM;
    }

    memcpy(packet.data, data, size);

    if (xQueueSend(s_play_queue, &packet, pdMS_TO_TICKS(500)) != pdTRUE) {
        free(packet.data);
        ESP_LOGW(TAG, "播放队列满");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

void audio_codec_stop_play(void)
{
    // 清空播放队列
    if (s_play_queue) {
        audio_packet_t packet;
        while (xQueueReceive(s_play_queue, &packet, 0) == pdTRUE) {
            free(packet.data);
        }
    }
    s_playing = false;
    ESP_LOGI(TAG, "停止播放");
}

bool audio_codec_is_playing(void)
{
    return s_playing;
}

void audio_codec_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    s_volume = volume;
    ESP_LOGI(TAG, "设置音量: %d%%", volume);
}

int16_t audio_codec_get_level(void)
{
    return s_audio_level;
}

void audio_codec_deinit(void)
{
    s_capturing = false;
    s_playing = false;

    if (s_capture_task_handle) {
        vTaskDelete(s_capture_task_handle);
        s_capture_task_handle = NULL;
    }
    if (s_play_task_handle) {
        vTaskDelete(s_play_task_handle);
        s_play_task_handle = NULL;
    }
    if (s_play_queue) {
        audio_packet_t packet;
        while (xQueueReceive(s_play_queue, &packet, 0) == pdTRUE) {
            free(packet.data);
        }
        vQueueDelete(s_play_queue);
        s_play_queue = NULL;
    }
    if (s_mic_chan) {
        i2s_channel_disable(s_mic_chan);
        i2s_del_channel(s_mic_chan);
        s_mic_chan = NULL;
    }
    if (s_spk_chan) {
        i2s_channel_disable(s_spk_chan);
        i2s_del_channel(s_spk_chan);
        s_spk_chan = NULL;
    }
    ESP_LOGI(TAG, "音频编解码器已释放");
}