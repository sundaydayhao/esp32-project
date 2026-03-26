#pragma once

#include "esp_err.h"
#include "fft_analyzer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the HTTP server with WebSocket support
 * @return ESP_OK on success
 */
esp_err_t web_server_start(void);

/**
 * @brief Stop the HTTP server
 */
void web_server_stop(void);

/**
 * @brief Broadcast spectrum data to all connected WebSocket clients
 *        Called from the FFT callback.
 * @param spectrum  Pointer to the latest spectrum data
 */
void web_server_broadcast_spectrum(const fft_spectrum_t *spectrum);

#ifdef __cplusplus
}
#endif