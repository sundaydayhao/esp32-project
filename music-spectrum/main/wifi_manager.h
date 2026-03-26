#pragma once

#include "esp_err.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi connection states
 */
typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,
} wifi_state_t;

/**
 * @brief Initialize WiFi in station mode
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Connect to configured WiFi network (blocks until connected or failed)
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_connect(void);

/**
 * @brief Get current WiFi state
 */
wifi_state_t wifi_manager_get_state(void);

/**
 * @brief Get assigned IP address as string
 * @param buf  Buffer to write IP string into
 * @param len  Buffer length
 */
void wifi_manager_get_ip(char *buf, size_t len);

#ifdef __cplusplus
}
#endif