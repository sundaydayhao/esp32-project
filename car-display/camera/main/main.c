/**
 * @file main.c
 * @brief 车辆左右盲区视觉显示系统 - 主程序
 *
 * ============================================================
 * 系统概述
 * ============================================================
 *
 * 本系统使用 ESP32-S3 实现车辆左右盲区的实时视觉显示，
 * 通过两个 OV2640 摄像头分别监控车辆左右两侧盲区，
 * 并将画面实时显示在 SPI LCD 屏幕上。
 *
 * 功能特性：
 *   - 双摄像头分时复用（共享 DVP 接口，通过 PWDN 切换）
 *   - 分屏显示：左右各占半屏
 *   - 转向灯联动：打左转灯时左摄像头全屏，打右转灯时右摄像头全屏
 *   - 盲区警告：基于边缘检测的物体接近警告（绿/黄/红）
 *   - 状态栏：显示帧率、转向灯状态、显示模式
 *
 * ============================================================
 * 硬件连接
 * ============================================================
 *
 * OV2640 摄像头 (共享 DVP 接口):
 *   XCLK  -> GPIO15    PCLK  -> GPIO13
 *   VSYNC -> GPIO6     HREF  -> GPIO7
 *   D0    -> GPIO11    D1    -> GPIO9
 *   D2    -> GPIO8     D3    -> GPIO10
 *   D4    -> GPIO12    D5    -> GPIO18
 *   D6    -> GPIO17    D7    -> GPIO16
 *   SDA   -> GPIO4     SCL   -> GPIO5
 *
 * 左摄像头独立控制:
 *   PWDN  -> GPIO1     RESET -> GPIO2
 *
 * 右摄像头独立控制:
 *   PWDN  -> GPIO3     RESET -> GPIO46
 *
 * ST7789 LCD (SPI):
 *   MOSI  -> GPIO35    SCLK  -> GPIO36
 *   CS    -> GPIO34    DC    -> GPIO33
 *   RST   -> GPIO37    BL    -> GPIO38
 *
 * 转向灯检测:
 *   左转  -> GPIO39    右转  -> GPIO40
 *
 * ============================================================
 * 任务架构
 * ============================================================
 *
 *  ┌─────────────────┐    ┌─────────────────┐
 *  │  camera_task    │    │  display_task   │
 *  │  (Core 0)       │    │  (Core 1)       │
 *  │                 │    │                 │
 *  │ 交替采集左右帧   │───>│ 渲染到LCD屏幕   │
 *  │ 放入帧队列       │    │ 绘制警告叠加    │
 *  └─────────────────┘    │ 更新状态栏      │
 *                          └─────────────────┘
 *           ↑
 *  ┌─────────────────┐
 *  │  turn_signal    │
 *  │  (中断驱动)     │
 *  │                 │
 *  │ 检测转向灯信号   │
 *  │ 触发模式切换     │
 *  └─────────────────┘
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "camera_manager.h"
#include "lcd_driver.h"
#include "blind_spot_ui.h"
#include "turn_signal.h"

/* Kconfig 生成的配置宏 */
#include "sdkconfig.h"

static const char *TAG = "main";

/* ============================================================
 * 帧队列结构
 * ============================================================ */
typedef struct {
    camera_frame_t *left_frame;
    camera_frame_t *right_frame;
} frame_pair_t;

/* 帧队列深度 */
#define FRAME_QUEUE_DEPTH  2

static QueueHandle_t s_frame_queue = NULL;

/* ============================================================
 * 转向灯回调
 * ============================================================ */
static void on_turn_signal_changed(turn_signal_state_t state, void *user_data)
{
    (void)user_data;
    ESP_LOGI(TAG, "转向灯变化: %s", turn_signal_state_name(state));
    blind_spot_ui_update_by_turn_signal(state);
}

/* ============================================================
 * 摄像头采集任务
 * 运行在 Core 0，负责交替采集左右摄像头帧
 * ============================================================ */
static void camera_task(void *pvParameters)
{
    (void)pvParameters;

    /* 目标帧间隔 (ms) */
    const int frame_interval_ms = 1000 / CONFIG_CAM_FRAME_RATE;
    /* 每个摄像头的采集间隔 = 总间隔 / 2 */
    const int cam_interval_ms   = frame_interval_ms / 2;

    ESP_LOGI(TAG, "摄像头任务启动 (目标帧率: %d fps, 间隔: %d ms)",
             CONFIG_CAM_FRAME_RATE, frame_interval_ms);

    while (1) {
        TickType_t start_tick = xTaskGetTickCount();

        frame_pair_t pair = {NULL, NULL};

        /* ---- 采集左摄像头 ---- */
        esp_err_t ret = camera_manager_capture(CAMERA_LEFT, &pair.left_frame);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "左摄像头采集失败: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(cam_interval_ms));

        /* ---- 采集右摄像头 ---- */
        ret = camera_manager_capture(CAMERA_RIGHT, &pair.right_frame);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "右摄像头采集失败: %s", esp_err_to_name(ret));
        }

        /* ---- 发送帧对到显示队列 ---- */
        frame_pair_t old_pair;
        if (xQueueSend(s_frame_queue, &pair, 0) != pdTRUE) {
            /* 队列满，丢弃旧帧 */
            if (xQueueReceive(s_frame_queue, &old_pair, 0) == pdTRUE) {
                camera_frame_free(old_pair.left_frame);
                camera_frame_free(old_pair.right_frame);
            }
            xQueueSend(s_frame_queue, &pair, 0);
        }

        /* 等待到下一帧时间 */
        TickType_t elapsed = xTaskGetTickCount() - start_tick;
        TickType_t target  = pdMS_TO_TICKS(frame_interval_ms);
        if (elapsed < target) {
            vTaskDelay(target - elapsed);
        }
    }
}

/* ============================================================
 * 显示渲染任务
 * 运行在 Core 1，负责将帧渲染到 LCD
 * ============================================================ */
static void display_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "显示任务启动");

    /* 先显示待机界面 */
    blind_spot_ui_render_idle();
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        frame_pair_t pair;

        /* 等待新帧 (最多等 200ms) */
        if (xQueueReceive(s_frame_queue, &pair, pdMS_TO_TICKS(200)) != pdTRUE) {
            /* 超时：显示待机界面 */
            blind_spot_ui_render_idle();
            continue;
        }

        /* 获取当前显示模式 */
        display_mode_t mode = blind_spot_ui_get_mode();

        /* 获取帧率统计 */
        float fps_left = 0, fps_right = 0;
        camera_manager_get_stats(CAMERA_LEFT,  &fps_left,  NULL);
        camera_manager_get_stats(CAMERA_RIGHT, &fps_right, NULL);

        /* 获取转向灯状态 */
        turn_signal_state_t turn_state = turn_signal_get_state();

        /* ---- 根据模式渲染 ---- */
        switch (mode) {
            case DISPLAY_MODE_SPLIT:
                blind_spot_ui_render_split(pair.left_frame, pair.right_frame);
                break;

            case DISPLAY_MODE_LEFT_FULL:
                blind_spot_ui_render_fullscreen(pair.left_frame, CAMERA_LEFT);
                break;

            case DISPLAY_MODE_RIGHT_FULL:
                blind_spot_ui_render_fullscreen(pair.right_frame, CAMERA_RIGHT);
                break;

            case DISPLAY_MODE_IDLE:
            default:
                blind_spot_ui_render_idle();
                break;
        }

        /* ---- 绘制状态栏 ---- */
        blind_spot_ui_draw_statusbar(fps_left, fps_right, turn_state);

        /* ---- 释放帧内存 ---- */
        camera_frame_free(pair.left_frame);
        camera_frame_free(pair.right_frame);
    }
}

/* ============================================================
 * 系统初始化
 * ============================================================ */

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t init_lcd(void)
{
    lcd_config_t lcd_cfg = {
        .pin_mosi     = CONFIG_LCD_MOSI_GPIO,
        .pin_sclk     = CONFIG_LCD_SCLK_GPIO,
        .pin_cs       = CONFIG_LCD_CS_GPIO,
        .pin_dc       = CONFIG_LCD_DC_GPIO,
        .pin_rst      = CONFIG_LCD_RST_GPIO,
        .pin_bl       = CONFIG_LCD_BL_GPIO,
        .width        = CONFIG_LCD_WIDTH,
        .height       = CONFIG_LCD_HEIGHT,
        .spi_clock_hz = CONFIG_LCD_SPI_CLOCK_HZ,
    };
    esp_err_t ret = lcd_driver_init(&lcd_cfg);
    if (ret == ESP_OK) {
        lcd_set_backlight(200);
    }
    return ret;
}

static esp_err_t init_cameras(void)
{
    camera_manager_config_t cam_cfg = {
        /* 共享 DVP 引脚 */
        .pin_xclk  = CONFIG_CAM_XCLK_GPIO,
        .pin_pclk  = CONFIG_CAM_PCLK_GPIO,
        .pin_vsync = CONFIG_CAM_VSYNC_GPIO,
        .pin_href  = CONFIG_CAM_HREF_GPIO,
        .pin_d0    = CONFIG_CAM_D0_GPIO,
        .pin_d1    = CONFIG_CAM_D1_GPIO,
        .pin_d2    = CONFIG_CAM_D2_GPIO,
        .pin_d3    = CONFIG_CAM_D3_GPIO,
        .pin_d4    = CONFIG_CAM_D4_GPIO,
        .pin_d5    = CONFIG_CAM_D5_GPIO,
        .pin_d6    = CONFIG_CAM_D6_GPIO,
        .pin_d7    = CONFIG_CAM_D7_GPIO,
        .pin_sda   = CONFIG_CAM_SDA_GPIO,
        .pin_scl   = CONFIG_CAM_SCL_GPIO,
        /* 左摄像头独立引脚 */
        .pin_left_pwdn  = CONFIG_CAM_LEFT_PWDN_GPIO,
        .pin_left_reset = CONFIG_CAM_LEFT_RESET_GPIO,
        /* 右摄像头独立引脚 */
        .pin_right_pwdn  = CONFIG_CAM_RIGHT_PWDN_GPIO,
        .pin_right_reset = CONFIG_CAM_RIGHT_RESET_GPIO,
        /* 采集参数 */
        .frame_width    = CONFIG_CAM_FRAME_WIDTH,
        .frame_height   = CONFIG_CAM_FRAME_HEIGHT,
        .xclk_freq_hz   = CONFIG_CAM_XCLK_FREQ_HZ,
        .switch_delay_ms = CONFIG_CAM_SWITCH_DELAY_MS,
    };
    return camera_manager_init(&cam_cfg);
}

static esp_err_t init_turn_signal(void)
{
    turn_signal_config_t ts_cfg = {
        .pin_left     = CONFIG_TURN_LEFT_GPIO,
        .pin_right    = CONFIG_TURN_RIGHT_GPIO,
        .debounce_ms  = CONFIG_TURN_SIGNAL_DEBOUNCE_MS,
        .callback     = on_turn_signal_changed,
        .user_data    = NULL,
    };
    return turn_signal_init(&ts_cfg);
}

static esp_err_t init_blind_spot_ui(void)
{
    blind_spot_ui_config_t ui_cfg = {
        .screen_width        = CONFIG_LCD_WIDTH,
        .screen_height       = CONFIG_LCD_HEIGHT,
        .cam_frame_width     = CONFIG_CAM_FRAME_WIDTH,
        .cam_frame_height    = CONFIG_CAM_FRAME_HEIGHT,
        .warning_enabled     = CONFIG_BLIND_SPOT_WARNING_ENABLED,
        .warn_threshold_pct  = CONFIG_BLIND_SPOT_WARN_THRESHOLD,
        .danger_threshold_pct = CONFIG_BLIND_SPOT_DANGER_THRESHOLD,
    };
    return blind_spot_ui_init(&ui_cfg);
}

/* ============================================================
 * 应用程序入口
 * ============================================================ */
void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  车辆盲区视觉显示系统 v1.0");
    ESP_LOGI(TAG, "  ESP32-S3 + OV2640 x2 + ST7789 LCD");
    ESP_LOGI(TAG, "============================================");

    /* ---- 初始化 NVS ---- */
    ESP_ERROR_CHECK(init_nvs());
    ESP_LOGI(TAG, "[1/5] NVS 初始化完成");

    /* ---- 初始化 LCD ---- */
    ESP_ERROR_CHECK(init_lcd());
    ESP_LOGI(TAG, "[2/5] LCD 初始化完成 (%dx%d)",
             CONFIG_LCD_WIDTH, CONFIG_LCD_HEIGHT);

    /* ---- 初始化盲区 UI ---- */
    ESP_ERROR_CHECK(init_blind_spot_ui());
    blind_spot_ui_render_idle();
    ESP_LOGI(TAG, "[3/5] 盲区UI初始化完成");

    /* ---- 初始化摄像头 ---- */
    ESP_LOGI(TAG, "[4/5] 正在初始化摄像头...");
    esp_err_t cam_ret = init_cameras();
    if (cam_ret != ESP_OK) {
        ESP_LOGE(TAG, "摄像头初始化失败: %s", esp_err_to_name(cam_ret));
        ESP_LOGE(TAG, "请检查摄像头连接和引脚配置");
        /* 继续运行，显示无信号界面 */
    } else {
        ESP_LOGI(TAG, "[4/5] 摄像头初始化完成");
    }

    /* ---- 初始化转向灯检测 ---- */
    esp_err_t ts_ret = init_turn_signal();
    if (ts_ret != ESP_OK) {
        ESP_LOGW(TAG, "转向灯检测初始化失败: %s", esp_err_to_name(ts_ret));
    } else {
        ESP_LOGI(TAG, "[5/5] 转向灯检测初始化完成");
    }

    /* ---- 创建帧队列 ---- */
    s_frame_queue = xQueueCreate(FRAME_QUEUE_DEPTH, sizeof(frame_pair_t));
    if (!s_frame_queue) {
        ESP_LOGE(TAG, "帧队列创建失败");
        return;
    }

    /* ---- 设置初始显示模式 ---- */
    blind_spot_ui_set_mode(DISPLAY_MODE_SPLIT);

    ESP_LOGI(TAG, "系统初始化完成，启动任务...");

    /* ---- 创建摄像头采集任务 (Core 0) ---- */
    if (cam_ret == ESP_OK) {
        xTaskCreatePinnedToCore(
            camera_task,
            "camera_task",
            8192,           /* 栈大小 */
            NULL,
            5,              /* 优先级 */
            NULL,
            0               /* Core 0 */
        );
    }

    /* ---- 创建显示渲染任务 (Core 1) ---- */
    xTaskCreatePinnedToCore(
        display_task,
        "display_task",
        8192,
        NULL,
        4,
        NULL,
        1               /* Core 1 */
    );

    ESP_LOGI(TAG, "所有任务已启动");
    ESP_LOGI(TAG, "显示模式: 分屏 | 打转向灯自动切换全屏");

    /* 主任务进入监控循环 */
    while (1) {
        float fps_l = 0, fps_r = 0;
        uint32_t cnt_l = 0, cnt_r = 0;
        camera_manager_get_stats(CAMERA_LEFT,  &fps_l, &cnt_l);
        camera_manager_get_stats(CAMERA_RIGHT, &fps_r, &cnt_r);

        ESP_LOGI(TAG, "状态: 左摄像头 %.1ffps(%lu帧) | 右摄像头 %.1ffps(%lu帧) | 转向: %s | 模式: %d",
                 fps_l, (unsigned long)cnt_l,
                 fps_r, (unsigned long)cnt_r,
                 turn_signal_state_name(turn_signal_get_state()),
                 blind_spot_ui_get_mode());

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}