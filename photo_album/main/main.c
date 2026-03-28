/**
 * @file main.c
 * @brief ESP32-S3 桌面相册主程序
 *
 * 启动流程：
 *  1. NVS 初始化
 *  2. RGB LCD 初始化（800×480，双帧缓冲）
 *  3. SD 卡挂载（SPI 模式，FAT32）
 *  4. 图片扫描（递归扫描 SD 卡）
 *  5. 幻灯片管理器初始化
 *  6. 创建幻灯片任务
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "config.h"
#include "lcd/lcd_driver.h"
#include "sd/sd_card.h"
#include "sd/photo_scanner.h"
#include "slideshow/slideshow.h"

/* ================================================================
 * 启动画面（纯色 + 进度条）
 * ================================================================ */
static void show_splash(uint16_t *fb, const char *msg, uint8_t progress)
{
    lcd_fb_fill(fb, COLOR_BLACK);

    /* 进度条背景 */
    int bar_x = LCD_H_RES / 4;
    int bar_y = LCD_V_RES / 2 + 20;
    int bar_w = LCD_H_RES / 2;
    int bar_h = 8;
    lcd_fb_fill_rect(fb, bar_x, bar_y, bar_w, bar_h, COLOR_GRAY);

    /* 进度条填充 */
    int fill_w = (bar_w * progress) / 100;
    lcd_fb_fill_rect(fb, bar_x, bar_y, fill_w, bar_h, COLOR_WHITE);

    lcd_driver_flush(fb);
    (void)msg;
}

/* ================================================================
 * 错误画面
 * ================================================================ */
static void show_error(uint16_t *fb, uint16_t color)
{
    lcd_fb_fill(fb, COLOR_BLACK);
    /* 绘制 X 形错误标志 */
    for (int i = 0; i < 60; i++) {
        int cx = LCD_H_RES / 2;
        int cy = LCD_V_RES / 2;
        fb[(cy - 30 + i) * LCD_H_RES + (cx - 30 + i)] = color;
        fb[(cy - 30 + i) * LCD_H_RES + (cx + 30 - i)] = color;
    }
    lcd_driver_flush(fb);
}

/* ================================================================
 * 主函数
 * ================================================================ */
void app_main(void)
{
    ESP_LOGI(TAG_MAIN, "========================================");
    ESP_LOGI(TAG_MAIN, "  %s v%s", DEVICE_NAME, FW_VERSION_STR);
    ESP_LOGI(TAG_MAIN, "  Board: %s", DISPLAY_BOARD_NAME);
    ESP_LOGI(TAG_MAIN, "========================================");

    /* ---- 1. NVS 初始化 ---- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG_MAIN, "NVS 分区损坏，执行擦除...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG_MAIN, "[1/5] NVS 初始化完成");

    /* ---- 2. LCD 初始化 ---- */
    ret = lcd_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "[2/5] LCD 初始化失败: %s", esp_err_to_name(ret));
        /* LCD 失败无法继续 */
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG_MAIN, "[2/5] LCD 初始化完成 (%dx%d)", LCD_H_RES, LCD_V_RES);

    /* 获取帧缓冲区，显示启动画面 */
    uint16_t *fb0, *fb1;
    lcd_driver_get_framebuf(&fb0, &fb1);
    show_splash(fb0, "初始化中...", 20);

    /* ---- 3. SD 卡初始化 ---- */
    ret = sd_card_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "[3/5] SD 卡初始化失败: %s", esp_err_to_name(ret));
        show_error(fb0, COLOR_RED);
        /* SD 卡失败：等待重新插入（实际产品可加热插拔检测） */
        ESP_LOGE(TAG_MAIN, "请插入 SD 卡后重启设备");
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG_MAIN, "[3/5] SD 卡挂载成功");
    show_splash(fb0, "扫描图片...", 50);

    /* ---- 4. 图片扫描 ---- */
    int photo_count = photo_scanner_init(PHOTO_SCAN_PATH);
    if (photo_count <= 0) {
        ESP_LOGW(TAG_MAIN, "[4/5] 未找到图片，请将 JPEG/BMP 文件放入 SD 卡根目录");
        show_error(fb0, COLOR_BLUE);
        /* 继续运行，等待用户放入图片后重启 */
        while (1) vTaskDelay(pdMS_TO_TICKS(5000));
    }
    ESP_LOGI(TAG_MAIN, "[4/5] 图片扫描完成: 共 %d 张", photo_count);
    show_splash(fb0, "准备就绪...", 80);

    /* 随机种子 */
    srand((unsigned int)esp_timer_get_time());

    /* ---- 5. 幻灯片管理器初始化 ---- */
    ESP_ERROR_CHECK(slideshow_init());
    ESP_LOGI(TAG_MAIN, "[5/5] 幻灯片管理器初始化完成");
    show_splash(fb0, "启动完成", 100);
    vTaskDelay(pdMS_TO_TICKS(500));

    /* ---- 创建幻灯片任务 ---- */
    xTaskCreatePinnedToCore(
        slideshow_task,
        "slideshow",
        TASK_STACK_SLIDESHOW,
        NULL,
        TASK_PRIO_SLIDESHOW,
        NULL,
        1   /* Core 1 */
    );

    ESP_LOGI(TAG_MAIN, "系统启动完成！共 %d 张图片，开始播放...", photo_count);
    ESP_LOGI(TAG_MAIN, "可用堆内存: %lu KB", esp_get_free_heap_size() / 1024);

    /* app_main 返回后 FreeRTOS 会删除此任务 */
}
