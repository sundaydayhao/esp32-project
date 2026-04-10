/**
 * @file ui_stream_binfont.h
 * @brief Preload LVGL binfont into memory and keep all glyph A8 bitmaps indexed
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_font_t *ui_stream_binfont_create(const char *path);
void ui_stream_binfont_destroy(lv_font_t *font);

#ifdef __cplusplus
}
#endif
