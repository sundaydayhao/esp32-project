#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * 初始化音频输入（INMP441 I2S 麦克风）
 */
esp_err_t audio_input_init(void);

/**
 * 开始录音
 */
void audio_input_start(void);

/**
 * 停止录音
 */
void audio_input_stop(void);

/**
 * 录制音频（带静音检测）
 * 
 * @param out_buffer    输出缓冲区指针（调用者负责释放）
 * @param out_size      输出数据大小
 * @param max_ms        最大录音时长（毫秒）
 * @param silence_threshold  静音阈值（RMS）
 * @param silence_ms    静音持续时间触发停止（毫秒）
 * @return true 成功，false 失败
 */
bool audio_input_record(uint8_t **out_buffer, size_t *out_size,
                         uint32_t max_ms, float silence_threshold,
                         uint32_t silence_ms);

/**
 * 读取一帧音频数据
 */
size_t audio_input_read_frame(int16_t *buffer, size_t samples);

/**
 * 计算音频 RMS 能量
 */
float audio_calc_rms(const int16_t *samples, size_t count);

/**
 * 反初始化
 */
void audio_input_deinit(void);