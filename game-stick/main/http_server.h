#pragma once

/**
 * @brief Start the HTTP + WebSocket server on port 80.
 *
 * Registered routes:
 *   GET /        -> index.html  (game selection page)
 *   GET /tetris  -> tetris.html
 *   GET /2048    -> game2048.html
 *   GET /ws      -> WebSocket endpoint
 */
void http_server_start(void);

/**
 * @brief Send a text message to every connected WebSocket client.
 * @param msg  NULL-terminated string to send.
 */
void ws_broadcast(const char *msg);