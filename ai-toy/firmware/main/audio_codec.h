#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * @brief 音频数据包结构
 */
typedef struct {
    uint8_t *data;      // 音频数据指针
    size_t  size;       // 数据大小 (字节)
    uint32_t timestamp; // 时间戳 (ms)
} audio_packet_t;

/**
 * @brief 音频编解码器回调函数类型
 * @param packet 音频数据包
 */
typedef void (*audio_capture_cb_t)(const audio_packet_t *packet);

/**
 * @brief 初始化音频编解码器
 * @param capture_queue 音频采集数据队列句柄 (可为NULL，使用回调方式)
 * @return ESP_OK 成功
 */
esp_err_t audio_codec_init(QueueHandle_t capture_queue);

/**
 * @brief 开始音频采集
 * @return ESP_OK 成功
 */
esp_err_t audio_codec_start_capture(void);

/**
 * @brief 停止音频采集
 * @return ESP_OK 成功
 */
esp_err_t audio_codec_stop_capture(void);

/**
 * @brief 播放音频数据
 * @param data PCM音频数据
 * @param size 数据大小 (字节)
 * @return ESP_OK 成功
 */
esp_err_t audio_codec_play(const uint8_t *data, size_t size);

/**
 * @brief 停止播放
 */
void audio_codec_stop_play(void);

/**
 * @brief 检查是否正在播放
 * @return true 正在播放
 */
bool audio_codec_is_playing(void);

/**
 * @brief 设置播放音量
 * @param volume 音量 (0-100)
 */
void audio_codec_set_volume(uint8_t volume);

/**
 * @brief 获取当前音频电平 (用于VAD)
 * @return 音频RMS电平 (0-32767)
 */
int16_t audio_codec_get_level(void);

/**
 * @brief 反初始化音频编解码器
 */
void audio_codec_deinit(void);