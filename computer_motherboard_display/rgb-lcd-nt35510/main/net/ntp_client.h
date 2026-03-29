/**
 * @file ntp_client.h
 * @brief NTP 时间同步客户端
 */
#pragma once
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ntp_client_init(void);
bool      ntp_client_is_synced(void);
time_t    ntp_client_get_time(void);
void      ntp_client_get_local_time(struct tm *tm_info);
void      ntp_task(void *arg);

#ifdef __cplusplus
}
#endif