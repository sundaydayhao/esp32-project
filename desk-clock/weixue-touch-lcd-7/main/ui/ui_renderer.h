/**
 * @file ui_renderer.h
 * @brief UI 渲染器（整合时钟 + 天气，管理双缓冲刷新）
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_renderer_init(void);
bool      ui_renderer_is_ready(void);
void      ui_renderer_task(void *arg);

#ifdef __cplusplus
}
#endif
