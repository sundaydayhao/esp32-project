/**
 * @file voice_handler.c
 * @brief 语音模块实现
 * 
 * SYN6288 TTS 协议：
 *   帧格式: 0xFD [LEN_H] [LEN_L] [CMD] [ENC] [TEXT...]
 *   CMD=0x01: GBK编码  CMD=0x04: UTF-8编码
 *   LEN = sizeof(CMD) + sizeof(ENC) + sizeof(TEXT)
 * 
 * HLK-V20 ASR 协议：
 *   接收帧: 0xAA [TYPE] [LEN] [DATA...] [SUM]
 *   TYPE=0x01: 唤醒  TYPE=0x02: 休眠  TYPE=0x03: 识别结果
 *   识别结果DATA[0]为指令ID
 */

#include "voice_handler.h"
#include "config.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "voice";

/* ============================================================
 * 内部状态
 * ============================================================ */

static asr_callback_t s_asr_callback = NULL;
static bool s_is_speaking = false;
static bool s_is_awake = false;
static uint8_t s_volume = 5;   // 默认音量5
static uint8_t s_speed = 5;    // 默认语速5
static int64_t s_awake_time = 0;  // 唤醒时间戳

/* TTS发送队列 */
static QueueHandle_t s_tts_queue = NULL;
#define TTS_MAX_TEXT_LEN    256
#define TTS_QUEUE_SIZE      8

/* ============================================================
 * SYN6288 TTS 驱动
 * ============================================================ */

/**
 * @brief 发送SYN6288 TTS帧
 * 
 * 帧格式: 0xFD [LEN_H] [LEN_L] 0x04 0x00 [UTF-8 TEXT]
 * CMD=0x04 表示UTF-8编码
 */
static esp_err_t syn6288_send_text(const char *text)
{
    if (text == NULL || strlen(text) == 0) return ESP_ERR_INVALID_ARG;

    size_t text_len = strlen(text);
    /* LEN = CMD(1) + ENC(1) + text_len */
    uint16_t frame_len = 2 + text_len;

    /* 构建帧 */
    uint8_t *frame = malloc(5 + text_len);
    if (frame == NULL) return ESP_ERR_NO_MEM;

    frame[0] = 0xFD;                        // 帧头
    frame[1] = (frame_len >> 8) & 0xFF;     // 长度高字节
    frame[2] = frame_len & 0xFF;            // 长度低字节
    frame[3] = 0x04;                        // 命令：UTF-8文本播报
    frame[4] = 0x00;                        // 背景音乐：无

    memcpy(frame + 5, text, text_len);

    /* 发送帧 */
    int written = uart_write_bytes(VOICE_UART_NUM, frame, 5 + text_len);
    free(frame);

    if (written < 0) {
        ESP_LOGE(TAG, "TTS发送失败");
        return ESP_FAIL;
    }

    s_is_speaking = true;
    ESP_LOGI(TAG, "TTS播报: %s", text);
    return ESP_OK;
}

/**
 * @brief 发送SYN6288控制命令
 * 帧格式: 0xFD 0x00 0x01 [CMD]
 */
static void syn6288_send_cmd(uint8_t cmd)
{
    uint8_t frame[4] = {0xFD, 0x00, 0x01, cmd};
    uart_write_bytes(VOICE_UART_NUM, frame, 4);
}

/**
 * @brief 停止TTS播报
 */
static void syn6288_stop(void)
{
    syn6288_send_cmd(0x02);  // 停止合成
    s_is_speaking = false;
}

/**
 * @brief 设置TTS音量（通过控制字符串）
 * SYN6288支持在文本中嵌入控制标签: [v5] 表示音量5
 */
static void syn6288_set_volume_speed(const char *text, char *out_buf, size_t buf_len)
{
    /* 在文本前添加音量和语速控制标签 */
    snprintf(out_buf, buf_len, "[v%d][s%d]%s", s_volume, s_speed, text);
}

/* ============================================================
 * HLK-V20 ASR 驱动
 * ============================================================ */

/**
 * @brief 解析HLK-V20数据帧
 * 
 * 帧格式: 0xAA [TYPE] [LEN] [DATA...] [SUM]
 * SUM = TYPE + LEN + DATA[0] + ... (低8位)
 */
static bool hlkv20_parse_frame(const uint8_t *buf, int len, 
                                 uint8_t *type, uint8_t *cmd_id)
{
    if (len < 4) return false;
    if (buf[0] != 0xAA) return false;

    uint8_t frame_type = buf[1];
    uint8_t data_len   = buf[2];

    if (len < 3 + data_len + 1) return false;

    /* 校验和验证 */
    uint8_t sum = 0;
    for (int i = 1; i < 3 + data_len; i++) {
        sum += buf[i];
    }
    if (sum != buf[3 + data_len]) {
        ESP_LOGW(TAG, "ASR帧校验失败: 计算=%02X 接收=%02X", sum, buf[3 + data_len]);
        return false;
    }

    *type = frame_type;
    *cmd_id = (data_len > 0) ? buf[3] : 0;
    return true;
}

/* ============================================================
 * TTS 发送任务
 * ============================================================ */

static void tts_task(void *arg)
{
    static char text_buf[TTS_MAX_TEXT_LEN];
    static char ctrl_buf[TTS_MAX_TEXT_LEN + 16];

    ESP_LOGI(TAG, "TTS发送任务启动");

    while (1) {
        if (xQueueReceive(s_tts_queue, text_buf, portMAX_DELAY) == pdTRUE) {
            /* 添加音量/语速控制标签 */
            syn6288_set_volume_speed(text_buf, ctrl_buf, sizeof(ctrl_buf));

            /* 发送TTS */
            syn6288_send_text(ctrl_buf);

            /* 等待播报完成（估算时长：每个汉字约0.3秒） */
            /* 简单估算：UTF-8中文每字3字节，每字约300ms */
            size_t char_count = strlen(text_buf) / 3 + 1;
            uint32_t wait_ms = char_count * 350 + 500;
            if (wait_ms > 10000) wait_ms = 10000;  // 最长等待10秒

            vTaskDelay(pdMS_TO_TICKS(wait_ms));
            s_is_speaking = false;
        }
    }
}

/* ============================================================
 * ASR 接收任务
 * ============================================================ */

static void asr_task(void *arg)
{
    uint8_t buf[64];
    uint8_t frame_buf[32];
    int frame_pos = 0;
    bool in_frame = false;

    ESP_LOGI(TAG, "ASR接收任务启动");

    while (1) {
        int len = uart_read_bytes(VOICE_UART_NUM, buf, sizeof(buf) - 1,
                                   pdMS_TO_TICKS(50));
        if (len <= 0) {
            /* 检查唤醒超时（30秒无指令则休眠） */
            if (s_is_awake) {
                int64_t now = esp_timer_get_time() / 1000;
                if ((now - s_awake_time) > 30000) {
                    s_is_awake = false;
                    ESP_LOGI(TAG, "ASR超时休眠");
                }
            }
            continue;
        }

        /* 解析接收到的字节 */
        for (int i = 0; i < len; i++) {
            uint8_t byte = buf[i];

            if (!in_frame) {
                if (byte == 0xAA) {
                    in_frame = true;
                    frame_pos = 0;
                    frame_buf[frame_pos++] = byte;
                }
            } else {
                if (frame_pos < (int)sizeof(frame_buf)) {
                    frame_buf[frame_pos++] = byte;
                }

                /* 检查帧是否完整 */
                if (frame_pos >= 3) {
                    uint8_t data_len = frame_buf[2];
                    int expected_len = 3 + data_len + 1;  // 头+类型+长度+数据+校验

                    if (frame_pos >= expected_len) {
                        /* 解析帧 */
                        uint8_t frame_type = 0, cmd_id = 0;
                        if (hlkv20_parse_frame(frame_buf, frame_pos,
                                               &frame_type, &cmd_id)) {
                            switch (frame_type) {
                                case 0x01:  /* 唤醒 */
                                    s_is_awake = true;
                                    s_awake_time = esp_timer_get_time() / 1000;
                                    ESP_LOGI(TAG, "ASR唤醒！");
                                    voice_handler_speak("我在，请说");
                                    if (s_asr_callback) {
                                        s_asr_callback(ASR_CMD_WAKE);
                                    }
                                    break;

                                case 0x02:  /* 休眠 */
                                    s_is_awake = false;
                                    ESP_LOGI(TAG, "ASR进入休眠");
                                    break;

                                case 0x03:  /* 识别结果 */
                                    if (s_is_awake) {
                                        s_awake_time = esp_timer_get_time() / 1000;  // 重置超时
                                        ESP_LOGI(TAG, "ASR识别指令: 0x%02X", cmd_id);
                                        if (s_asr_callback) {
                                            s_asr_callback((asr_command_t)cmd_id);
                                        }
                                    }
                                    break;

                                default:
                                    ESP_LOGD(TAG, "ASR未知帧类型: 0x%02X", frame_type);
                                    break;
                            }
                        }

                        in_frame = false;
                        frame_pos = 0;
                    }
                }
            }
        }
    }
}

/* ============================================================
 * 公共接口实现
 * ============================================================ */

esp_err_t voice_handler_init(void)
{
    ESP_LOGI(TAG, "初始化语音模块 (SYN6288 TTS + HLK-V20 ASR)");

    /* 创建TTS发送队列 */
    s_tts_queue = xQueueCreate(TTS_QUEUE_SIZE, TTS_MAX_TEXT_LEN);
    if (s_tts_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* 初始化UART（共用一个UART，TX接SYN6288，RX接HLK-V20） */
    uart_config_t uart_config = {
        .baud_rate  = VOICE_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(VOICE_UART_NUM,
                                         VOICE_UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "语音UART安装失败: %s", esp_err_to_name(ret));
        return ret;
    }

    uart_param_config(VOICE_UART_NUM, &uart_config);
    uart_set_pin(VOICE_UART_NUM,
                 VOICE_TTS_TX_PIN,   /* TX -> SYN6288 RX */
                 VOICE_ASR_RX_PIN,   /* RX <- HLK-V20 TX */
                 UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE);

    ESP_LOGI(TAG, "语音UART初始化完成 (TX:%d->SYN6288, RX:%d<-HLK-V20, %dbps)",
             VOICE_TTS_TX_PIN, VOICE_ASR_RX_PIN, VOICE_UART_BAUD);

    /* 等待模块上电稳定 */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 启动TTS发送任务 */
    xTaskCreate(tts_task, "tts_task", 3072, NULL, 5, NULL);

    /* 启动ASR接收任务 */
    xTaskCreate(asr_task, "asr_task", 3072, NULL, 6, NULL);

    /* 播报启动提示 */
    vTaskDelay(pdMS_TO_TICKS(200));
    voice_handler_speak("小车伴已就绪，出发啦");

    ESP_LOGI(TAG, "语音模块初始化完成");
    return ESP_OK;
}

void voice_handler_register_asr_callback(asr_callback_t cb)
{
    s_asr_callback = cb;
}

esp_err_t voice_handler_speak(const char *text)
{
    if (text == NULL || s_tts_queue == NULL) return ESP_ERR_INVALID_ARG;

    static char buf[TTS_MAX_TEXT_LEN];
    snprintf(buf, sizeof(buf), "%s", text);

    if (xQueueSend(s_tts_queue, buf, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGW(TAG, "TTS队列已满，丢弃: %.30s", text);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t voice_handler_speakf(const char *fmt, ...)
{
    static char buf[TTS_MAX_TEXT_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return voice_handler_speak(buf);
}

void voice_handler_stop(void)
{
    syn6288_stop();
    /* 清空TTS队列 */
    if (s_tts_queue) {
        xQueueReset(s_tts_queue);
    }
}

void voice_handler_set_volume(uint8_t volume)
{
    if (volume > 9) volume = 9;
    s_volume = volume;
    ESP_LOGI(TAG, "TTS音量设置为: %d", s_volume);
}

void voice_handler_set_speed(uint8_t speed)
{
    if (speed > 9) speed = 9;
    s_speed = speed;
    ESP_LOGI(TAG, "TTS语速设置为: %d", s_speed);
}

bool voice_handler_is_speaking(void)
{
    return s_is_speaking;
}

bool voice_handler_is_awake(void)
{
    return s_is_awake;
}