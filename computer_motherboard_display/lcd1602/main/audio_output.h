#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_output_init(void);
esp_err_t audio_output_deinit(void);
bool audio_output_is_ready(void);
esp_err_t audio_output_play_beijing_time_announcement(int hour, int minute);

#ifdef __cplusplus
}
#endif