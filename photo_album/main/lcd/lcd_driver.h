/**
 * @file lcd_driver.h
 * @brief Unified RGB / I80 LCD driver API.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RGB565(r, g, b) ((uint16_t)(((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F)))
#define COLOR_BLACK     RGB565(0, 0, 0)
#define COLOR_WHITE     RGB565(31, 63, 31)
#define COLOR_RED       RGB565(31, 0, 0)
#define COLOR_GREEN     RGB565(0, 63, 0)
#define COLOR_BLUE      RGB565(0, 0, 31)
#define COLOR_GRAY      RGB565(15, 31, 15)

esp_err_t lcd_driver_init(void);
void lcd_driver_get_framebuf(uint16_t **fb0, uint16_t **fb1);
void lcd_driver_flush(uint16_t *fb);
void lcd_driver_wait_vsync(void);
void lcd_driver_set_brightness(uint8_t brightness);
void lcd_fb_fill(uint16_t *fb, uint16_t color);
void lcd_fb_fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color);
esp_lcd_panel_handle_t lcd_driver_get_panel(void);

#ifdef __cplusplus
}
#endif
