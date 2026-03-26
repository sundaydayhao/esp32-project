/**
 * @file keypad.h
 * @brief 4x4 矩阵键盘驱动模块
 *
 * 键盘布局:
 *   [1][2][3][A]
 *   [4][5][6][B]
 *   [7][8][9][C]
 *   [*][0][#][D]
 *
 * 使用说明:
 *   - '#' 键: 确认输入
 *   - '*' 键: 删除/取消
 *   - 'A' 键: 存件模式
 *   - 'D' 键: 管理员模式
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// 无效按键
#define KEYPAD_NO_KEY   0xFF

/**
 * @brief 初始化键盘
 * @return ESP_OK 成功
 */
esp_err_t keypad_init(void);

/**
 * @brief 键盘扫描任务 (在FreeRTOS任务中运行)
 * @param arg 未使用
 */
void keypad_task(void *arg);

/**
 * @brief 获取按键队列句柄 (用于其他任务读取按键)
 * @return 队列句柄
 */
QueueHandle_t keypad_get_queue(void);

/**
 * @brief 等待一个按键 (阻塞)
 * @param timeout_ms 超时时间 (ms), 0=永久等待
 * @return 按键字符, KEYPAD_NO_KEY 超时
 */
char keypad_wait_key(uint32_t timeout_ms);

/**
 * @brief 非阻塞获取按键
 * @return 按键字符, KEYPAD_NO_KEY 无按键
 */
char keypad_get_key(void);

#ifdef __cplusplus
}
#endif