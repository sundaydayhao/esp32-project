#pragma once

#include "esp_err.h"
#include "esp_netif.h"

/**
 * @brief Initialize WiFi in AP mode (creates hotspot for web interface access)
 *        SSID: ESP32-ADSB, Password: adsb1234
 *        Connect to http://192.168.4.1 to view the ADS-B map
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init_ap(void);

/**
 * @brief Initialize WiFi in STA mode (connect to existing network)
 *
 * @param ssid     WiFi SSID
 * @param password WiFi password
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init_sta(const char *ssid, const char *password);

/**
 * @brief Get the current IP address as string
 *
 * @param buf    Buffer to store IP string
 * @param buflen Buffer length (at least 16 bytes)
 */
void wifi_manager_get_ip(char *buf, size_t buflen);