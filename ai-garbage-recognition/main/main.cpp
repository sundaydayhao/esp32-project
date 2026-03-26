/**
 * main.cpp
 * AI 智能垃圾分类系统 - 主程序入口
 * AI Garbage Recognition & Sorting System - Main Entry
 *
 * ESP32-S3 + ESP-IDF 框架
 *
 * 系统工作流程:
 *   1. 初始化所有硬件模块
 *   2. 等待摄像头检测到物体 (运动检测)
 *   3. 捕获图像并进行颜色分析
 *   4. 根据卡片颜色识别垃圾类型
 *   5. 控制舵机将垃圾投入对应垃圾桶
 *   6. 播放语音提示
 *   7. 复位，等待下一次检测
 */

#include "app_config.h"
#include "camera_handler.h"
#include "garbage_classifier.h"
#include "motor_control.h"
#include "audio_player.h"
#include "led_indicator.h"

#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "esp_timer.h"

static const char* TAG = "MAIN";

// ============================================================
// 系统状态机 / System State Machine
// ============================================================
static sys_state_t s_state = SYS_STATE_INIT;
static garbage_type_t s_last_type = GARBAGE_UNKNOWN;
static int64_t s_last_detection_us = 0;
static bool s_system_ok = false;

// LED 更新定时器
static esp_timer_handle_t s_led_timer = NULL;

// ============================================================
// LED 定时器回调 / LED Timer Callback
// ============================================================
static void led_timer_callback(void* arg)
{
    led_update();
}

// ============================================================
// 打印系统信息 / Print System Info
// ============================================================
static void print_banner(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   AI 智能垃圾分类系统  v%s         ║", APP_VERSION_STR);
    ESP_LOGI(TAG, "║   AI Garbage Sorting System          ║");
    ESP_LOGI(TAG, "║   ESP32-S3 + ESP-IDF                 ║");
    ESP_LOGI(TAG, "╠══════════════════════════════════════╣");
    ESP_LOGI(TAG, "║  卡片颜色对应:                       ║");
    ESP_LOGI(TAG, "║  红色 -> 有害垃圾                    ║");
    ESP_LOGI(TAG, "║  蓝色 -> 可回收物                    ║");
    ESP_LOGI(TAG, "║  绿色 -> 厨余垃圾                    ║");
    ESP_LOGI(TAG, "║  灰色 -> 其他垃圾                    ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
}

// ============================================================
// 初始化所有模块 / Initialize All Modules
// ============================================================
static esp_err_t system_init(void)
{
    esp_err_t ret;

    // 初始化 NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 擦除并重新初始化");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS 初始化失败 (非致命): %d", ret);
    }

    // [1/5] 初始化 LED
    ESP_LOGI(TAG, "[1/5] 初始化 LED 指示灯...");
    ret = led_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "  LED 初始化失败 (非致命)");
    } else {
        ESP_LOGI(TAG, "  LED 初始化成功");
        led_self_test();
    }

    // 启动 LED 定时器 (每 10ms 更新一次)
    esp_timer_create_args_t timer_args = {
        .callback        = led_timer_callback,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "led_timer",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&timer_args, &s_led_timer);
    esp_timer_start_periodic(s_led_timer, 10 * 1000);  // 10ms

    // [2/5] 初始化音频播放器
    ESP_LOGI(TAG, "[2/5] 初始化音频播放器 (DFPlayer Mini)...");
    ret = audio_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "  音频播放器初始化失败 (非致命，系统将无声音运行)");
    } else {
        ESP_LOGI(TAG, "  音频播放器初始化成功");
        // 播放系统就绪提示音
        vTaskDelay(pdMS_TO_TICKS(500));
        audio_play_track(AUDIO_TRACK_SYSTEM_READY);
    }

    // [3/5] 初始化电机
    ESP_LOGI(TAG, "[3/5] 初始化舵机电机...");
    ret = motor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "  电机初始化失败!");
        return ret;
    }
    ESP_LOGI(TAG, "  电机初始化成功");
    motor_reset_all();

    // [4/5] 初始化摄像头
    ESP_LOGI(TAG, "[4/5] 初始化摄像头 (OV2640)...");
    ret = camera_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "  摄像头初始化失败!");
        return ret;
    }
    ESP_LOGI(TAG, "  摄像头初始化成功");

    // [5/5] 初始化分类器
    ESP_LOGI(TAG, "[5/5] 初始化垃圾分类器...");
    ret = classifier_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "  分类器初始化失败!");
        return ret;
    }
    ESP_LOGI(TAG, "  分类器初始化成功");

    return ESP_OK;
}

// ============================================================
// 主状态机 / Main State Machine
// ============================================================

static void state_idle(void)
{
    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_ms = (now_us - s_last_detection_us) / 1000;

    // 检查冷却时间
    if (elapsed_ms < DETECTION_COOLDOWN_MS) {
        vTaskDelay(pdMS_TO_TICKS(50));
        return;
    }

    // 检测是否有物体放入
    if (camera_detect_object()) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, ">>> 检测到物体，开始识别...");
        led_set_status(LED_STATUS_DETECTING);
        s_state = SYS_STATE_DETECTING;
    } else {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void state_detecting(void)
{
    ESP_LOGI(TAG, "捕获图像...");

    // 稳定等待 (让物体放稳)
    vTaskDelay(pdMS_TO_TICKS(300));

    esp_err_t ret = camera_capture_frame();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "图像捕获失败，返回空闲");
        s_state = SYS_STATE_IDLE;
        return;
    }

    ESP_LOGI(TAG, "图像捕获成功");
    led_set_status(LED_STATUS_CLASSIFYING);
    s_state = SYS_STATE_CLASSIFYING;
}

static void state_classifying(void)
{
    ESP_LOGI(TAG, "识别垃圾类型...");

    classification_result_t result;
    esp_err_t ret = classifier_run(&result);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "分类失败，返回空闲");
        s_state = SYS_STATE_IDLE;
        return;
    }

    // 打印识别结果
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "┌─────────────────────────────────────┐");
    ESP_LOGI(TAG, "│  识别结果: %-24s │", classifier_get_name_cn(result.type));
    ESP_LOGI(TAG, "│  置信度:   %d%%                       │", result.confidence);
    ESP_LOGI(TAG, "│  颜色:     R=%-3d G=%-3d B=%-3d        │",
             result.avg_r, result.avg_g, result.avg_b);
    ESP_LOGI(TAG, "│  HSV:      H=%-5.1f S=%-4.2f V=%-4.2f  │",
             result.hue, result.saturation, result.value);
    ESP_LOGI(TAG, "└─────────────────────────────────────┘");

    if (result.type == GARBAGE_UNKNOWN || result.confidence < CONFIDENCE_THRESHOLD) {
        ESP_LOGW(TAG, "置信度不足 (%d%% < %d%%)，重新检测",
                 result.confidence, CONFIDENCE_THRESHOLD);
        audio_play_track(AUDIO_TRACK_UNKNOWN);
        s_last_detection_us = esp_timer_get_time();
        led_set_status(LED_STATUS_READY);
        s_state = SYS_STATE_IDLE;
        return;
    }

    s_last_type = result.type;
    s_last_detection_us = esp_timer_get_time();

    // 点亮对应 LED
    led_set_garbage_type(result.type);
    led_set_status(LED_STATUS_SORTING);

    s_state = SYS_STATE_SORTING;
}

static void state_sorting(void)
{
    ESP_LOGI(TAG, "分拣: 投入 %s", classifier_get_name_cn(s_last_type));

    // 控制电机分拣
    motor_sort_garbage(s_last_type);

    s_state = SYS_STATE_AUDIO;
}

static void state_audio(void)
{
    // 播放语音提示
    audio_play_for_type(s_last_type);

    // 等待语音播放完成 (约 2-3 秒)
    vTaskDelay(pdMS_TO_TICKS(3000));

    s_state = SYS_STATE_RESETTING;
}

static void state_resetting(void)
{
    ESP_LOGI(TAG, "复位中...");

    vTaskDelay(pdMS_TO_TICKS(RESET_DELAY_MS));

    // 电机复位
    motor_reset_all();

    // 清除 LED
    led_clear_garbage();
    led_set_status(LED_STATUS_READY);

    ESP_LOGI(TAG, "复位完成，等待下一次检测");
    ESP_LOGI(TAG, "");

    s_state = SYS_STATE_IDLE;
}

static void state_error(void)
{
    static int64_t last_error_log = 0;
    int64_t now = esp_timer_get_time();

    if (now - last_error_log > 5000000) {  // 每5秒打印一次
        ESP_LOGE(TAG, "系统错误！请检查硬件连接并重启");
        last_error_log = now;
    }

    led_set_status(LED_STATUS_ERROR);
    vTaskDelay(pdMS_TO_TICKS(500));
}

// ============================================================
// 主任务 / Main Task
// ============================================================
static void main_task(void* pvParameters)
{
    print_banner();

    ESP_LOGI(TAG, "系统初始化中...");

    esp_err_t ret = system_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "系统初始化失败: %d", ret);
        s_state = SYS_STATE_ERROR;
        s_system_ok = false;
    } else {
        s_system_ok = true;
        s_state = SYS_STATE_IDLE;
        s_last_detection_us = esp_timer_get_time();
        led_set_status(LED_STATUS_READY);

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  系统就绪！请将垃圾卡片放置在摄像头前");
        ESP_LOGI(TAG, "  System Ready! Place card in front of camera");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "");
    }

    // 主状态机循环
    while (true) {
        switch (s_state) {
            case SYS_STATE_IDLE:
                state_idle();
                break;

            case SYS_STATE_DETECTING:
                state_detecting();
                break;

            case SYS_STATE_CLASSIFYING:
                state_classifying();
                break;

            case SYS_STATE_SORTING:
                state_sorting();
                break;

            case SYS_STATE_AUDIO:
                state_audio();
                break;

            case SYS_STATE_RESETTING:
                state_resetting();
                break;

            case SYS_STATE_ERROR:
            default:
                state_error();
                break;
        }
    }
}

// ============================================================
// ESP-IDF 入口点 / ESP-IDF Entry Point
// ============================================================
extern "C" void app_main(void)
{
    // 创建主任务 (在 APP_CPU 上运行)
    xTaskCreatePinnedToCore(
        main_task,
        "main_task",
        MAIN_TASK_STACK_SIZE,
        NULL,
        MAIN_TASK_PRIORITY,
        NULL,
        1  // APP_CPU (Core 1)
    );
}