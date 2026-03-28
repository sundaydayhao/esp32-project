#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief Start the HTTP + WebSocket server
 * @return Handle to the server, or NULL on failure
 */
httpd_handle_t http_server_start(void);

/**
 * @brief Stop the HTTP server
 */
void http_server_stop(httpd_handle_t server);