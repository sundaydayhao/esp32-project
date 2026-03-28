/**
 * wifi_manager.h
 * WiFi Manager Interface
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_connect(const char *ssid, const char *password, uint32_t timeout_ms);
void wifi_disconnect(void);
bool wifi_is_connected(void);
esp_err_t http_get(const char *url, char *response, size_t resp_len);
esp_err_t http_post(const char *url,
                    const void *body,
                    size_t body_len,
                    const char *content_type,
                    const char *auth_token,
                    char *response,
                    size_t resp_len);
esp_err_t http_post_json(const char *url,
                         const char *json_body,
                         const char *auth_token,
                         char *response,
                         size_t resp_len);

#ifdef __cplusplus
}
#endif
