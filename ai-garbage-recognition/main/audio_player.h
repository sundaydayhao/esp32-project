/**
 * audio_player.h
 * 音频播放模块接口 / Audio Player Interface
 *
 * 使用 DFPlayer Mini MP3 模块通过 UART 播放语音提示
 *
 * SD 卡目录结构:
 *   /01/001.mp3  - 系统就绪
 *   /01/002.mp3  - 有害垃圾，请注意安全处理
 *   /01/003.mp3  - 可回收物，感谢您的环保行动
 *   /01/004.mp3  - 厨余垃圾，请妥善处理
 *   /01/005.mp3  - 其他垃圾
 *   /01/006.mp3  - 无法识别，请重新放置
 *   /01/007.mp3  - 系统错误
 */

#pragma once

#include "app_config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化音频播放器 (DFPlayer Mini)
 * @return ESP_OK 成功, ESP_FAIL 失败 (系统可继续运行，无声音)
 */
esp_err_t audio_init(void);

/**
 * @brief 播放指定曲目
 * @param track 曲目编号 (1-7)
 */
void audio_play_track(int track);

/**
 * @brief 根据垃圾类型播放对应语音
 * @param type 垃圾类型
 */
void audio_play_for_type(garbage_type_t type);

/**
 * @brief 设置音量
 * @param volume 音量 0-30
 */
void audio_set_volume(int volume);

/**
 * @brief 停止播放
 */
void audio_stop(void);

/**
 * @brief 检查 DFPlayer 是否就绪
 * @return true 就绪
 */
bool audio_is_ready(void);

#ifdef __cplusplus
}
#endif