/**
 * @file led_indicator.cpp
 * @brief WS2812B RGB LED 状态指示灯驱动
 * 
 * 使用ESP32-S3的RMT外设驱动WS2812B LED
 */

#include "led_indicator.h"
#include "app_config.h"

#include "esp_log.h"
#include "esp_err.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char *TAG = "LED";

// RMT 编码器句柄
static rmt_channel_handle_t s_led_chan = NULL;
static rmt_encoder_handle_t s_led_encoder = NULL;

// LED 任务句柄
static TaskHandle_t s_led_task_handle = NULL;
static led_state_t s_current_state = LED_STATE_OFF;
static volatile bool s_state_changed = false;

// WS2812B 时序参数 (单位: ns)
#define WS2812_T0H_NS   350
#define WS2812_T0L_NS   1000
#define WS2812_T1H_NS   1000
#define WS2812_T1L_NS   350
#define WS2812_RESET_US 280

// LED 颜色结构
typedef struct {
    uint8_t g;  // WS2812B 顺序: G, R, B
    uint8_t r;
    uint8_t b;
} ws2812_pixel_t;

static ws2812_pixel_t s_pixel = {0, 0, 0};

/**
 * @brief WS2812B RMT 编码器
 */
typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_encoder;
    rmt_encoder_t *copy_encoder;
    int state;
    rmt_symbol_word_t reset_code;
} rmt_led_strip_encoder_t;

static size_t rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                    const void *primary_data, size_t data_size,
                                    rmt_encode_state_t *ret_state)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = led_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = led_encoder->copy_encoder;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (led_encoder->state) {
    case 0:
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = 1;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    // fall-through
    case 1:
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &led_encoder->reset_code,
                                                  sizeof(led_encoder->reset_code), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            led_encoder->state = RMT_ENCODING_RESET;
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
    }
out:
    *ret_state = (rmt_encode_state_t)state;
    return encoded_symbols;
}

static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    free(led_encoder);
    return ESP_OK;
}

static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

static esp_err_t create_led_strip_encoder(rmt_encoder_handle_t *ret_encoder)
{
    rmt_led_strip_encoder_t *led_encoder = (rmt_led_strip_encoder_t *)calloc(1, sizeof(rmt_led_strip_encoder_t));
    if (!led_encoder) return ESP_ERR_NO_MEM;

    led_encoder->base.encode = rmt_encode_led_strip;
    led_encoder->base.del = rmt_del_led_strip_encoder;
    led_encoder->base.reset = rmt_led_strip_encoder_reset;

    // 字节编码器配置
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = (uint16_t)(WS2812_T0H_NS / 10),  // 单位: 10ns (100MHz)
            .level1 = 0,
            .duration1 = (uint16_t)(WS2812_T0L_NS / 10),
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = (uint16_t)(WS2812_T1H_NS / 10),
            .level1 = 0,
            .duration1 = (uint16_t)(WS2812_T1L_NS / 10),
        },
        .flags = {
            .msb_first = 1,
        },
    };
    rmt_new_bytes_encoder(&bytes_encoder_config, &led_encoder->bytes_encoder);

    // 复制编码器 (用于复位信号)
    rmt_copy_encoder_config_t copy_encoder_config = {};
    rmt_new_copy_encoder(&copy_encoder_config, &led_encoder->copy_encoder);

    // 复位信号
    uint32_t reset_ticks = (uint32_t)(WS2812_RESET_US * 1000 / 10);
    led_encoder->reset_code = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = reset_ticks / 2,
        .level1 = 0,
        .duration1 = reset_ticks / 2,
    };

    *ret_encoder = &led_encoder->base;
    return ESP_OK;
}

/**
 * @brief 发送像素数据到LED
 */
static void led_strip_write(ws2812_pixel_t *pixel)
{
    if (!s_led_chan || !s_led_encoder) return;

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    rmt_transmit(s_led_chan, s_led_encoder, pixel, sizeof(ws2812_pixel_t), &tx_config);
    rmt_tx_wait_all_done(s_led_chan, portMAX_DELAY);
}

/**
 * @brief 设置LED颜色
 */
static void set_led_color(uint8_t r, uint8_t g, uint8_t b)
{
    s_pixel.r = r;
    s_pixel.g = g;
    s_pixel.b = b;
    led_strip_write(&s_pixel);
}

/**
 * @brief LED动画任务
 */
static void led_task(void *arg)
{
    uint8_t brightness = 0;
    int8_t direction = 1;
    uint32_t blink_count = 0;

    while (1) {
        led_state_t state = s_current_state;

        switch (state) {
        case LED_STATE_OFF:
            set_led_color(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_STATE_IDLE:
        case LED_STATE_CONNECTED:
            // 绿色常亮 (低亮度)
            set_led_color(0, 30, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case LED_STATE_CONNECTING:
            // 蓝色慢闪
            blink_count++;
            if (blink_count % 2 == 0) {
                set_led_color(0, 0, 80);
            } else {
                set_led_color(0, 0, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case LED_STATE_WAKEUP:
            // 紫色快闪3次
            for (int i = 0; i < 3; i++) {
                set_led_color(80, 0, 80);
                vTaskDelay(pdMS_TO_TICKS(100));
                set_led_color(0, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            // 唤醒动画结束后保持紫色
            set_led_color(40, 0, 40);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;

        case LED_STATE_LISTENING:
            // 红色呼吸灯
            brightness += direction * 5;
            if (brightness >= 200) {
                brightness = 200;
                direction = -1;
            } else if (brightness <= 10) {
                brightness = 10;
                direction = 1;
            }
            set_led_color(brightness, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(30));
            break;

        case LED_STATE_PROCESSING:
            // 黄色快闪
            blink_count++;
            if (blink_count % 2 == 0) {
                set_led_color(80, 80, 0);
            } else {
                set_led_color(0, 0, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            break;

        case LED_STATE_SPEAKING:
            // 青色呼吸灯
            brightness += direction * 4;
            if (brightness >= 150) {
                brightness = 150;
                direction = -1;
            } else if (brightness <= 10) {
                brightness = 10;
                direction = 1;
            }
            set_led_color(0, brightness / 2, brightness);
            vTaskDelay(pdMS_TO_TICKS(30));
            break;

        case LED_STATE_ERROR:
            // 红色快闪
            blink_count++;
            if (blink_count % 2 == 0) {
                set_led_color(255, 0, 0);
            } else {
                set_led_color(0, 0, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        default:
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }
    }
}

// ============================================================
// 公共接口实现
// ============================================================

esp_err_t led_indicator_init(void)
{
    ESP_LOGI(TAG, "初始化LED指示灯 (GPIO %d)", LED_PIN);

    // 配置RMT TX通道
    rmt_tx_channel_config_t chan_config = {
        .gpio_num = (gpio_num_t)LED_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,  // 10MHz, 1 tick = 100ns
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };

    esp_err_t ret = rmt_new_tx_channel(&chan_config, &s_led_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建RMT通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 创建LED编码器
    ret = create_led_strip_encoder(&s_led_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建LED编码器失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 使能RMT通道
    ret = rmt_enable(s_led_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "使能RMT通道失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始关闭LED
    set_led_color(0, 0, 0);

    // 创建LED动画任务
    xTaskCreate(led_task, "led_task", TASK_STACK_LED, NULL, TASK_PRIORITY_LED, &s_led_task_handle);

    ESP_LOGI(TAG, "LED初始化完成");
    return ESP_OK;
}

void led_indicator_set_state(led_state_t state)
{
    if (s_current_state != state) {
        ESP_LOGD(TAG, "LED状态切换: %d -> %d", s_current_state, state);
        s_current_state = state;
    }
}

void led_indicator_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    set_led_color(r, g, b);
}

void led_indicator_off(void)
{
    s_current_state = LED_STATE_OFF;
    set_led_color(0, 0, 0);
}

void led_indicator_deinit(void)
{
    if (s_led_task_handle) {
        vTaskDelete(s_led_task_handle);
        s_led_task_handle = NULL;
    }
    if (s_led_encoder) {
        rmt_del_encoder(s_led_encoder);
        s_led_encoder = NULL;
    }
    if (s_led_chan) {
        rmt_disable(s_led_chan);
        rmt_del_channel(s_led_chan);
        s_led_chan = NULL;
    }
}