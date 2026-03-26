#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

typedef bool (*lcd_driver_flush_done_cb_t)(void *user_ctx);

esp_err_t lcd_driver_init(void);
esp_lcd_panel_handle_t lcd_driver_get_panel(void);
void lcd_driver_get_framebuf(uint16_t **fb0, uint16_t **fb1);
size_t lcd_driver_get_framebuf_size(void);
bool lcd_driver_uses_direct_mode(void);
bool lcd_driver_needs_rgb565_byte_swap(void);
esp_err_t lcd_driver_show_test_pattern(uint32_t pattern_index);
esp_err_t lcd_driver_register_flush_done_callback(lcd_driver_flush_done_cb_t cb, void *user_ctx);
void lcd_driver_set_brightness(uint8_t pct);
