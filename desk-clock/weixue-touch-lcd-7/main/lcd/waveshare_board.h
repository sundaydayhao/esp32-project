#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t waveshare_board_init(void);
esp_err_t waveshare_board_set_lcd_state(bool display_on);
esp_err_t waveshare_board_set_sd_card_selected(bool selected);
esp_err_t waveshare_board_set_touch_reset(bool high);
esp_err_t waveshare_board_i2c_write(uint8_t device_addr,
                                    const uint8_t *data,
                                    size_t data_len);
esp_err_t waveshare_board_i2c_write_read(uint8_t device_addr,
                                         const uint8_t *write_data,
                                         size_t write_len,
                                         uint8_t *read_data,
                                         size_t read_len);

#ifdef __cplusplus
}
#endif
