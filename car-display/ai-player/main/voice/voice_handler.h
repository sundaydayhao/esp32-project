#pragma once

/**
 * @file voice_handler.h
 * @brief 语音模块接口
 * 
 * 硬件：
 *   - SYN6288 中文TTS语音合成模块（UART TX）
 *   - HLK-V20 离线语音识别模块（UART RX）
 * 
 * 功能：
 *   - TTS：将文字转换为语音播报
 *   - ASR：识别用户语音指令，触发共情响应
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * HLK-V20 语音识别指令ID定义
 * 需与HLK-V20模块烧录的指令表一致
 * ============================================================ */
typedef enum {
    ASR_CMD_NONE        = 0x00,
    ASR_CMD_WAKE        = 0x01,  // 唤醒词：小车伴
    ASR_CMD_SPEED       = 0x02,  // 现在多快 / 速度多少
    ASR_CMD_TIRED       = 0x03,  // 我累了 / 好累
    ASR_CMD_TRAFFIC_JAM = 0x04,  // 堵车了 / 好堵
    ASR_CMD_WEATHER     = 0x05,  // 天气怎么样
    ASR_CMD_JOKE        = 0x06,  // 讲个笑话
    ASR_CMD_HELLO       = 0x07,  // 你好 / 嗨
    ASR_CMD_THANKS      = 0x08,  // 谢谢
    ASR_CMD_QUIET       = 0x09,  // 安静 / 别说了
    ASR_CMD_LOUDER      = 0x0A,  // 大声点
    ASR_CMD_SOFTER      = 0x0B,  // 小声点
    ASR_CMD_MUSIC       = 0x0C,  // 来首歌 / 放音乐
    ASR_CMD_NAVIGATE    = 0x0D,  // 导航 / 带我回家
    ASR_CMD_TRIP_RESET  = 0x0E,  // 重置行程
    ASR_CMD_STATUS      = 0x0F,  // 状态怎么样 / 报告
} asr_command_t;

/**
 * @brief 语音识别回调函数类型
 * @param cmd 识别到的指令ID
 */
typedef void (*asr_callback_t)(asr_command_t cmd);

/**
 * @brief 初始化语音模块
 * 
 * 初始化UART，连接SYN6288和HLK-V20
 * 
 * @return ESP_OK 成功
 */
esp_err_t voice_handler_init(void);

/**
 * @brief 注册语音识别回调
 * @param cb 回调函数，识别到指令时调用
 */
void voice_handler_register_asr_callback(asr_callback_t cb);

/**
 * @brief TTS播报文字（UTF-8中文）
 * 
 * @param text 要播报的文字（UTF-8编码）
 * @return ESP_OK 成功
 */
esp_err_t voice_handler_speak(const char *text);

/**
 * @brief TTS播报格式化文字
 * 
 * @param fmt 格式字符串
 * @param ... 参数
 * @return ESP_OK 成功
 */
esp_err_t voice_handler_speakf(const char *fmt, ...);

/**
 * @brief 停止当前播报
 */
void voice_handler_stop(void);

/**
 * @brief 设置TTS音量
 * @param volume 音量 0-9
 */
void voice_handler_set_volume(uint8_t volume);

/**
 * @brief 设置TTS语速
 * @param speed 语速 0-9（5为正常）
 */
void voice_handler_set_speed(uint8_t speed);

/**
 * @brief 获取TTS是否正在播报
 * @return true 正在播报
 */
bool voice_handler_is_speaking(void);

/**
 * @brief 获取HLK-V20是否已唤醒
 * @return true 已唤醒，等待指令
 */
bool voice_handler_is_awake(void);

#ifdef __cplusplus
}
#endif