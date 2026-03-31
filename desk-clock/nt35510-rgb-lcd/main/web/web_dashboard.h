/**
 * @file web_dashboard.h
 * @brief Mobile web dashboard for indoor/outdoor charts
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_dashboard_init(void);
void web_dashboard_task(void *arg);

#ifdef __cplusplus
}
#endif
