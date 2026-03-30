/**
 * @file env_history.h
 * @brief Indoor/outdoor environment history ring buffer
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t timestamp_s;
    bool indoor_valid;
    int8_t indoor_temp_c;
    uint8_t indoor_humidity;
    bool outdoor_valid;
    int8_t outdoor_temp_c;
    uint8_t outdoor_humidity;
} env_history_point_t;

esp_err_t env_history_init(void);
size_t env_history_get_count(void);
size_t env_history_copy_points(env_history_point_t *points, size_t max_points);
void env_history_task(void *arg);

#ifdef __cplusplus
}
#endif
