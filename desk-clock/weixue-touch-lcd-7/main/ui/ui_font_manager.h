/**
 * @file ui_font_manager.h
 * @brief UI font loading helpers
 */
#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_font_manager_init(void);
esp_err_t ui_font_manager_load_external(void);
const lv_font_t *ui_font_manager_get_font_16(void);
bool ui_font_manager_is_external_loaded(void);
void ui_font_loader_task(void *arg);

#ifdef __cplusplus
}
#endif
