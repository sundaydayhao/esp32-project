#pragma once

/**
 * AI 睡前故事机 - 配置文件
 * ESP32-S3 硬件引脚和系统参数配置
 */

// ==================== WiFi 配置 ====================
#define WIFI_SSID           "your_wifi_ssid"
#define WIFI_PASSWORD       "your_wifi_password"
#define WIFI_MAX_RETRY      5

// ==================== 服务器配置 ====================
#define SERVER_HOST         "192.168.1.100"   // 后端服务器 IP
#define SERVER_PORT         8000
#define SERVER_WS_PATH      "/ws/esp32s3-001"
#define SERVER_WS_URI       "ws://" SERVER_HOST ":" "8000" SERVER_WS_PATH

// ==================== TFT 屏幕引脚 (ST7789 240x240) ====================
#define TFT_MOSI_PIN        40    // SDA
#define TFT_SCLK_PIN        39    // SCL
#define TFT_CS_PIN          38    // CS
#define TFT_DC_PIN          37    // DC (Data/Command)
#define TFT_RST_PIN         36    // RST
#define TFT_BL_PIN          35    // 背光 (可选)

#define TFT_WIDTH           240
#define TFT_HEIGHT          240
#define TFT_SPI_HOST        SPI2_HOST
#define TFT_SPI_FREQ        40000000  // 40 MHz

// ==================== 麦克风引脚 (INMP441 I2S) ====================
#define MIC_WS_PIN          14    // LRCK (Word Select)
#define MIC_SCK_PIN         13    // BCLK (Bit Clock)
#define MIC_SD_PIN          12    // DATA (Serial Data)
#define MIC_I2S_PORT        I2S_NUM_0
#define MIC_SAMPLE_RATE     16000
#define MIC_BITS_PER_SAMPLE 16
#define MIC_CHANNELS        1
#define MIC_BUFFER_SIZE     1024  // 每次读取的样本数

// ==================== 扬声器引脚 (MAX98357A I2S) ====================
#define SPK_LRC_PIN         8     // LRCK
#define SPK_BCLK_PIN        9     // BCLK
#define SPK_DIN_PIN         10    // DIN
#define SPK_I2S_PORT        I2S_NUM_1
#define SPK_SAMPLE_RATE     24000  // MP3 解码后的采样率
#define SPK_BITS_PER_SAMPLE 16
#define SPK_CHANNELS        1

// ==================== 按钮引脚 ====================
#define BTN_RECORD_PIN      0     // 录音按钮 (BOOT 按钮)
#define BTN_STOP_PIN        -1    // 停止按钮 (可选)

// ==================== LED 指示灯 ====================
#define LED_PIN             2     // 状态 LED (可选)

// ==================== 音频参数 ====================
#define AUDIO_RECORD_MAX_MS     8000   // 最大录音时长 (ms)
#define AUDIO_SILENCE_THRESHOLD 500    // 静音检测阈值 (RMS)
#define AUDIO_SILENCE_MS        1500   // 静音持续时间触发停止 (ms)
#define AUDIO_CHUNK_SIZE        4096   // 音频传输块大小

// ==================== WebSocket 参数 ====================
#define WS_RECONNECT_INTERVAL_MS    5000   // 重连间隔
#define WS_PING_INTERVAL_MS         30000  // 心跳间隔
#define WS_BUFFER_SIZE              8192   // 接收缓冲区大小

// ==================== 显示参数 ====================
#define DISPLAY_TEXT_SIZE       2
#define DISPLAY_BG_COLOR        0x0000  // 黑色 (RGB565)
#define DISPLAY_TEXT_COLOR      0xFFFF  // 白色 (RGB565)
#define DISPLAY_ACCENT_COLOR    0xFFE0  // 黄色 (RGB565)

// ==================== 任务优先级 ====================
#define TASK_WIFI_PRIORITY      5
#define TASK_WS_PRIORITY        6
#define TASK_AUDIO_IN_PRIORITY  7
#define TASK_AUDIO_OUT_PRIORITY 7
#define TASK_DISPLAY_PRIORITY   4
#define TASK_MAIN_PRIORITY      3

// ==================== 任务栈大小 ====================
#define TASK_WIFI_STACK         4096
#define TASK_WS_STACK           8192
#define TASK_AUDIO_IN_STACK     4096
#define TASK_AUDIO_OUT_STACK    8192
#define TASK_DISPLAY_STACK      4096

// ==================== 设备状态 ====================
typedef enum {
    STATE_INIT = 0,         // 初始化
    STATE_WIFI_CONNECTING,  // WiFi 连接中
    STATE_WIFI_CONNECTED,   // WiFi 已连接
    STATE_WS_CONNECTING,    // WebSocket 连接中
    STATE_IDLE,             // 空闲，等待唤醒
    STATE_LISTENING,        // 正在录音
    STATE_PROCESSING,       // 处理中（识别/生成）
    STATE_PLAYING,          // 播放故事中
    STATE_ERROR,            // 错误状态
    STATE_SLEEPING,         // 睡眠模式
} device_state_t;

// ==================== 消息类型 ====================
typedef enum {
    MSG_WIFI_CONNECTED = 0,
    MSG_WIFI_DISCONNECTED,
    MSG_WS_CONNECTED,
    MSG_WS_DISCONNECTED,
    MSG_WS_DATA,
    MSG_BUTTON_PRESSED,
    MSG_AUDIO_READY,
    MSG_STORY_START,
    MSG_STORY_COMPLETE,
    MSG_ERROR,
} message_type_t;

// ==================== 消息结构 ====================
typedef struct {
    message_type_t type;
    void *data;
    size_t data_len;
} device_message_t;