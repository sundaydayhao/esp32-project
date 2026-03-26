#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi Manager
 *
 * Handles WiFi station connection with automatic retry.
 * Credentials are configured via menuconfig (Kconfig.projbuild).
 */

/**
 * @brief Initialize WiFi and connect to configured AP
 *
 * Blocks until connected or max retries exceeded.
 * @return ESP_OK if connected successfully
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Check if WiFi is currently connected
 * @return true if connected and has IP address
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get current IP address as string
 * @param buf   Output buffer
 * @param len   Buffer length (at least 16 bytes)
 */
void wifi_manager_get_ip(char *buf, size_t len);

/**
 * @brief Disconnect and deinitialize WiFi
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_deinit(void);

#ifdef __cplusplus
}
#endif