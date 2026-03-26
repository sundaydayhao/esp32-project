#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief 按键事件类型
 */
typedef enum {
    BUTTON_EVENT_PRESS = 0,         // 短按
    BUTTON_EVENT_LONG_PRESS,        // 长按
    BUTTON_EVENT_RELEASE,           // 释放
} button_event_t;

/**
 * @brief 按键事件回调函数类型
 */
typedef void (*button_event_cb_t)(button_event_t event);

/**
 * @brief 初始化按键处理器
 * @param event_cb 按键事件回调
 * @return ESP_OK 成功
 */
esp_err_t button_handler_init(button_event_cb_t event_cb);

/**
 * @brief 反初始化按键处理器
 */
void button_handler_deinit(void);