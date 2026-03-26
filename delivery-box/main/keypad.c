/**
 * @file keypad.c
 * @brief 4x4 矩阵键盘驱动实现
 *
 * 扫描原理:
 *   行引脚设为输出，列引脚设为输入(内部上拉)
 *   依次将每行拉低，检测哪列变为低电平
 */

#include "keypad.h"
#include "config.h"
#include "buzzer.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "keypad";

// 按键事件队列 (容量16)
static QueueHandle_t s_key_queue = NULL;

// ============================================================
// 初始化
// ============================================================

esp_err_t keypad_init(void)
{
    // 创建按键队列
    s_key_queue = xQueueCreate(16, sizeof(char));
    if (!s_key_queue) {
        ESP_LOGE(TAG, "创建按键队列失败");
        return ESP_ERR_NO_MEM;
    }

    // 配置行引脚为输出
    gpio_config_t row_conf = {
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    for (int r = 0; r < KEYPAD_ROWS; r++) {
        row_conf.pin_bit_mask = (1ULL << KEYPAD_ROW_GPIOS[r]);
        gpio_config(&row_conf);
        gpio_set_level(KEYPAD_ROW_GPIOS[r], 1); // 默认高电平
    }

    // 配置列引脚为输入 (内部上拉)
    gpio_config_t col_conf = {
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    for (int c = 0; c < KEYPAD_COLS; c++) {
        col_conf.pin_bit_mask = (1ULL << KEYPAD_COL_GPIOS[c]);
        gpio_config(&col_conf);
    }

    ESP_LOGI(TAG, "键盘初始化完成");
    return ESP_OK;
}

// ============================================================
// 键盘扫描
// ============================================================

/**
 * @brief 扫描键盘，返回当前按下的键
 * @return 按键字符, KEYPAD_NO_KEY 无按键
 */
static char scan_keypad(void)
{
    for (int r = 0; r < KEYPAD_ROWS; r++) {
        // 将当前行拉低
        gpio_set_level(KEYPAD_ROW_GPIOS[r], 0);

        // 短暂延时让电平稳定
        for (volatile int d = 0; d < 100; d++);

        // 检测各列
        for (int c = 0; c < KEYPAD_COLS; c++) {
            if (gpio_get_level(KEYPAD_COL_GPIOS[c]) == 0) {
                // 检测到按键
                gpio_set_level(KEYPAD_ROW_GPIOS[r], 1);
                return KEYPAD_MAP[r][c];
            }
        }

        // 恢复高电平
        gpio_set_level(KEYPAD_ROW_GPIOS[r], 1);
    }
    return (char)KEYPAD_NO_KEY;
}

// ============================================================
// 键盘扫描任务
// ============================================================

void keypad_task(void *arg)
{
    ESP_LOGI(TAG, "键盘扫描任务启动");

    char last_key = (char)KEYPAD_NO_KEY;
    uint32_t press_time = 0;
    bool key_sent = false;

    while (1) {
        char key = scan_keypad();

        if (key != (char)KEYPAD_NO_KEY) {
            if (key != last_key) {
                // 新按键按下
                last_key = key;
                press_time = xTaskGetTickCount();
                key_sent = false;
            } else if (!key_sent) {
                // 检查消抖时间
                uint32_t elapsed = (xTaskGetTickCount() - press_time) * portTICK_PERIOD_MS;
                if (elapsed >= KEYPAD_DEBOUNCE_MS) {
                    // 发送按键事件
                    xQueueSend(s_key_queue, &key, 0);
                    key_sent = true;
                    ESP_LOGD(TAG, "按键: '%c'", key);
                }
            }
        } else {
            // 无按键，重置状态
            last_key = (char)KEYPAD_NO_KEY;
            key_sent = false;
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms 扫描间隔
    }
}

// ============================================================
// API
// ============================================================

QueueHandle_t keypad_get_queue(void)
{
    return s_key_queue;
}

char keypad_wait_key(uint32_t timeout_ms)
{
    char key = (char)KEYPAD_NO_KEY;
    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY
                                         : pdMS_TO_TICKS(timeout_ms);
    xQueueReceive(s_key_queue, &key, ticks);
    return key;
}

char keypad_get_key(void)
{
    char key = (char)KEYPAD_NO_KEY;
    xQueueReceive(s_key_queue, &key, 0);
    return key;
}