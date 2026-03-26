/**
 * @file main.c
 * @brief 深度睡眠示例
 *
 * 功能：
 *   - 定时器唤醒（每 10 秒唤醒一次）
 *   - GPIO 唤醒（按键唤醒）
 *   - 记录唤醒原因
 *   - RTC 内存保存数据（睡眠期间不丢失）
 *
 * 深度睡眠功耗：约 10~150 μA（取决于配置）
 * 正常运行功耗：约 160~260 mA
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "DeepSleep";

/* 睡眠配置 */
#define SLEEP_DURATION_US   (10 * 1000000)  // 10 秒
#define WAKEUP_GPIO         GPIO_NUM_0       // BOOT 按键

/* RTC 内存（深度睡眠期间保持数据） */
RTC_DATA_ATTR static int s_boot_count = 0;
RTC_DATA_ATTR static int64_t s_last_wake_time = 0;

/**
 * @brief 打印唤醒原因
 */
static void print_wakeup_reason(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
        ESP_LOGI(TAG, "唤醒原因: 定时器");
        break;
    case ESP_SLEEP_WAKEUP_EXT0:
        ESP_LOGI(TAG, "唤醒原因: GPIO%d 外部中断", WAKEUP_GPIO);
        break;
    case ESP_SLEEP_WAKEUP_EXT1:
        ESP_LOGI(TAG, "唤醒原因: EXT1 外部中断");
        break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        ESP_LOGI(TAG, "唤醒原因: 触摸板");
        break;
    case ESP_SLEEP_WAKEUP_ULP:
        ESP_LOGI(TAG, "唤醒原因: ULP 协处理器");
        break;
    default:
        ESP_LOGI(TAG, "唤醒原因: 上电复位或其他 (%d)", cause);
        break;
    }
}

void app_main(void)
{
    s_boot_count++;

    ESP_LOGI(TAG, "深度睡眠示例启动");
    ESP_LOGI(TAG, "启动次数: %d（RTC 内存保存）", s_boot_count);

    /* 打印唤醒原因 */
    print_wakeup_reason();

    /* 计算睡眠时长 */
    int64_t now = esp_timer_get_time() / 1000;
    if (s_last_wake_time > 0) {
        ESP_LOGI(TAG, "距上次唤醒: %lld ms", now - s_last_wake_time);
    }
    s_last_wake_time = now;

    /* 模拟工作：采集数据、发送数据等 */
    ESP_LOGI(TAG, "正在执行工作任务...");
    vTaskDelay(pdMS_TO_TICKS(2000));  // 工作 2 秒

    /* 配置唤醒源 */

    /* 方式1：定时器唤醒 */
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US));
    ESP_LOGI(TAG, "定时器唤醒已配置: %lld 秒后唤醒",
             SLEEP_DURATION_US / 1000000);

    /* 方式2：GPIO 唤醒（低电平触发） */
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0));
    ESP_LOGI(TAG, "GPIO%d 唤醒已配置（低电平触发）", WAKEUP_GPIO);

    /* 进入深度睡眠 */
    ESP_LOGI(TAG, "进入深度睡眠...");
    ESP_LOGI(TAG, "按 GPIO%d 按键或等待 %lld 秒唤醒",
             WAKEUP_GPIO, SLEEP_DURATION_US / 1000000);

    vTaskDelay(pdMS_TO_TICKS(100));  // 等待日志输出完成
    esp_deep_sleep_start();

    /* 以下代码不会执行（深度睡眠后重启） */
    ESP_LOGI(TAG, "这行不会执行");
}