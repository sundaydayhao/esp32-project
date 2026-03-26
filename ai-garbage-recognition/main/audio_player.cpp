/**
 * audio_player.cpp
 * 音频播放模块实现 / Audio Player Implementation
 *
 * DFPlayer Mini 通信协议:
 *   波特率: 9600
 *   数据帧格式 (10字节):
 *     [0] 0x7E  起始位
 *     [1] 0xFF  版本
 *     [2] 0x06  数据长度
 *     [3] CMD   命令字
 *     [4] 0x00  反馈标志 (0=不需要反馈)
 *     [5] MSB   参数高字节
 *     [6] LSB   参数低字节
 *     [7] CSUM_H 校验和高字节
 *     [8] CSUM_L 校验和低字节
 *     [9] 0xEF  结束位
 *
 * 常用命令:
 *   0x03 - 指定曲目播放
 *   0x06 - 设置音量 (0-30)
 *   0x16 - 停止播放
 *   0x0F - 指定文件夹播放
 */

#include "audio_player.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "AUDIO";

// DFPlayer Mini 命令定义
#define DFPLAYER_CMD_PLAY_TRACK     0x03  // 播放指定曲目
#define DFPLAYER_CMD_VOLUME         0x06  // 设置音量
#define DFPLAYER_CMD_PLAY_FOLDER    0x0F  // 播放指定文件夹中的曲目
#define DFPLAYER_CMD_STOP           0x16  // 停止
#define DFPLAYER_CMD_RESET          0x0C  // 复位
#define DFPLAYER_CMD_QUERY_STATUS   0x42  // 查询状态

#define DFPLAYER_FRAME_SIZE         10
#define DFPLAYER_START_BYTE         0x7E
#define DFPLAYER_VERSION            0xFF
#define DFPLAYER_DATA_LEN           0x06
#define DFPLAYER_END_BYTE           0xEF
#define DFPLAYER_DEFAULT_VOLUME     20    // 默认音量 (0-30)
#define DFPLAYER_FOLDER             1     // 使用 /01/ 文件夹

static bool s_initialized = false;
static bool s_ready       = false;

// ============================================================
// 内部函数 / Internal Functions
// ============================================================

/**
 * 计算 DFPlayer 帧校验和
 * 校验和 = -(版本 + 数据长度 + 命令 + 反馈 + MSB + LSB)
 */
static int16_t calc_checksum(const uint8_t *frame)
{
    int16_t sum = 0;
    for (int i = 1; i <= 6; i++) {
        sum += frame[i];
    }
    return -sum;
}

/**
 * 发送 DFPlayer 命令帧
 */
static void send_command(uint8_t cmd, uint8_t msb, uint8_t lsb)
{
    if (!s_initialized) return;

    uint8_t frame[DFPLAYER_FRAME_SIZE];
    frame[0] = DFPLAYER_START_BYTE;
    frame[1] = DFPLAYER_VERSION;
    frame[2] = DFPLAYER_DATA_LEN;
    frame[3] = cmd;
    frame[4] = 0x00;  // 不需要反馈
    frame[5] = msb;
    frame[6] = lsb;

    int16_t checksum = calc_checksum(frame);
    frame[7] = (uint8_t)((checksum >> 8) & 0xFF);
    frame[8] = (uint8_t)(checksum & 0xFF);
    frame[9] = DFPLAYER_END_BYTE;

    int written = uart_write_bytes(DFPLAYER_UART_NUM, (const char*)frame, DFPLAYER_FRAME_SIZE);
    if (written != DFPLAYER_FRAME_SIZE) {
        ESP_LOGW(TAG, "UART 写入不完整: %d/%d", written, DFPLAYER_FRAME_SIZE);
    }

    ESP_LOGD(TAG, "发送命令: CMD=0x%02X MSB=0x%02X LSB=0x%02X", cmd, msb, lsb);
}

// ============================================================
// 公共接口实现 / Public Interface Implementation
// ============================================================

esp_err_t audio_init(void)
{
    if (s_initialized) return ESP_OK;

    // 配置 UART
    uart_config_t uart_conf = {
        .baud_rate           = DFPLAYER_BAUD_RATE,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk          = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(DFPLAYER_UART_NUM, &uart_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART 参数配置失败: %d", ret);
        return ret;
    }

    ret = uart_set_pin(DFPLAYER_UART_NUM,
                       DFPLAYER_TX_GPIO,
                       DFPLAYER_RX_GPIO,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART 引脚配置失败: %d", ret);
        return ret;
    }

    ret = uart_driver_install(DFPLAYER_UART_NUM,
                              DFPLAYER_UART_BUF_SIZE,
                              DFPLAYER_UART_BUF_SIZE,
                              0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART 驱动安装失败: %d", ret);
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "UART%d 初始化成功 (TX=GPIO%d, RX=GPIO%d, %dbps)",
             DFPLAYER_UART_NUM, DFPLAYER_TX_GPIO, DFPLAYER_RX_GPIO,
             DFPLAYER_BAUD_RATE);

    // 等待 DFPlayer 上电稳定
    vTaskDelay(pdMS_TO_TICKS(1500));

    // 复位 DFPlayer
    send_command(DFPLAYER_CMD_RESET, 0x00, 0x00);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 设置默认音量
    send_command(DFPLAYER_CMD_VOLUME, 0x00, DFPLAYER_DEFAULT_VOLUME);
    vTaskDelay(pdMS_TO_TICKS(100));

    s_ready = true;
    ESP_LOGI(TAG, "DFPlayer Mini 初始化完成，音量: %d", DFPLAYER_DEFAULT_VOLUME);
    return ESP_OK;
}

void audio_play_track(int track)
{
    if (!s_ready) {
        ESP_LOGW(TAG, "音频播放器未就绪，跳过播放");
        return;
    }

    if (track < 1 || track > 99) {
        ESP_LOGW(TAG, "无效曲目编号: %d", track);
        return;
    }

    ESP_LOGI(TAG, "播放曲目: /01/%03d.mp3", track);

    // 使用文件夹播放命令: 文件夹1, 曲目N
    send_command(DFPLAYER_CMD_PLAY_FOLDER,
                 (uint8_t)DFPLAYER_FOLDER,
                 (uint8_t)track);
}

void audio_play_for_type(garbage_type_t type)
{
    int track = AUDIO_TRACK_UNKNOWN;

    switch (type) {
        case GARBAGE_HAZARDOUS:   track = AUDIO_TRACK_HAZARDOUS;   break;
        case GARBAGE_RECYCLABLE:  track = AUDIO_TRACK_RECYCLABLE;  break;
        case GARBAGE_KITCHEN:     track = AUDIO_TRACK_KITCHEN;     break;
        case GARBAGE_OTHER:       track = AUDIO_TRACK_OTHER;       break;
        default:                  track = AUDIO_TRACK_UNKNOWN;     break;
    }

    ESP_LOGI(TAG, "播放垃圾类型语音: %s (曲目%d)",
             GARBAGE_NAMES_CN[type < GARBAGE_TYPE_COUNT ? type : GARBAGE_UNKNOWN],
             track);

    audio_play_track(track);
}

void audio_set_volume(int volume)
{
    if (!s_ready) return;

    if (volume < 0)  volume = 0;
    if (volume > 30) volume = 30;

    send_command(DFPLAYER_CMD_VOLUME, 0x00, (uint8_t)volume);
    ESP_LOGI(TAG, "音量设置为: %d", volume);
}

void audio_stop(void)
{
    if (!s_ready) return;
    send_command(DFPLAYER_CMD_STOP, 0x00, 0x00);
    ESP_LOGI(TAG, "停止播放");
}

bool audio_is_ready(void)
{
    return s_ready;
}