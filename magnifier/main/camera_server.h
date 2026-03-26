#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the camera with board-specific pin configuration.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t camera_init(void);

/**
 * @brief Start the HTTP web server for camera streaming.
 *        Serves the magnifier web UI and MJPEG stream.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t start_camera_server(void);

#ifdef __cplusplus
}
#endif