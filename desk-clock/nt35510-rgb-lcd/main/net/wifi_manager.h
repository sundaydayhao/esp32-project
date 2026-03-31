/**
 * @file wifi_manager.h
 * @brief WiFi 管理器（STA 模式，断线自动重连）
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING   = 1,
    WIFI_STATE_CONNECTED    = 2,
    WIFI_STATE_CONFIGURING  = 3,
    WIFI_STATE_FAILED       = 4,
} wifi_state_t;

esp_err_t   wifi_manager_init(void);
wifi_state_t wifi_manager_get_state(void);
bool        wifi_manager_is_connected(void);
bool        wifi_manager_is_config_mode(void);
bool        wifi_manager_has_credentials(void);
esp_err_t   wifi_manager_wait_connected(uint32_t timeout_ms);
void        wifi_manager_get_ip(char *buf, size_t len);
void        wifi_manager_get_ap_ssid(char *buf, size_t len);
void        wifi_manager_get_ap_client_ip(char *buf, size_t len);
void        wifi_manager_get_config_url(char *buf, size_t len);
void        wifi_manager_get_saved_ssid(char *buf, size_t len);
int8_t      wifi_manager_get_rssi(void);
esp_err_t   wifi_manager_connect_with_credentials(const char *ssid, const char *pass);
esp_err_t   wifi_manager_reset_credentials_and_start_provisioning(void);

#ifdef __cplusplus
}
#endif
