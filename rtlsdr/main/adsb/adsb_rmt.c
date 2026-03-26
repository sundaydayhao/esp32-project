#include "adsb_rmt.h"
#include "adsb_decoder.h"
#include "config.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/rmt_rx.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "adsb_rmt";

// ---------------------------------------------------------------------------
// RMT 配置
// ---------------------------------------------------------------------------
// 分辨率：2MHz = 每tick 0.5μs
// Mode-S 前导码 = 16 ticks (8μs)
// Mode-S 长消息 = 448 ticks (224μs)
// 总计最大 = 464 ticks，缓冲区取 512

#define RMT_RESOLUTION_HZ   2000000     // 2MHz, 0.5μs/tick
#define RMT_MEM_SYMBOLS     512         // RMT 符号缓冲区大小
#define RMT_IDLE_US         300         // 300μs 空闲视为消息结束

// Mode-S 时序（单位：RMT tick = 0.5μs）
#define PREAMBLE_TICKS      16          // 前导码 8μs = 16 ticks
#define BIT_TICKS           4           // 每比特 2μs = 4 ticks
#define PULSE_TICKS         2           // 脉冲宽度 1μs = 2 ticks
#define SHORT_MSG_BITS      56          // 短消息 56 比特
#define LONG_MSG_BITS       112         // 长消息 112 比特
#define MAX_TIME_DOMAIN     600         // 时域数组最大长度 (300μs)

// ---------------------------------------------------------------------------
// 内部状态
// ---------------------------------------------------------------------------
static rmt_channel_handle_t s_rx_chan = NULL;
static QueueHandle_t        s_rmt_queue = NULL;
static rmt_symbol_word_t    s_raw_symbols[RMT_MEM_SYMBOLS];

// ---------------------------------------------------------------------------
// RMT 接收完成回调（ISR 上下文）
// ---------------------------------------------------------------------------
static bool IRAM_ATTR rmt_rx_done_cb(rmt_channel_handle_t channel,
                                      const rmt_rx_done_event_data_t *edata,
                                      void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_data;
    // 将事件数据发送到处理队列
    xQueueSendFromISR(queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

// ---------------------------------------------------------------------------
// 将 RMT 符号流展开为时域数组
// 每个元素代表 0.5μs，值为 0 或 1
// ---------------------------------------------------------------------------
static int symbols_to_time_domain(const rmt_symbol_word_t *symbols,
                                   size_t num_symbols,
                                   uint8_t *td, int max_len)
{
    int pos = 0;
    for (size_t i = 0; i < num_symbols && pos < max_len; i++) {
        // 每个 RMT 符号包含两段：(level0, duration0) 和 (level1, duration1)
        int d0 = (int)symbols[i].duration0;
        uint8_t l0 = (uint8_t)symbols[i].level0;
        for (int j = 0; j < d0 && pos < max_len; j++) {
            td[pos++] = l0;
        }

        int d1 = (int)symbols[i].duration1;
        uint8_t l1 = (uint8_t)symbols[i].level1;
        for (int j = 0; j < d1 && pos < max_len; j++) {
            td[pos++] = l1;
        }
    }
    return pos;
}

// ---------------------------------------------------------------------------
// 在时域数组中搜索 Mode-S 前导码
//
// 前导码模式（16 ticks = 8μs，0.5μs/tick）：
// tick: 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15
// val:  1  0  1  0  0  0  0  1  0  1  0  0  0  0  0  0
//
// 对应脉冲：
//   脉冲1: t=0~0.5μs (tick 0)
//   脉冲2: t=1~1.5μs (tick 2)
//   脉冲3: t=3.5~4μs (tick 7)
//   脉冲4: t=4.5~5μs (tick 9)
// ---------------------------------------------------------------------------
static const uint8_t PREAMBLE_PATTERN[PREAMBLE_TICKS] = {
    1, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0
};

static int find_preamble(const uint8_t *td, int td_len)
{
    for (int i = 0; i <= td_len - PREAMBLE_TICKS; i++) {
        bool match = true;
        for (int j = 0; j < PREAMBLE_TICKS; j++) {
            if (td[i + j] != PREAMBLE_PATTERN[j]) {
                match = false;
                break;
            }
        }
        if (match) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// 从时域数组解码 Mode-S 比特
//
// 每比特 4 ticks (2μs)：
//   比特1：前2 ticks 高电平 {1,1,0,0}
//   比特0：后2 ticks 高电平 {0,0,1,1}
// ---------------------------------------------------------------------------
static int decode_bits(const uint8_t *td, int start, int td_len,
                        uint8_t *msg, int num_bits)
{
    memset(msg, 0, (num_bits + 7) / 8);

    for (int i = 0; i < num_bits; i++) {
        int tick = start + i * BIT_TICKS;
        if (tick + BIT_TICKS - 1 >= td_len) {
            return i;  // 数据不足
        }

        // 检查前半段（比特1）还是后半段（比特0）有脉冲
        int first_half  = td[tick] + td[tick + 1];
        int second_half = td[tick + 2] + td[tick + 3];

        uint8_t bit;
        if (first_half > second_half) {
            bit = 1;
        } else if (second_half > first_half) {
            bit = 0;
        } else {
            // 无法判断，消息无效
            return -1;
        }

        msg[i / 8] |= (bit << (7 - (i % 8)));
    }
    return num_bits;
}

// ---------------------------------------------------------------------------
// CRC-24 校验（与 adsb_decoder.c 中相同）
// ---------------------------------------------------------------------------
static const uint32_t CRC24_GEN = 0xFFF409;

static uint32_t crc24(const uint8_t *msg, int len)
{
    uint32_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= ((uint32_t)msg[i]) << 16;
        for (int j = 0; j < 8; j++) {
            crc <<= 1;
            if (crc & 0x1000000) crc ^= CRC24_GEN;
        }
    }
    return crc & 0xFFFFFF;
}

// ---------------------------------------------------------------------------
// 处理一帧 RMT 数据
// ---------------------------------------------------------------------------
static void process_rmt_frame(const rmt_symbol_word_t *symbols, size_t num_symbols)
{
    // 展开为时域数组
    static uint8_t td[MAX_TIME_DOMAIN];
    int td_len = symbols_to_time_domain(symbols, num_symbols, td, MAX_TIME_DOMAIN);

    if (td_len < PREAMBLE_TICKS + SHORT_MSG_BITS * BIT_TICKS) {
        return;  // 数据太短
    }

    // 搜索前导码
    int preamble_pos = find_preamble(td, td_len);
    if (preamble_pos < 0) {
        return;  // 未找到前导码
    }

    int data_start = preamble_pos + PREAMBLE_TICKS;
    int bits_available = (td_len - data_start) / BIT_TICKS;

    // 确定消息长度（先解码前5比特获取 DF 字段）
    if (bits_available < 8) return;

    uint8_t first_byte = 0;
    for (int i = 0; i < 8; i++) {
        int tick = data_start + i * BIT_TICKS;
        if (tick + BIT_TICKS - 1 >= td_len) break;
        int fh = td[tick] + td[tick + 1];
        int sh = td[tick + 2] + td[tick + 3];
        if (fh > sh) first_byte |= (1 << (7 - i));
    }

    int df = (first_byte >> 3) & 0x1F;
    int msg_bits;

    // 根据 DF 确定消息长度
    switch (df) {
        case 0: case 4: case 5: case 11:
            msg_bits = SHORT_MSG_BITS;
            break;
        case 16: case 17: case 18: case 19:
        case 20: case 21: case 24:
            msg_bits = LONG_MSG_BITS;
            break;
        default:
            return;  // 不支持的 DF
    }

    if (bits_available < msg_bits) return;

    // 解码完整消息
    uint8_t msg[14] = {0};
    int decoded = decode_bits(td, data_start, td_len, msg, msg_bits);
    if (decoded != msg_bits) return;

    int msg_bytes = msg_bits / 8;

    // CRC 校验（仅对 DF17/18 进行）
    if (df == 17 || df == 18) {
        if (msg_bytes < 14) return;
        uint32_t crc_calc = crc24(msg, 11);
        uint32_t crc_msg  = ((uint32_t)msg[11] << 16) |
                            ((uint32_t)msg[12] << 8)  |
                             (uint32_t)msg[13];
        if (crc_calc != crc_msg) return;
    }

    // 将解码后的消息注入 ADS-B 解码器
    // 通过构造 AVR 格式字符串并调用内部接口
    // 这里直接调用解码器的内部处理函数（通过公开接口）
    // 将原始字节转换为 AVR 格式字符串后注入 UART 缓冲区
    // 实际上我们直接调用 adsb_inject_raw_message()
    adsb_inject_raw_message(msg, msg_bytes);

    ESP_LOGD(TAG, "RMT decoded DF%d, %d bytes, ICAO=%02X%02X%02X",
             df, msg_bytes, msg[1], msg[2], msg[3]);
}

// ---------------------------------------------------------------------------
// RMT 接收处理任务
// ---------------------------------------------------------------------------
static void adsb_rmt_task(void *pvParameters)
{
    ESP_LOGI(TAG, "RMT ADS-B receiver task started on GPIO%d", ADSB_SIGNAL_PIN);

    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 200,              // 最小脉冲 200ns（滤除噪声）
        .signal_range_max_ns = RMT_IDLE_US * 1000, // 300μs 空闲结束
    };

    rmt_rx_done_event_data_t rx_data;

    while (1) {
        // 启动一次接收
        ESP_ERROR_CHECK(rmt_receive(s_rx_chan, s_raw_symbols,
                                    sizeof(s_raw_symbols), &receive_config));

        // 等待接收完成
        if (xQueueReceive(s_rmt_queue, &rx_data, pdMS_TO_TICKS(500)) == pdTRUE) {
            process_rmt_frame(rx_data.received_symbols, rx_data.num_symbols);
        }
    }
}

// ---------------------------------------------------------------------------
// 公开 API
// ---------------------------------------------------------------------------

esp_err_t adsb_rmt_init(void)
{
    ESP_LOGI(TAG, "Initializing RMT ADS-B receiver on GPIO%d", ADSB_SIGNAL_PIN);
    ESP_LOGI(TAG, "Resolution: %dHz (%.1fus/tick)", RMT_RESOLUTION_HZ,
             1e6f / RMT_RESOLUTION_HZ);

    // 创建接收队列
    s_rmt_queue = xQueueCreate(4, sizeof(rmt_rx_done_event_data_t));
    if (!s_rmt_queue) {
        ESP_LOGE(TAG, "Failed to create RMT queue");
        return ESP_ERR_NO_MEM;
    }

    // 配置 RMT 接收通道
    rmt_rx_channel_config_t rx_chan_config = {
        .clk_src         = RMT_CLK_SRC_DEFAULT,
        .resolution_hz   = RMT_RESOLUTION_HZ,
        .mem_block_symbols = RMT_MEM_SYMBOLS,
        .gpio_num        = ADSB_SIGNAL_PIN,
        .flags.invert_in = false,
        .flags.with_dma  = false,
    };

    esp_err_t ret = rmt_new_rx_channel(&rx_chan_config, &s_rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT RX channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // 注册接收完成回调
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_cb,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(s_rx_chan, &cbs, s_rmt_queue));

    // 使能通道
    ESP_ERROR_CHECK(rmt_enable(s_rx_chan));

    // 启动处理任务
    xTaskCreate(adsb_rmt_task, "adsb_rmt", 4096, NULL, 6, NULL);

    ESP_LOGI(TAG, "RMT ADS-B receiver initialized");
    ESP_LOGI(TAG, "Hardware needed:");
    ESP_LOGI(TAG, "  1090MHz SAW filter (EPCOS B3588)");
    ESP_LOGI(TAG, "  LNA module (SPF5189Z)");
    ESP_LOGI(TAG, "  Envelope detector (BAT54 + 100pF + 1kohm)");
    ESP_LOGI(TAG, "  Comparator (LM393)");

    return ESP_OK;
}