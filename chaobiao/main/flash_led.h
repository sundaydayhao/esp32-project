#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t flash_led_init(void);
void flash_led_capture_begin(void);
void flash_led_capture_end(void);

#ifdef __cplusplus
}
#endif
