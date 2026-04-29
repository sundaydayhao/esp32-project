#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_intercom_init(void);
esp_err_t audio_intercom_set_ws_client(httpd_handle_t server, int fd);
void audio_intercom_clear_ws_client(int fd);
esp_err_t audio_intercom_play_pcm(const uint8_t *pcm, size_t len);
bool audio_intercom_is_ready(void);

#ifdef __cplusplus
}
#endif
