#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t log_file_init(void);
bool log_file_is_ready(void);
