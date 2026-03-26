/**
 * AI 睡前故事机 - 主程序
 * ESP32-S3 主控制逻辑
 *
 * 功能流程:
 * 1. 初始化硬件（屏幕、音频、WiFi）
 * 2. 连接 WiFi 和 WebSocket 服务器
 * 3. 等待按钮触发录音
 * 4. 发送音频到服务器识别关键词
 * 5. 接收故事文本和音频流
 * 6. 播放故事并显示插图
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "config.h"
#include "wifi_manager.h"
#include "websocket_client.h"
#include "audio_input.h"
#include "audio_output.h"
#include "display.h"
#include "story_player.h"

static const char *TAG = "MAIN";

// 全局状态
static device_state_t g_device_state = STATE_INIT;
static QueueHandle_t g_message_queue = NULL;

// 事件组
static EventGroupHandle_t g_event_group = NULL;
#define WIFI_CONNECTED_BIT      BIT0
#define WS_CONNECTED_BIT        BIT1
#define BUTTON_PRESSED_BIT      BIT2
#define STORY_COMPLETE_BIT      BIT3

/**
 * 设置设备状态并更新显示
 */
void set_device_state(device_state_t new_state) {
    g_device_state = new_state;
    ESP_LOGI(TAG, "状态切换: %d", new_state);

    switch (new_state) {
        case STATE_WIFI_CONNECTING:
            display_show_status("连接 WiFi...", "请稍候", DISPLAY_ACCENT_COLOR);
            break;
        case STATE_WIFI_CONNECTED:
            display_show_status("WiFi 已连接", "正在连接服务器...", DISPLAY_ACCENT_COLOR);
            break;
        case STATE_WS_CONNECTING:
            display_show_status("连接服务器...", SERVER_HOST, DISPLAY_ACCENT_COLOR);
            break;
        case STATE_IDLE:
            display_show_welcome();
            break;
        case STATE_LISTENING:
            display_show_listening();
            break;
        case STATE_PROCESSING:
            display_show_processing();
            break;
        case STATE_PLAYING:
            // 由故事播放器控制显示
            break;
        case STATE_ERROR:
            display_show_error("发生错误", "请重启设备");
            break;
        case STATE_SLEEPING:
            display_show_sleeping();
            break;
        default:
            break;
    }
}

/**
 * 按钮中断处理
 */
static void IRAM_ATTR button_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    device_message_t msg = {
        .type = MSG_BUTTON_PRESSED,
        .data = NULL,
        .data_len = 0
    };
    xQueueSendFromISR(g_message_queue, &msg, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * WiFi 事件回调
 */
void on_wifi_connected(void) {
    ESP_LOGI(TAG, "WiFi 连接成功");
    set_device_state(STATE_WIFI_CONNECTED);
    xEventGroupSetBits(g_event_group, WIFI_CONNECTED_BIT);
}

void on_wifi_disconnected(void) {
    ESP_LOGW(TAG, "WiFi 断开连接");
    xEventGroupClearBits(g_event_group, WIFI_CONNECTED_BIT);
    xEventGroupClearBits(g_event_group, WS_CONNECTED_BIT);
    set_device_state(STATE_WIFI_CONNECTING);
}

/**
 * WebSocket 事件回调
 */
void on_ws_connected(void) {
    ESP_LOGI(TAG, "WebSocket 连接成功");
    set_device_state(STATE_IDLE);
    xEventGroupSetBits(g_event_group, WS_CONNECTED_BIT);
}

void on_ws_disconnected(void) {
    ESP_LOGW(TAG, "WebSocket 断开连接");
    xEventGroupClearBits(g_event_group, WS_CONNECTED_BIT);
    set_device_state(STATE_WS_CONNECTING);
}

void on_ws_data(const char *data, size_t len) {
    // 将数据传递给故事播放器处理
    story_player_handle_message(data, len);
}

/**
 * 录音任务
 */
static void recording_task(void *pvParameters) {
    ESP_LOGI(TAG, "开始录音任务");
    set_device_state(STATE_LISTENING);

    // 开始录音
    audio_input_start();

    uint8_t *audio_buffer = NULL;
    size_t audio_size = 0;

    // 录音（带静音检测，最长 AUDIO_RECORD_MAX_MS 毫秒）
    bool success = audio_input_record(
        &audio_buffer,
        &audio_size,
        AUDIO_RECORD_MAX_MS,
        AUDIO_SILENCE_THRESHOLD,
        AUDIO_SILENCE_MS
    );

    audio_input_stop();

    if (success && audio_size > 0) {
        ESP_LOGI(TAG, "录音完成，大小: %d bytes", audio_size);
        set_device_state(STATE_PROCESSING);

        // 发送音频到服务器
        ws_send_audio(audio_buffer, audio_size, MIC_SAMPLE_RATE);
    } else {
        ESP_LOGW(TAG, "录音失败或无声音");
        set_device_state(STATE_IDLE);
    }

    if (audio_buffer) {
        free(audio_buffer);
    }

    vTaskDelete(NULL);
}

/**
 * 主任务
 */
static void main_task(void *pvParameters) {
    ESP_LOGI(TAG, "主任务启动");

    device_message_t msg;

    while (1) {
        // 等待消息
        if (xQueueReceive(g_message_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (msg.type) {
                case MSG_BUTTON_PRESSED:
                    ESP_LOGI(TAG, "按钮按下");
                    if (g_device_state == STATE_IDLE) {
                        // 检查 WebSocket 连接
                        EventBits_t bits = xEventGroupGetBits(g_event_group);
                        if (bits & WS_CONNECTED_BIT) {
                            // 启动录音任务
                            xTaskCreate(
                                recording_task,
                                "recording",
                                TASK_AUDIO_IN_STACK,
                                NULL,
                                TASK_AUDIO_IN_PRIORITY,
                                NULL
                            );
                        } else {
                            display_show_error("未连接服务器", "请检查网络");
                            vTaskDelay(pdMS_TO_TICKS(2000));
                            set_device_state(STATE_IDLE);
                        }
                    } else if (g_device_state == STATE_PLAYING) {
                        // 停止播放
                        story_player_stop();
                        audio_output_stop();
                        set_device_state(STATE_IDLE);
                    }
                    break;

                case MSG_WS_DATA:
                    // WebSocket 数据由回调处理
                    break;

                case MSG_STORY_COMPLETE:
                    ESP_LOGI(TAG, "故事播放完成");
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    set_device_state(STATE_SLEEPING);
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    set_device_state(STATE_IDLE);
                    break;

                case MSG_ERROR:
                    ESP_LOGE(TAG, "发生错误");
                    set_device_state(STATE_ERROR);
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    set_device_state(STATE_IDLE);
                    break;

                default:
                    break;
            }

            // 释放消息数据
            if (msg.data) {
                free(msg.data);
            }
        }

        // 定期检查连接状态
        EventBits_t bits = xEventGroupGetBits(g_event_group);
        if (!(bits & WS_CONNECTED_BIT) && (bits & WIFI_CONNECTED_BIT)) {
            if (g_device_state != STATE_WS_CONNECTING) {
                set_device_state(STATE_WS_CONNECTING);
            }
        }
    }
}

/**
 * 初始化按钮
 */
static void init_button(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BTN_RECORD_PIN),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BTN_RECORD_PIN, button_isr_handler, NULL);
    ESP_LOGI(TAG, "按钮初始化完成，GPIO %d", BTN_RECORD_PIN);
}

/**
 * 应用程序入口
 */
void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  AI 睡前故事机 v1.0");
    ESP_LOGI(TAG, "  ESP32-S3 固件");
    ESP_LOGI(TAG, "========================================");

    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 创建消息队列
    g_message_queue = xQueueCreate(20, sizeof(device_message_t));
    if (!g_message_queue) {
        ESP_LOGE(TAG, "创建消息队列失败");
        return;
    }

    // 创建事件组
    g_event_group = xEventGroupCreate();
    if (!g_event_group) {
        ESP_LOGE(TAG, "创建事件组失败");
        return;
    }

    // 初始化显示屏
    ESP_LOGI(TAG, "初始化显示屏...");
    display_init();
    display_show_boot_screen();
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 初始化音频输入（麦克风）
    ESP_LOGI(TAG, "初始化麦克风...");
    audio_input_init();

    // 初始化音频输出（扬声器）
    ESP_LOGI(TAG, "初始化扬声器...");
    audio_output_init();

    // 初始化故事播放器
    ESP_LOGI(TAG, "初始化故事播放器...");
    story_player_init(g_message_queue);

    // 初始化按钮
    init_button();

    // 初始化 WiFi
    ESP_LOGI(TAG, "初始化 WiFi...");
    set_device_state(STATE_WIFI_CONNECTING);
    wifi_manager_init(on_wifi_connected, on_wifi_disconnected);
    wifi_manager_connect(WIFI_SSID, WIFI_PASSWORD);

    // 等待 WiFi 连接
    EventBits_t bits = xEventGroupWaitBits(
        g_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(30000)
    );

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WiFi 连接超时");
        display_show_error("WiFi 连接失败", "请检查配置");
        // 继续运行，等待重连
    }

    // 初始化 WebSocket 客户端
    ESP_LOGI(TAG, "初始化 WebSocket 客户端...");
    set_device_state(STATE_WS_CONNECTING);
    ws_client_init(
        SERVER_WS_URI,
        on_ws_connected,
        on_ws_disconnected,
        on_ws_data
    );
    ws_client_start();

    // 等待 WebSocket 连接
    bits = xEventGroupWaitBits(
        g_event_group,
        WS_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(15000)
    );

    if (!(bits & WS_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "WebSocket 连接超时，将在后台重试");
    }

    // 启动主任务
    xTaskCreate(
        main_task,
        "main_task",
        TASK_MAIN_PRIORITY * 1024,
        NULL,
        TASK_MAIN_PRIORITY,
        NULL
    );

    ESP_LOGI(TAG, "系统初始化完成！按下按钮开始录音");
}