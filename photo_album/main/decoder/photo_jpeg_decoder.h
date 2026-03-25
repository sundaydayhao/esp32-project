/**
 * @file photo_jpeg_decoder.h
 * @brief Decode JPEG images into the LCD frame buffer.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t src_w;
    uint16_t src_h;
    uint16_t dst_w;
    uint16_t dst_h;
    int offset_x;
    int offset_y;
} decode_info_t;

esp_err_t jpeg_decode_to_fb(const char *path, uint16_t *fb,
                            scale_mode_t mode, uint16_t bg_color,
                            decode_info_t *info);

esp_err_t jpeg_get_size(const char *path, uint16_t *width, uint16_t *height);

#ifdef __cplusplus
}
#endif
