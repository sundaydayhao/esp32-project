/**
 * @file display_manager.h
 * @brief SSD1306 OLED 显示管理器
 *
 * 功能：
 *  - 多屏幕轮播（环境数据 / 报警状态 / 网络状态 / 系统信息）
 *  - 报警时闪烁提示
 *  - 低功耗：无操作 60s 后熄屏
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISPLAY_SCREEN_ENV    = 0,  /* 温湿度 + CO₂ */
    DISPLAY_SCREEN_AIR    = 1,  /* PM2.5 + AQI */
    DISPLAY_SCREEN_NET    = 2,  /* WiFi + MQTT 状态 */
    DISPLAY_SCREEN_SYS    = 3,  /* 固件版本 + 运行时间 */
    DISPLAY_SCREEN_ALARM  = 4,  /* 报警详情 */
    DISPLAY_SCREEN_COUNT  = 5,
} display_screen_t;

/**
 * @brief 初始化 OLED 显示器
 */
esp_err_t display_manager_init(void);

/**
 * @brief 切换到指定屏幕
 */
void display_manager_set_screen(display_screen_t screen);

/**
 * @brief 显示管理任务（由 main 创建）
 */
void display_task(void *arg);

/**
 * @brief 强制刷新当前屏幕
 */
void display_manager_refresh(void);

/**
 * @brief 开启/关闭显示
 */
void display_manager_power(bool on);

#ifdef __cplusplus
}
#endif