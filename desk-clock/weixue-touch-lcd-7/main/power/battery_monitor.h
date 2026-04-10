/**
 * @file battery_monitor.h
 * @brief Optional ADC-based battery monitor
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool available;
    bool charging;
    uint8_t level_percent;
    uint16_t voltage_mv;
} battery_status_t;

esp_err_t battery_monitor_init(void);
bool battery_monitor_get_status(battery_status_t *status);

#ifdef __cplusplus
}
#endif
