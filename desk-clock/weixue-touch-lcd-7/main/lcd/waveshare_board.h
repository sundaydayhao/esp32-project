#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t waveshare_board_init(void);
esp_err_t waveshare_board_set_lcd_state(bool display_on);

#ifdef __cplusplus
}
#endif
