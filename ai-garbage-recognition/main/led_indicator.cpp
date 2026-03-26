/**
 * led_indicator.cpp
 * LED 指示灯模块实现 / LED Indicator Implementation
 *
 * 使用 ESP-IDF GPIO 驱动控制 5 个 LED:
 *   - 4个垃圾类型 LED (红/蓝/绿/黄)
 *   - 1个系统状态 LED (白)
 */

#include "led_indicator.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "LED";

// LED GPIO 列表
static const int LED_GPIOS[] = {
    LED_HAZARDOUS_GPIO,   // 0: 有害垃圾 (红)
    LED_RECYCLABLE_GPIO,  // 1: 可回收物 (蓝)
    LED_KITCHEN_GPIO,     // 2: 厨余垃圾 (绿)
    LED_OTHER_GPIO,       // 3: 其他垃圾 (黄)
    LED_STATUS_GPIO,      // 4: 状态 (白)
};
#define LED_COUNT  5
#define LED_STATUS_IDX  4

static bool s_initialized = false;
static led_status_t s_current_status = LED_STATUS_OFF;
static uint32_t s_blink_counter = 0;

// ============================================================
// 内部函数 / Internal Functions
// ============================================================

static void led_set_gpio(int gpio, bool on)
{
    gpio_set_level((gpio_num_t)gpio, on ? 1 : 0);
}

// ============================================================
// 公共接口实现 / Public Interface Implementation
// ============================================================

esp_err_t led_init(void)
{
    if (s_initialized) return ESP_OK;

    // 配置所有 LED GPIO 为输出模式
    gpio_config_t io_conf = {
        .pin_bit_mask = 0,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    for (int i = 0; i < LED_COUNT; i++) {
        io_conf.pin_bit_mask = (1ULL << LED_GPIOS[i]);
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "GPIO%d 配置失败: %d", LED_GPIOS[i], ret);
            return ret;
        }
        gpio_set_level((gpio_num_t)LED_GPIOS[i], 0);  // 初始关闭
    }

    s_initialized = true;
    ESP_LOGI(TAG, "LED 初始化完成 (有害=%d 可回收=%d 厨余=%d 其他=%d 状态=%d)",
             LED_HAZARDOUS_GPIO, LED_RECYCLABLE_GPIO,
             LED_KITCHEN_GPIO, LED_OTHER_GPIO, LED_STATUS_GPIO);
    return ESP_OK;
}

void led_self_test(void)
{
    if (!s_initialized) return;

    ESP_LOGI(TAG, "LED 自检...");

    // 依次点亮每个 LED
    for (int i = 0; i < LED_COUNT; i++) {
        led_set_gpio(LED_GPIOS[i], true);
        vTaskDelay(pdMS_TO_TICKS(200));
        led_set_gpio(LED_GPIOS[i], false);
    }

    // 全部点亮
    for (int i = 0; i < LED_COUNT; i++) {
        led_set_gpio(LED_GPIOS[i], true);
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // 全部关闭
    for (int i = 0; i < LED_COUNT; i++) {
        led_set_gpio(LED_GPIOS[i], false);
    }

    ESP_LOGI(TAG, "LED 自检完成");
}

void led_set_status(led_status_t status)
{
    s_current_status = status;
    s_blink_counter = 0;
}

void led_set_garbage_type(garbage_type_t type)
{
    if (!s_initialized) return;

    // 关闭所有垃圾类型 LED
    led_set_gpio(LED_HAZARDOUS_GPIO,  false);
    led_set_gpio(LED_RECYCLABLE_GPIO, false);
    led_set_gpio(LED_KITCHEN_GPIO,    false);
    led_set_gpio(LED_OTHER_GPIO,      false);

    // 点亮对应 LED
    switch (type) {
        case GARBAGE_HAZARDOUS:
            led_set_gpio(LED_HAZARDOUS_GPIO, true);
            ESP_LOGI(TAG, "有害垃圾 LED 亮起 (红)");
            break;
        case GARBAGE_RECYCLABLE:
            led_set_gpio(LED_RECYCLABLE_GPIO, true);
            ESP_LOGI(TAG, "可回收物 LED 亮起 (蓝)");
            break;
        case GARBAGE_KITCHEN:
            led_set_gpio(LED_KITCHEN_GPIO, true);
            ESP_LOGI(TAG, "厨余垃圾 LED 亮起 (绿)");
            break;
        case GARBAGE_OTHER:
            led_set_gpio(LED_OTHER_GPIO, true);
            ESP_LOGI(TAG, "其他垃圾 LED 亮起 (黄)");
            break;
        default:
            break;
    }
}

void led_clear_garbage(void)
{
    if (!s_initialized) return;
    led_set_gpio(LED_HAZARDOUS_GPIO,  false);
    led_set_gpio(LED_RECYCLABLE_GPIO, false);
    led_set_gpio(LED_KITCHEN_GPIO,    false);
    led_set_gpio(LED_OTHER_GPIO,      false);
}

void led_update(void)
{
    if (!s_initialized) return;

    s_blink_counter++;

    switch (s_current_status) {
        case LED_STATUS_OFF:
            led_set_gpio(LED_STATUS_GPIO, false);
            break;

        case LED_STATUS_READY:
            // 慢闪: 每 1000ms 切换一次 (update 每 10ms 调用)
            led_set_gpio(LED_STATUS_GPIO, (s_blink_counter % 100) < 50);
            break;

        case LED_STATUS_DETECTING:
            // 快闪: 每 200ms 切换一次
            led_set_gpio(LED_STATUS_GPIO, (s_blink_counter % 20) < 10);
            break;

        case LED_STATUS_CLASSIFYING:
        case LED_STATUS_SORTING:
            // 常亮
            led_set_gpio(LED_STATUS_GPIO, true);
            break;

        case LED_STATUS_ERROR:
            // 双闪: 快闪两次后暂停
            {
                int phase = s_blink_counter % 60;
                bool on = (phase < 8) || (phase >= 12 && phase < 20);
                led_set_gpio(LED_STATUS_GPIO, on);
            }
            break;
    }
}