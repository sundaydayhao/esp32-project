/**
 * @file main.cpp
 * @brief AI玩具主程序入口
 * 
 * 基于ESP32-S3的AI语音玩具，参考虾哥小智方案
 * 
 * 功能流程:
 * 1. 初始化硬件 (LED, 按键, 音频)
 * 2. 连接WiFi
 * 3. 连接WebSocket服务器
 * 4. 等待唤醒词或按键触发
 * 5. 录音 → 发送 → 接收TTS → 播放
 */

#include "app_config.h"
#include "led_indicator.h"
#include "button_handler.h"
#include "wifi_manager.h"
#include "ai_assistant.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

// ============================================================
// 回调函数
// ============================================================

/**
 * @brief WiFi状态变化回调
 */
static void on_wifi_status(wifi_status_t status)
{
    switch (status) {
    case WIFI_STATUS_CONNECTING:
        ESP_LOGI(TAG, "WiFi连接中...");
        led_indicator_set_state(LED_STATE_CONNECTING);
        break;

    case WIFI_STATUS_CONNECTED:
        ESP_LOGI(TAG, "WiFi已连接");
        led_indicator_set_state(LED_STATE_CONNECTED);
        break;

    case WIFI_STATUS_DISCONNECTED:
        ESP_LOGW(TAG, "WiFi已断开");
        led_indicator_set_state(LED_STATE_CONNECTING);
        break;

    case WIFI_STATUS_FAILED:
        ESP_LOGE(TAG, "WiFi连接失败");
        led_indicator_set_state(LED_STATE_ERROR);
        break;
    }
}

/**
 * @brief 按键事件回调
 */
static void on_button_event(button_event_t event)
{
    switch (event) {
    case BUTTON_EVENT_PRESS:
        ESP_LOGI(TAG, "按键短按 - 触发唤醒/打断");
        ai_state_t state = ai_assistant_get_state();

        if (state == AI_STATE_IDLE) {
            // 空闲状态：唤醒
            ai_assistant_send_event(AI_EVENT_WAKEUP);
        } else if (state == AI_STATE_LISTENING) {
            // 录音中：手动停止录音
            ai_assistant_send_event(AI_EVENT_SPEECH_END);
        } else if (state == AI_STATE_SPEAKING || state == AI_STATE_PROCESSING) {
            // 播放/处理中：打断
            ai_assistant_send_event(AI_EVENT_INTERRUPT);
        }
        break;

    case BUTTON_EVENT_LONG_PRESS:
        ESP_LOGI(TAG, "按键长按 - 重置");
        ai_assistant_send_event(AI_EVENT_RESET);
        break;

    case BUTTON_EVENT_RELEASE:
        // 不处理释放事件
        break;
    }
}

// ============================================================
// 主函数
// ============================================================

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  AI玩具 v1.0 启动");
    ESP_LOGI(TAG, "  基于ESP32-S3 + 虾哥小智方案");
    ESP_LOGI(TAG, "========================================");

    // 打印系统信息
    ESP_LOGI(TAG, "芯片型号: %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "IDF版本: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "可用堆内存: %lu KB", esp_get_free_heap_size() / 1024);

    esp_err_t ret;

    // ---- 1. 初始化LED ----
    ret = led_indicator_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED初始化失败: %s", esp_err_to_name(ret));
        // LED失败不影响主功能，继续运行
    }
    led_indicator_set_state(LED_STATE_CONNECTING);

    // ---- 2. 初始化按键 ----
    ret = button_handler_init(on_button_event);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(ret));
    }

    // ---- 3. 初始化WiFi并连接 ----
    ret = wifi_manager_init(on_wifi_status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi管理器初始化失败: %s", esp_err_to_name(ret));
        led_indicator_set_state(LED_STATE_ERROR);
        // WiFi失败，重启
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    ret = wifi_manager_connect(WIFI_SSID, WIFI_PASSWORD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi连接启动失败: %s", esp_err_to_name(ret));
    }

    // 等待WiFi连接
    ESP_LOGI(TAG, "等待WiFi连接 (超时: %d ms)...", WIFI_CONNECT_TIMEOUT_MS);
    ret = wifi_manager_wait_connected(WIFI_CONNECT_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi连接超时或失败，重启...");
        led_indicator_set_state(LED_STATE_ERROR);
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    // 打印IP地址
    char ip_buf[16];
    wifi_manager_get_ip(ip_buf, sizeof(ip_buf));
    ESP_LOGI(TAG, "本机IP: %s", ip_buf);

    // ---- 4. 初始化AI助手 ----
    ret = ai_assistant_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AI助手初始化失败: %s", esp_err_to_name(ret));
        led_indicator_set_state(LED_STATE_ERROR);
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    // ---- 5. 启动完成 ----
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  系统启动完成！");
    ESP_LOGI(TAG, "  说 '你好小智' 或按下按键唤醒");
    ESP_LOGI(TAG, "========================================");

    led_indicator_set_state(LED_STATE_CONNECTED);

    // 主循环：监控系统状态
    uint32_t loop_count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        loop_count++;

        // 每30秒打印一次系统状态
        if (loop_count % 6 == 0) {
            ESP_LOGI(TAG, "系统状态 - 堆内存: %lu KB, AI状态: %d, WiFi: %d",
                     esp_get_free_heap_size() / 1024,
                     ai_assistant_get_state(),
                     wifi_manager_get_status());
        }

        // 检查WiFi是否断开，尝试重连
        if (wifi_manager_get_status() == WIFI_STATUS_DISCONNECTED ||
            wifi_manager_get_status() == WIFI_STATUS_FAILED) {
            ESP_LOGW(TAG, "WiFi断开，尝试重连...");
            wifi_manager_connect(WIFI_SSID, WIFI_PASSWORD);
        }
    }
}