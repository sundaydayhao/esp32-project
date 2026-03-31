/**
 * @file weather_client.h
 * @brief QWeather daily forecast client
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char text[32];
    char forecast_date[16];
    int8_t temp_min;
    int8_t temp_max;
    uint8_t humidity;
    char update_time[20];
    bool valid;
} weather_data_t;

esp_err_t weather_client_init(void);
esp_err_t weather_client_fetch(void);
void weather_client_get_data(weather_data_t *out);
void weather_task(void *arg);

#ifdef __cplusplus
}
#endif
