/**
 * @file turn_signal.c
 * @brief 车辆转向灯检测模块
 *
 * 通过 GPIO 中断检测左右转向灯信号，支持防抖处理。
 * 转向灯信号通常为 1~2Hz 的闪烁信号，本模块检测高电平状态。
 */

#include "turn_signal.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>

static const char *TAG = "turn_sig";

/* ============================================================
 * 内部状态
 * ============================================================ */
static struct {
    turn_signal_config_t config;
    turn_signal_state_t  state;
    bool                 initialized;

    /* 防抖定时器 */
    TimerHandle_t debounce_timer_left;
    TimerHandle_t debounce_timer_right;

    /* 原始 GPIO 电平 */
    volatile bool raw_left;
    volatile bool raw_right;
} s_ts = {
    .state       = TURN_SIGNAL_NONE,
    .initialized = false,
};

/* ============================================================
 * 内部函数
 * ============================================================ */

static void update_state(void)
{
    turn_signal_state_t new_state = TURN_SIGNAL_NONE;

    if (s_ts.raw_left && s_ts.raw_right) {
        new_state = TURN_SIGNAL_BOTH;
    } else if (s_ts.raw_left) {
        new_state = TURN_SIGNAL_LEFT;
    } else if (s_ts.raw_right) {
        new_state = TURN_SIGNAL_RIGHT;
    }

    if (new_state != s_ts.state) {
        ESP_LOGI(TAG, "转向灯状态: %s -> %s",
                 turn_signal_state_name(s_ts.state),
                 turn_signal_state_name(new_state));
        s_ts.state = new_state;

        if (s_ts.config.callback) {
            s_ts.config.callback(new_state, s_ts.config.user_data);
        }
    }
}

static void debounce_timer_cb_left(TimerHandle_t xTimer)
{
    (void)xTimer;
    s_ts.raw_left = (gpio_get_level(s_ts.config.pin_left) == 1);
    update_state();
}

static void debounce_timer_cb_right(TimerHandle_t xTimer)
{
    (void)xTimer;
    s_ts.raw_right = (gpio_get_level(s_ts.config.pin_right) == 1);
    update_state();
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    int pin = (int)(intptr_t)arg;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (pin == s_ts.config.pin_left) {
        xTimerResetFromISR(s_ts.debounce_timer_left, &higher_priority_task_woken);
    } else if (pin == s_ts.config.pin_right) {
        xTimerResetFromISR(s_ts.debounce_timer_right, &higher_priority_task_woken);
    }

    if (higher_priority_task_woken) {
        portYIELD_FROM_ISR();
    }
}

/* ============================================================
 * 公共接口实现
 * ============================================================ */

esp_err_t turn_signal_init(const turn_signal_config_t *config)
{
    if (s_ts.initialized) {
        ESP_LOGW(TAG, "转向灯检测已初始化");
        return ESP_OK;
    }
    if (!config) return ESP_ERR_INVALID_ARG;

    memcpy(&s_ts.config, config, sizeof(turn_signal_config_t));
    s_ts.state = TURN_SIGNAL_NONE;

    /* ---- 配置 GPIO ---- */
    gpio_config_t io_conf = {
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLDOWN_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };

    if (config->pin_left >= 0) {
        io_conf.pin_bit_mask = (1ULL << config->pin_left);
        gpio_config(&io_conf);
    }

    if (config->pin_right >= 0) {
        io_conf.pin_bit_mask = (1ULL << config->pin_right);
        gpio_config(&io_conf);
    }

    /* ---- 创建防抖定时器 ---- */
    int debounce_ticks = pdMS_TO_TICKS(config->debounce_ms > 0 ? config->debounce_ms : 50);

    s_ts.debounce_timer_left = xTimerCreate(
        "ts_left", debounce_ticks, pdFALSE,
        NULL, debounce_timer_cb_left);

    s_ts.debounce_timer_right = xTimerCreate(
        "ts_right", debounce_ticks, pdFALSE,
        NULL, debounce_timer_cb_right);

    if (!s_ts.debounce_timer_left || !s_ts.debounce_timer_right) {
        ESP_LOGE(TAG, "创建防抖定时器失败");
        return ESP_ERR_NO_MEM;
    }

    /* ---- 安装 GPIO 中断服务 ---- */
    gpio_install_isr_service(0);

    if (config->pin_left >= 0) {
        gpio_isr_handler_add(config->pin_left, gpio_isr_handler,
                             (void *)(intptr_t)config->pin_left);
    }
    if (config->pin_right >= 0) {
        gpio_isr_handler_add(config->pin_right, gpio_isr_handler,
                             (void *)(intptr_t)config->pin_right);
    }

    /* 读取初始状态 */
    s_ts.raw_left  = (config->pin_left  >= 0) && (gpio_get_level(config->pin_left)  == 1);
    s_ts.raw_right = (config->pin_right >= 0) && (gpio_get_level(config->pin_right) == 1);
    update_state();

    s_ts.initialized = true;
    ESP_LOGI(TAG, "转向灯检测初始化完成 (左GPIO=%d, 右GPIO=%d)",
             config->pin_left, config->pin_right);
    return ESP_OK;
}

void turn_signal_deinit(void)
{
    if (!s_ts.initialized) return;

    if (s_ts.config.pin_left >= 0) {
        gpio_isr_handler_remove(s_ts.config.pin_left);
    }
    if (s_ts.config.pin_right >= 0) {
        gpio_isr_handler_remove(s_ts.config.pin_right);
    }

    if (s_ts.debounce_timer_left)  xTimerDelete(s_ts.debounce_timer_left, 0);
    if (s_ts.debounce_timer_right) xTimerDelete(s_ts.debounce_timer_right, 0);

    s_ts.initialized = false;
    ESP_LOGI(TAG, "转向灯检测已释放");
}

turn_signal_state_t turn_signal_get_state(void)
{
    return s_ts.state;
}

bool turn_signal_is_left(void)
{
    return (s_ts.state == TURN_SIGNAL_LEFT || s_ts.state == TURN_SIGNAL_BOTH);
}

bool turn_signal_is_right(void)
{
    return (s_ts.state == TURN_SIGNAL_RIGHT || s_ts.state == TURN_SIGNAL_BOTH);
}

const char *turn_signal_state_name(turn_signal_state_t state)
{
    switch (state) {
        case TURN_SIGNAL_NONE:  return "无";
        case TURN_SIGNAL_LEFT:  return "左转";
        case TURN_SIGNAL_RIGHT: return "右转";
        case TURN_SIGNAL_BOTH:  return "双闪";
        default:                return "未知";
    }
}