/**
 * @file media_library.h
 * @brief SD card Picture/Video directory indexing helpers
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_LIBRARY_MAX_ITEMS 32
#define MEDIA_LIBRARY_NAME_LEN  64
#define MEDIA_LIBRARY_PATH_LEN  160

typedef struct {
    char name[MEDIA_LIBRARY_NAME_LEN];
    char path[MEDIA_LIBRARY_PATH_LEN];
    char lvgl_path[MEDIA_LIBRARY_PATH_LEN + 4];
    bool has_preview;
    char preview_path[MEDIA_LIBRARY_PATH_LEN];
    char preview_lvgl_path[MEDIA_LIBRARY_PATH_LEN + 4];
} media_library_entry_t;

esp_err_t media_library_refresh(void);
uint32_t media_library_get_generation(void);
int media_library_get_photo_count(void);
int media_library_get_video_count(void);
const media_library_entry_t *media_library_get_photo(int index);
const media_library_entry_t *media_library_get_video(int index);

#ifdef __cplusplus
}
#endif
