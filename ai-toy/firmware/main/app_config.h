#pragma once

/**
 * @file app_config.h
 * @brief AI玩具项目配置文件
 * 
 * 根据实际硬件连接修改以下引脚定义和参数
 */

// ============================================================
// WiFi 配置 (可通过 menuconfig 覆盖)
// ============================================================
#ifndef WIFI_SSID
#define WIFI_SSID           "your_wifi_ssid"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD       "your_wifi_password"
#endif

#define WIFI_MAXIMUM_RETRY  5
#define WIFI_CONNECT_TIMEOUT_MS  30000

// ============================================================
// 后端服务器配置
// ============================================================
#ifndef SERVER_HOST
#define SERVER_HOST         "192.168.1.100"   // 后端服务器IP
#endif

#ifndef SERVER_PORT
#define SERVER_PORT         8765              // WebSocket端口
#endif

#define SERVER_WS_URI       "ws://" SERVER_HOST ":" STR(SERVER_PORT) "/ws"
#define SERVER_WS_TIMEOUT_MS  10000

// 辅助宏
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// ============================================================
// I2S 麦克风配置 (INMP441)
// ============================================================
#define MIC_I2S_PORT        I2S_NUM_0
#define MIC_I2S_SCK_PIN     GPIO_NUM_4    // 时钟
#define MIC_I2S_WS_PIN      GPIO_NUM_5    // 字选择 (LRCK)
#define MIC_I2S_SD_PIN      GPIO_NUM_6    // 数据输入
#define MIC_SAMPLE_RATE     16000         // 采样率 16kHz
#define MIC_BITS_PER_SAMPLE 16            // 位深
#define MIC_CHANNEL_NUM     1             // 单声道
#define MIC_DMA_BUF_COUNT   8
#define MIC_DMA_BUF_LEN     1024          // 每个DMA缓冲帧数
#define MIC_GAIN_DB         24            // 麦克风增益 (dB)

// 音频帧配置
#define AUDIO_FRAME_MS      30            // 每帧时长 (ms)
#define AUDIO_FRAME_SAMPLES (MIC_SAMPLE_RATE * AUDIO_FRAME_MS / 1000)  // 480 samples
#define AUDIO_FRAME_BYTES   (AUDIO_FRAME_SAMPLES * MIC_BITS_PER_SAMPLE / 8 * MIC_CHANNEL_NUM)

// ============================================================
// I2S 扬声器配置 (MAX98357A)
// ============================================================
#define SPK_I2S_PORT        I2S_NUM_1
#define SPK_I2S_BCK_PIN     GPIO_NUM_15   // 位时钟
#define SPK_I2S_WS_PIN      GPIO_NUM_16   // 字选择 (LRCK)
#define SPK_I2S_DATA_PIN    GPIO_NUM_17   // 数据输出
#define SPK_SAMPLE_RATE     16000         // 采样率
#define SPK_BITS_PER_SAMPLE 16
#define SPK_CHANNEL_NUM     1
#define SPK_DMA_BUF_COUNT   8
#define SPK_DMA_BUF_LEN     1024

// ============================================================
// LED 配置 (WS2812B)
// ============================================================
#define LED_PIN             GPIO_NUM_48   // WS2812B 数据引脚
#define LED_NUM             1             // LED数量
#define LED_RMT_CHANNEL     RMT_CHANNEL_0

// LED 颜色定义 (RGB)
#define LED_COLOR_OFF       0x000000
#define LED_COLOR_WHITE     0xFFFFFF
#define LED_COLOR_RED       0xFF0000      // 录音中
#define LED_COLOR_GREEN     0x00FF00      // 就绪/播放
#define LED_COLOR_BLUE      0x0000FF      // 连接中
#define LED_COLOR_YELLOW    0xFFFF00      // 处理中
#define LED_COLOR_PURPLE    0xFF00FF      // 唤醒
#define LED_COLOR_CYAN      0x00FFFF      // TTS播放

// ============================================================
// 按键配置
// ============================================================
#define BUTTON_PIN          GPIO_NUM_0    // Boot按键 (也可用作功能键)
#define BUTTON_ACTIVE_LEVEL 0             // 低电平有效
#define BUTTON_DEBOUNCE_MS  50
#define BUTTON_LONG_PRESS_MS 2000         // 长按时间

// ============================================================
// 唤醒词配置 (ESP-SR)
// ============================================================
#define WAKE_WORD_ENABLED   1
#define WAKE_WORD_THRESHOLD 0.9f          // 唤醒阈值 (0.0-1.0)

// ============================================================
// VAD (语音活动检测) 配置
// ============================================================
#define VAD_ENABLED         1
#define VAD_SILENCE_MS      1500          // 静音超时 (ms)，超过则停止录音
#define VAD_MIN_SPEECH_MS   300           // 最短有效语音时长 (ms)

// ============================================================
// 音频编码配置
// ============================================================
// 发送给服务器的音频格式: PCM 16bit 16kHz 单声道
#define AUDIO_ENCODE_PCM    0
#define AUDIO_ENCODE_OPUS   1
#define AUDIO_ENCODE_FORMAT AUDIO_ENCODE_PCM  // 默认使用PCM

// ============================================================
// 任务优先级和栈大小
// ============================================================
#define TASK_PRIORITY_AUDIO_CAPTURE  (configMAX_PRIORITIES - 2)
#define TASK_PRIORITY_AUDIO_PLAY     (configMAX_PRIORITIES - 2)
#define TASK_PRIORITY_WEBSOCKET      (configMAX_PRIORITIES - 3)
#define TASK_PRIORITY_AI_ASSISTANT   (configMAX_PRIORITIES - 4)
#define TASK_PRIORITY_LED            (configMAX_PRIORITIES - 5)

#define TASK_STACK_AUDIO_CAPTURE  4096
#define TASK_STACK_AUDIO_PLAY     4096
#define TASK_STACK_WEBSOCKET      8192
#define TASK_STACK_AI_ASSISTANT   8192
#define TASK_STACK_LED            2048

// ============================================================
// 消息队列配置
// ============================================================
#define AUDIO_QUEUE_SIZE    20            // 音频数据队列深度
#define EVENT_QUEUE_SIZE    10            // 事件队列深度

// ============================================================
// 调试配置
// ============================================================
#define DEBUG_AUDIO_LEVEL   0             // 打印音频电平
#define DEBUG_WEBSOCKET     0             // 打印WebSocket消息