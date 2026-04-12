/**
 * @file touch_gt911.h
 * @brief Minimal GT911 touch driver for Waveshare ESP32-S3-Touch-LCD-7
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t touch_gt911_init(void);
bool touch_gt911_is_ready(void);
esp_err_t touch_gt911_read(uint16_t *x, uint16_t *y, bool *pressed);

#ifdef __cplusplus
}
#endif
