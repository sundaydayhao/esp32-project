#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ch422g_ctrl_init(void);
esp_err_t ch422g_ctrl_set_pin(uint8_t pin, bool high);
esp_err_t ch422g_ctrl_set_pins(uint16_t pin_mask, bool high);

#ifdef __cplusplus
}
#endif
