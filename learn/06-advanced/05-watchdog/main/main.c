/**
 * @file main.c
 * @brief 看门狗定时器示例
 *
 * 功能：
 *   - 任务看门狗（TWDT）：监控任务是否卡死
 *   - 中断看门狗（IWDT）：监控中断是否卡死
 *   - 演示正常喂狗和超时重启
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "Watchdog";

/* 看门狗超时时间（秒） */
#define WDT_TIMEOUT_S   5

/**
 * @brief 正常任务（定期喂狗）
 */
static void normal_task(void *pvParameters)
{
    ESP_LOGI(TAG, "正常任务启动，将定期喂狗");

    /* 将当前任务添加到看门狗监控 */
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    int count = 0;
    while (1) {
        count++;
        ESP_LOGI(TAG, "正常任务运行中... count=%d", count);

        /* 喂狗（重置看门狗计时器） */
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        ESP_LOGD(TAG, "喂狗成功");

        vTaskDelay(pdMS_TO_TICKS(1000));

        /* 演示：运行 15 次后模拟任务卡死 */
        if (count == 15) {
            ESP_LOGW(TAG, "模拟任务卡死（停止喂狗）...");
            ESP_LOGW(TAG, "看门狗将在 %d 秒后触发重启", WDT_TIMEOUT_S);

            /* 停止喂狗，等待看门狗触发 */
            while (1) {
                ESP_LOGW(TAG, "任务卡死中...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                /* 注意：这里故意不调用 esp_task_wdt_reset() */
            }
        }
    }
}

/**
 * @brief 看门狗超时回调（可选）
 */
static void wdt_timeout_callback(const esp_task_wdt_user_handle_t *user_handles,
                                   size_t num_handles, const esp_task_wdt_timeout_ctx_t *ctx)
{
    ESP_LOGE(TAG, "看门狗超时！即将重启...");
    /* 这里可以保存重要数据 */
}

void app_main(void)
{
    ESP_LOGI(TAG, "看门狗示例启动");

    /* 配置任务看门狗 */
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms     = WDT_TIMEOUT_S * 1000,
        .idle_core_mask = 0,    // 不监控 idle 任务
        .trigger_panic  = true, // 超时后触发 panic（重启）
    };
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&wdt_config));
    ESP_LOGI(TAG, "任务看门狗已配置，超时: %d 秒", WDT_TIMEOUT_S);

    /* 创建正常任务 */
    xTaskCreate(normal_task, "normal_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "任务已创建，等待看门狗演示...");
}