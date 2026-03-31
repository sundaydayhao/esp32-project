#pragma once

/**
 * @file display.h
 * @brief 显示屏驱动接口 (ST7789 + LVGL)
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化显示屏
 * 
 * 初始化SPI总线、ST7789面板驱动和LVGL图形库
 * 
 * @return ESP_OK 成功
 */
esp_err_t display_init(void);

/**
 * @brief 获取LVGL显示句柄
 * @return lv_disp_t* LVGL显示对象指针
 */
void *display_get_lvgl_disp(void);

/**
 * @brief 获取LVGL互斥锁（线程安全操作LVGL时使用）
 * @return true 成功获取锁
 */
bool display_lock(uint32_t timeout_ms);

/**
 * @brief 释放LVGL互斥锁
 */
void display_unlock(void);

/**
 * @brief 设置背光亮度
 * @param brightness 亮度 0-100
 */
void display_set_brightness(uint8_t brightness);

#ifdef __cplusplus
}
#endif