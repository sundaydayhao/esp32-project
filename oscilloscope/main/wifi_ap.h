#pragma once

#include "esp_err.h"

/**
 * @brief Initialize WiFi in Access Point mode
 *        SSID: Oscilloscope
 *        Password: 12345678
 *        IP: 192.168.4.1
 */
void wifi_ap_init(void);