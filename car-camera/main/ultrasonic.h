#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t ultrasonic_init(void);
int ultrasonic_get_distance_cm(void);
bool ultrasonic_is_blocked(void);
