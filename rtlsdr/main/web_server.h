#pragma once

#include "esp_err.h"

/**
 * @brief Initialize and start the HTTP web server
 *        - Serves static files from SPIFFS (/spiffs/)
 *        - WebSocket endpoint at /ws for real-time aircraft data
 *        - REST API at /api/aircraft for current aircraft list (JSON)
 *        - REST API at /api/stats for decoder statistics (JSON)
 *
 * @return ESP_OK on success
 */
esp_err_t web_server_init(void);

/**
 * @brief Stop the web server
 */
void web_server_stop(void);