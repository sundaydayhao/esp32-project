#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE,
} wifi_state_t;

esp_err_t wifi_manager_init(void);
wifi_state_t wifi_manager_get_state(void);
bool wifi_manager_is_connected(void);
void wifi_manager_start_ap(void);
void wifi_manager_start_sta(const char *ssid, const char *password);
void wifi_manager_trigger_ntp_sync(void);
const char *wifi_manager_get_ip(void);