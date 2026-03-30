#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int8_t temperature_c;
    uint8_t humidity_pct;
    bool valid;
    uint32_t sample_age_sec;
} dht11_reading_t;

esp_err_t dht11_init(void);
esp_err_t dht11_read(dht11_reading_t *out);
void dht11_get_reading(dht11_reading_t *out);
void dht11_task(void *arg);

#ifdef __cplusplus
}
#endif