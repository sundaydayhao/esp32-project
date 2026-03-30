/**
 * @file weather_client.h
 * @brief QWeather client data types and APIs
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char   date[12];
    int8_t high;
    int8_t low;
    char   icon_day[8];
} weather_forecast_day_t;

typedef struct {
    char   icon_code[8];
    int8_t temp_now;
    int8_t temp_feels;
    int8_t temp_high;
    int8_t temp_low;
    uint8_t humidity;
    uint16_t wind_speed;
    uint16_t visibility;

    weather_forecast_day_t forecast[3];

    int64_t now_update_time;
    int64_t forecast_update_time;
    int64_t update_time;
    bool now_valid;
    bool forecast_valid;
    bool valid;
} weather_data_t;

esp_err_t weather_client_init(void);
esp_err_t weather_client_fetch(void);
void weather_client_get_data(weather_data_t *data);
void weather_task(void *arg);

#ifdef __cplusplus
}
#endif
