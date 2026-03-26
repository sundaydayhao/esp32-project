/**
 * @file main.c
 * @brief ESP32-S3 智能快递柜 - 主程序入口
 *
 * ============================================================
 * 系统架构:
 * ============================================================
 *
 *  ┌─────────────────────────────────────────────────────────┐
 *  │                    ESP32-S3 智能快递柜                   │
 *  ├──────────────┬──────────────┬──────────────┬────────────┤
 *  │  Core 0      │  Core 0      │  Core 1      │  Core 1    │
 *  │  WiFi/TCP    │  HTTP Server │  Keypad Task │  Display   │
 *  │  (系统)      │  REST API    │  键盘扫描    │  Task      │
 *  ├──────────────┴──────────────┼──────────────┴────────────┤
 *  │         Locker Task         │      Main UI Task         │
 *  │         格口状态机           │      用户交互逻辑          │
 *  └─────────────────────────────┴───────────────────────────┘
 *
 * ============================================================
 * 用户操作流程:
 * ============================================================
 *
 * 取件流程:
 *   1. 待机界面按 '#' 键
 *   2. 输入6位取件码
 *   3. 再次按 '#' 确认
 *   4. 验证成功 -> 对应格口解锁
 *   5. 取出包裹，关门
 *   6. 系统自动锁定，返回待机
 *
 * 存件流程 (快递员):
 *   1. 待机界面按 'A' 键
 *   2. 系统自动选择空闲格口并解锁
 *   3. 屏幕显示取件码
 *   4. 放入包裹，关门
 *   5. 系统自动锁定
 *   6. 将取件码告知收件人
 *
 * 管理员流程:
 *   1. 待机界面按 'D' 键
 *   2. 通过 HTTP API 或 Web 界面管理
 *
 * ============================================================
 * HTTP API 访问:
 * ============================================================
 *   AP模式: 连接 WiFi "SmartLocker_AP" (密码: admin1234)
 *           访问 http://192.168.4.1
 *   STA模式: 访问 http://<设备IP>
 * ============================================================
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "config.h"
#include "storage.h"
#include "buzzer.h"
#include "locker.h"
#include "keypad.h"
#include "display.h"
#include "wifi_manager.h"
#include "http_server.h"

static const char *TAG = "main";

// ============================================================
// UI 状态机
// ============================================================

typedef enum {
    UI_STATE_IDLE        = 0,   // 待机
    UI_STATE_PICKUP      = 1,   // 取件: 输入取件码
    UI_STATE_DEPOSIT     = 2,   // 存件: 快递员放件
    UI_STATE_ADMIN       = 3,   // 管理员模式
} ui_state_t;

typedef struct {
    ui_state_t  state;
    char        input_buf[CODE_LENGTH + 1]; // 输入缓冲区
    uint8_t     input_len;                  // 已输入长度
    uint32_t    idle_timeout;               // 无操作超时计数
} ui_ctx_t;

static ui_ctx_t s_ui;

// ============================================================
// UI 辅助函数
// ============================================================

static void ui_reset(void)
{
    s_ui.state = UI_STATE_IDLE;
    s_ui.input_len = 0;
    memset(s_ui.input_buf, 0, sizeof(s_ui.input_buf));
    s_ui.idle_timeout = 0;
    display_show_idle();
}

static void ui_handle_pickup_key(char key)
{
    if (key >= '0' && key <= '9') {
        // 数字键: 追加到输入缓冲区
        if (s_ui.input_len < CODE_LENGTH) {
            s_ui.input_buf[s_ui.input_len++] = key;
            s_ui.input_buf[s_ui.input_len] = '\0';
            buzzer_beep_key();
            display_show_enter_code(s_ui.input_buf, s_ui.input_len);
        }
    } else if (key == '*') {
        // 删除键
        if (s_ui.input_len > 0) {
            s_ui.input_len--;
            s_ui.input_buf[s_ui.input_len] = '\0';
            buzzer_beep_key();
            display_show_enter_code(s_ui.input_buf, s_ui.input_len);
        } else {
            // 取消，返回待机
            buzzer_beep_key();
            ui_reset();
        }
    } else if (key == '#') {
        // 确认键
        if (s_ui.input_len == CODE_LENGTH) {
            // 验证取件码
            uint8_t locker_id = 0;
            locker_result_t result = locker_pickup(s_ui.input_buf, &locker_id);

            if (result == LOCKER_OK) {
                ESP_LOGI(TAG, "取件成功，格口%d", locker_id);
                buzzer_beep_success();
                display_show_success("Pickup Success!", locker_id);
                vTaskDelay(pdMS_TO_TICKS(500));
                ui_reset();
            } else if (result == LOCKER_ERR_LOCKED) {
                ESP_LOGW(TAG, "格口被锁定");
                buzzer_beep_error();
                display_show_error("Locked! Try later", 0xFF);
                vTaskDelay(pdMS_TO_TICKS(2000));
                ui_reset();
            } else {
                ESP_LOGW(TAG, "取件码错误");
                buzzer_beep_error();
                // 计算剩余次数
                uint8_t remaining = MAX_FAILED_ATTEMPTS - 1;
                display_show_error("Wrong Code!", remaining);
                vTaskDelay(pdMS_TO_TICKS(1500));
                // 清空输入，重新输入
                s_ui.input_len = 0;
                memset(s_ui.input_buf, 0, sizeof(s_ui.input_buf));
                display_show_enter_code(s_ui.input_buf, s_ui.input_len);
            }
        } else {
            // 输入不足
            buzzer_beep_warning();
            display_show_error("Need 6 digits!", 0xFF);
            vTaskDelay(pdMS_TO_TICKS(1000));
            display_show_enter_code(s_ui.input_buf, s_ui.input_len);
        }
    } else if (key == 'B') {
        // B键: 取消，返回待机
        buzzer_beep_key();
        ui_reset();
    }

    // 重置超时计数
    s_ui.idle_timeout = 0;
}

static void ui_handle_deposit(void)
{
    // 自动选择空闲格口
    uint8_t out_id = 0;
    char out_code[CODE_LENGTH + 1] = {0};

    locker_result_t result = locker_deposit(0xFF, NULL, &out_id, out_code);

    if (result == LOCKER_OK) {
        ESP_LOGI(TAG, "存件成功，格口%d，取件码: %s", out_id, out_code);
        buzzer_beep_open();
        display_show_depositing(out_id, out_code);
        // 存件界面保持显示，直到门关闭或超时
        // locker_task 会自动处理后续状态
    } else {
        ESP_LOGW(TAG, "没有空闲格口");
        buzzer_beep_error();
        display_show_error("No Empty Locker!", 0xFF);
        vTaskDelay(pdMS_TO_TICKS(2000));
        ui_reset();
    }
}

static void ui_handle_idle_key(char key)
{
    switch (key) {
    case '#':
        // 取件模式
        s_ui.state = UI_STATE_PICKUP;
        s_ui.input_len = 0;
        memset(s_ui.input_buf, 0, sizeof(s_ui.input_buf));
        buzzer_beep_key();
        display_show_enter_code(s_ui.input_buf, 0);
        break;

    case 'A':
        // 存件模式 (快递员)
        s_ui.state = UI_STATE_DEPOSIT;
        buzzer_beep_key();
        ui_handle_deposit();
        break;

    case 'D':
        // 管理员模式: 显示WiFi信息
        s_ui.state = UI_STATE_ADMIN;
        buzzer_beep_key();
        {
            char ip[20] = {0};
            char ssid[33] = {0};
            if (!wifi_manager_get_ip(ip, sizeof(ip))) {
                wifi_manager_get_ap_ip(ip, sizeof(ip));
            }
            wifi_manager_get_ssid(ssid, sizeof(ssid));
            display_show_wifi_info(ip, ssid);
            // 10秒后自动返回 (display模块处理)
            vTaskDelay(pdMS_TO_TICKS(100));
            s_ui.state = UI_STATE_IDLE;
        }
        break;

    case '*':
        // 刷新显示
        buzzer_beep_key();
        display_refresh();
        break;

    default:
        break;
    }
}

// ============================================================
// 主 UI 任务
// ============================================================

static void ui_task(void *arg)
{
    ESP_LOGI(TAG, "UI任务启动");

    // 等待系统初始化完成
    vTaskDelay(pdMS_TO_TICKS(500));

    ui_reset();

    while (1) {
        // 获取按键 (100ms 超时)
        char key = keypad_wait_key(100);

        if (key != (char)KEYPAD_NO_KEY) {
            ESP_LOGD(TAG, "UI收到按键: '%c', 状态: %d", key, s_ui.state);
            s_ui.idle_timeout = 0;

            switch (s_ui.state) {
            case UI_STATE_IDLE:
                ui_handle_idle_key(key);
                break;

            case UI_STATE_PICKUP:
                ui_handle_pickup_key(key);
                break;

            case UI_STATE_DEPOSIT:
                // 存件模式下按任意键返回待机
                if (key == '*' || key == 'B') {
                    buzzer_beep_key();
                    ui_reset();
                }
                break;

            case UI_STATE_ADMIN:
                // 管理员模式下按任意键返回待机
                buzzer_beep_key();
                ui_reset();
                break;

            default:
                ui_reset();
                break;
            }
        } else {
            // 无按键，检查超时
            s_ui.idle_timeout++;

            // 非待机状态下，30秒无操作自动返回待机
            if (s_ui.state != UI_STATE_IDLE &&
                s_ui.idle_timeout >= 300) { // 300 * 100ms = 30s
                ESP_LOGI(TAG, "UI超时，返回待机");
                ui_reset();
            }

            // 存件模式下检查格口状态
            if (s_ui.state == UI_STATE_DEPOSIT) {
                // 检查是否有格口从UNLOCKED变为EMPTY (门已关闭)
                bool all_locked = true;
                for (int i = 0; i < LOCKER_COUNT; i++) {
                    locker_info_t info;
                    locker_get_info(i, &info);
                    if (info.state == LOCKER_STATE_UNLOCKED) {
                        all_locked = false;
                        break;
                    }
                }
                if (all_locked && s_ui.idle_timeout > 30) {
                    // 所有格口已关闭，返回待机
                    ui_reset();
                }
            }
        }
    }
}

// ============================================================
// 应用程序入口
// ============================================================

void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  ESP32-S3 智能快递柜 v1.0");
    ESP_LOGI(TAG, "  Smart Parcel Locker System");
    ESP_LOGI(TAG, "============================================");

    // --------------------------------------------------------
    // 1. 初始化 NVS 存储
    // --------------------------------------------------------
    ESP_LOGI(TAG, "[1/7] 初始化NVS存储...");
    ESP_ERROR_CHECK(storage_init());

    // --------------------------------------------------------
    // 2. 初始化蜂鸣器
    // --------------------------------------------------------
    ESP_LOGI(TAG, "[2/7] 初始化蜂鸣器...");
    ESP_ERROR_CHECK(buzzer_init());
    buzzer_beep(1000, 100); // 启动提示音

    // --------------------------------------------------------
    // 3. 初始化格口管理 (舵机、LED、门磁)
    // --------------------------------------------------------
    ESP_LOGI(TAG, "[3/7] 初始化格口管理...");
    ESP_ERROR_CHECK(locker_init());

    // --------------------------------------------------------
    // 4. 初始化键盘
    // --------------------------------------------------------
    ESP_LOGI(TAG, "[4/7] 初始化键盘...");
    ESP_ERROR_CHECK(keypad_init());

    // --------------------------------------------------------
    // 5. 初始化显示屏
    // --------------------------------------------------------
    ESP_LOGI(TAG, "[5/7] 初始化OLED显示屏...");
    esp_err_t disp_ret = display_init();
    if (disp_ret != ESP_OK) {
        ESP_LOGW(TAG, "OLED初始化失败 (可能未连接): %s",
                 esp_err_to_name(disp_ret));
        // 显示屏不是必须的，继续运行
    }

    // --------------------------------------------------------
    // 6. 初始化 WiFi
    // --------------------------------------------------------
    ESP_LOGI(TAG, "[6/7] 初始化WiFi...");
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(wifi_manager_start());

    // --------------------------------------------------------
    // 7. 启动 HTTP 服务器
    // --------------------------------------------------------
    ESP_LOGI(TAG, "[7/7] 启动HTTP服务器...");
    ESP_ERROR_CHECK(http_server_start());

    // --------------------------------------------------------
    // 打印系统信息
    // --------------------------------------------------------
    char ip[20] = {0};
    char ssid[33] = {0};
    if (!wifi_manager_get_ip(ip, sizeof(ip))) {
        wifi_manager_get_ap_ip(ip, sizeof(ip));
    }
    wifi_manager_get_ssid(ssid, sizeof(ssid));

    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  系统启动完成!");
    ESP_LOGI(TAG, "  WiFi SSID: %s", ssid);
    ESP_LOGI(TAG, "  IP地址:    %s", ip);
    ESP_LOGI(TAG, "  Web管理:   http://%s", ip);
    ESP_LOGI(TAG, "  格口数量:  %d", LOCKER_COUNT);
    ESP_LOGI(TAG, "  空闲格口:  %d", locker_get_empty_count());
    ESP_LOGI(TAG, "============================================");

    // 启动成功提示音
    buzzer_beep_success();

    // --------------------------------------------------------
    // 创建 FreeRTOS 任务
    // --------------------------------------------------------

    // 格口管理任务 (Core 1, 高优先级)
    xTaskCreatePinnedToCore(
        locker_task,
        "locker_task",
        LOCKER_TASK_STACK,
        NULL,
        LOCKER_TASK_PRIORITY,
        NULL,
        1   // Core 1
    );

    // 键盘扫描任务 (Core 1)
    xTaskCreatePinnedToCore(
        keypad_task,
        "keypad_task",
        KEYPAD_TASK_STACK,
        NULL,
        KEYPAD_TASK_PRIORITY,
        NULL,
        1   // Core 1
    );

    // 显示任务 (Core 1)
    if (disp_ret == ESP_OK) {
        xTaskCreatePinnedToCore(
            display_task,
            "display_task",
            DISPLAY_TASK_STACK,
            NULL,
            DISPLAY_TASK_PRIORITY,
            NULL,
            1   // Core 1
        );
    }

    // UI 交互任务 (Core 1)
    xTaskCreatePinnedToCore(
        ui_task,
        "ui_task",
        8192,
        NULL,
        5,
        NULL,
        1   // Core 1
    );

    ESP_LOGI(TAG, "所有任务已启动");

    // app_main 返回后，主任务会被删除
    // WiFi 和 HTTP 服务器在 Core 0 上由系统管理
}