/**
 * @file dht11_sensor.h
 * @brief DHT11 temperature/humidity sensor driver and background sampler
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int8_t temperature_c;
    uint8_t humidity;
    int64_t update_time_us;
    uint32_t sample_count;
    uint32_t error_count;
    bool valid;
} dht11_data_t;

esp_err_t dht11_sensor_init(void);
void dht11_sensor_get_data(dht11_data_t *data);
void dht11_sensor_task(void *arg);

#ifdef __cplusplus
}
#endif
