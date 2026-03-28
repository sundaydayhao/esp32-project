#pragma once

#include <stddef.h>

/**
 * @brief Start the HTTP server.
 *        Serves the radar page at  GET /
 *        WebSocket endpoint at     GET /ws
 */
void web_server_start(void);

/**
 * @brief Broadcast a text message to all connected WebSocket clients.
 * @param data  Null-terminated string to send.
 */
void ws_broadcast(const char *data);