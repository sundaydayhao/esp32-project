#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * 初始化音频输出（MAX98357A I2S 功放）
 */
esp_err_t audio_output_init(void);

/**
 * 播放 MP3 音频数据
 * @param mp3_data  MP3 数据指针
 * @param mp3_len   MP3 数据长度
 */
esp_err_t audio_output_play_mp3(const uint8_t *mp3_data, size_t mp3_len);

/**
 * 播放 PCM 音频数据
 * @param pcm_data      PCM 数据（16-bit signed）
 * @param pcm_len       数据长度（字节）
 * @param sample_rate   采样率
 */
esp_err_t audio_output_play_pcm(const int16_t *pcm_data, size_t pcm_len,
                                  uint32_t sample_rate);

/**
 * 停止播放
 */
void audio_output_stop(void);

/**
 * 检查是否正在播放
 */
bool audio_output_is_playing(void);

/**
 * 设置音量 (0-100)
 */
void audio_output_set_volume(uint8_t volume);

/**
 * 反初始化
 */
void audio_output_deinit(void);