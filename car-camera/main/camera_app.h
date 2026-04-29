#pragma once

#include <stdbool.h>

#include "esp_camera.h"
#include "esp_err.h"

esp_err_t camera_app_init(void);
bool camera_app_is_ready(void);
camera_fb_t *camera_app_fb_get(void);
void camera_app_fb_return(camera_fb_t *fb);
