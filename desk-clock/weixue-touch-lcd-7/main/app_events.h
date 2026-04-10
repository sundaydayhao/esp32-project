/**
 * @file app_events.h
 * @brief Application-level async event bus
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "config.h"
#include "esp_err.h"
#include "esp_event.h"
#include "net/weather_client.h"
#include "net/wifi_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(APP_EVENTS);

typedef enum {
    APP_EVENT_UI_STATE_CHANGED = 1,
    APP_EVENT_WIFI_STATE_CHANGED,
    APP_EVENT_FONT_STATE_CHANGED,
    APP_EVENT_NTP_STATE_CHANGED,
    APP_EVENT_WEATHER_STATE_CHANGED,
} app_event_id_t;

typedef enum {
    APP_UI_STATE_READY = 1,
    APP_UI_STATE_FAILED,
} app_ui_state_t;

typedef struct {
    app_ui_state_t state;
    esp_err_t err;
} app_ui_event_t;

typedef enum {
    APP_FONT_STATE_LOADING = 1,
    APP_FONT_STATE_READY,
    APP_FONT_STATE_FAILED,
} app_font_state_t;

typedef struct {
    app_font_state_t state;
    bool external_loaded;
    esp_err_t err;
} app_font_event_t;

typedef struct {
    wifi_state_t state;
    bool connected;
    bool configuring;
    bool has_credentials;
    int8_t rssi;
    char ip[20];
    char ssid[WIFI_SSID_MAX_LEN];
    char ap_ssid[33];
} app_wifi_event_t;

typedef enum {
    APP_NTP_STATE_WAITING_FOR_NETWORK = 1,
    APP_NTP_STATE_SYNCING,
    APP_NTP_STATE_SYNCED,
} app_ntp_state_t;

typedef struct {
    app_ntp_state_t state;
    bool synced;
    time_t now;
} app_ntp_event_t;

typedef enum {
    APP_WEATHER_STATE_WAITING = 1,
    APP_WEATHER_STATE_FETCHING,
    APP_WEATHER_STATE_UPDATED,
    APP_WEATHER_STATE_FAILED,
} app_weather_state_t;

typedef struct {
    app_weather_state_t state;
    bool wifi_ready;
    bool time_ready;
    esp_err_t err;
    weather_data_t data;
} app_weather_event_t;

esp_err_t app_events_init(void);
esp_event_loop_handle_t app_events_loop(void);
esp_err_t app_events_post(int32_t event_id, const void *event_data, size_t event_data_size);

#ifdef __cplusplus
}
#endif
