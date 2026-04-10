/**
 * @file ui_music_player.h
 * @brief Music player UI component
 */
#pragma once

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create music player UI panel
 * @param parent Parent object to attach the music player to
 * @return ESP_OK on success
 */
esp_err_t ui_music_player_create(lv_obj_t *parent);

/**
 * @brief Update music player UI with current status
 */
void ui_music_player_update(void);

/**
 * @brief Get the music player panel object
 * @return Pointer to the music player panel
 */
lv_obj_t *ui_music_player_get_panel(void);

#ifdef __cplusplus
}
#endif