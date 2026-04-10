/**
 * @file config.h
 * @brief Global project configuration
 */
#pragma once

#include "driver/gpio.h"

/* ================================================================
 * Firmware
 * ================================================================ */
#define FW_VERSION_STR      "1.0.0"
#define DEVICE_NAME         "ESP32-S3 Desk Clock"

/* ================================================================
 * Display / Waveshare ESP32-S3-Touch-LCD-7
 * ================================================================ */
#define DISPLAY_BOARD_NAME          "Waveshare ESP32-S3-Touch-LCD-7"

#define LCD_H_RES                   800
#define LCD_V_RES                   480
#define LCD_HSYNC_BACK_PORCH        8
#define LCD_HSYNC_FRONT_PORCH       8
#define LCD_HSYNC_PULSE_WIDTH       4
#define LCD_VSYNC_BACK_PORCH        16
#define LCD_VSYNC_FRONT_PORCH       16
#define LCD_VSYNC_PULSE_WIDTH       4
#define LCD_PCLK_HZ                 (16 * 1000 * 1000)

#define LCD_PIN_PCLK                GPIO_NUM_7
#define LCD_PIN_VSYNC               GPIO_NUM_3
#define LCD_PIN_HSYNC               GPIO_NUM_46
#define LCD_PIN_DE                  GPIO_NUM_5
#define LCD_PIN_BACKLIGHT           GPIO_NUM_NC

#define LCD_PIN_D0                  GPIO_NUM_14
#define LCD_PIN_D1                  GPIO_NUM_38
#define LCD_PIN_D2                  GPIO_NUM_18
#define LCD_PIN_D3                  GPIO_NUM_17
#define LCD_PIN_D4                  GPIO_NUM_10
#define LCD_PIN_D5                  GPIO_NUM_39
#define LCD_PIN_D6                  GPIO_NUM_0
#define LCD_PIN_D7                  GPIO_NUM_45
#define LCD_PIN_D8                  GPIO_NUM_48
#define LCD_PIN_D9                  GPIO_NUM_47
#define LCD_PIN_D10                 GPIO_NUM_21
#define LCD_PIN_D11                 GPIO_NUM_1
#define LCD_PIN_D12                 GPIO_NUM_2
#define LCD_PIN_D13                 GPIO_NUM_42
#define LCD_PIN_D14                 GPIO_NUM_41
#define LCD_PIN_D15                 GPIO_NUM_40

#define LCD_NUM_FB                  2

#define BOOT_MODE_LCD_DIAGNOSTIC    0
#define BOOT_MODE_UI_ONLY           1
#define BOOT_MODE_FULL_APP          2
#define SYSTEM_BOOT_MODE            BOOT_MODE_FULL_APP
#define LCD_DIAGNOSTIC_SWITCH_MS    1000

/* ================================================================
 * WiFi
 * ================================================================ */
#define WIFI_SSID_MAX_LEN       64
#define WIFI_PASS_MAX_LEN       64
#define WIFI_MAX_RETRY          10
#define WIFI_RECONNECT_DELAY_MS 3000
#define WIFI_CONNECT_TIMEOUT_MS 60000
#define WIFI_NVS_NS             "wifi_cfg"
#define WIFI_PROV_AP_SSID_PREFIX "DeskClock"
#define WIFI_PROV_AP_CHANNEL    6
#define WIFI_PROV_AP_IP         "192.168.4.1"

#ifndef CONFIG_WIFI_SSID_DEFAULT
#define CONFIG_WIFI_SSID_DEFAULT    ""
#endif
#ifndef CONFIG_WIFI_PASS_DEFAULT
#define CONFIG_WIFI_PASS_DEFAULT    ""
#endif

/* ================================================================
 * NTP
 * ================================================================ */
#define NTP_SERVER_1        "ntp.aliyun.com"
#define NTP_SERVER_2        "cn.pool.ntp.org"
#define NTP_SERVER_3        "time.windows.com"
#define NTP_TIMEZONE        "CST-8"
#define NTP_SYNC_INTERVAL_S (3600)
#define NTP_RETRY_INTERVAL_S (30)

/* ================================================================
 * Weather API
 * ================================================================ */
#define WEATHER_API_KEY             "04c81d5d4f3346a48d69b886c6c98cb0"
#define WEATHER_LOCATION_ID         "101280601"
#define WEATHER_API_HOST            "nx5khwapkg.re.qweatherapi.com"
#define WEATHER_UPDATE_INTERVAL_S   (3600)
#define WEATHER_HTTP_TIMEOUT_MS     (10000)

/* ================================================================
 * NVS
 * ================================================================ */
#define NVS_NAMESPACE_CLOCK     "desk_clock"
#define NVS_KEY_BRIGHTNESS      "brightness"
#define NVS_KEY_CITY_ID         "city_id"
#define NVS_KEY_API_KEY         "api_key"

/* ================================================================
 * Log tags
 * ================================================================ */
#define TAG_MAIN        "MAIN"
#define TAG_LCD         "LCD"
#define TAG_WIFI        "WIFI"
#define TAG_NTP         "NTP"
#define TAG_WEATHER     "WEATHER"
#define TAG_UI          "UI"
#define TAG_STORE       "STORE"
#define TAG_WEB         "WEB"

/* ================================================================
 * Web provisioning
 * ================================================================ */
#define WEB_SERVER_PORT         80

/* ================================================================
 * Tasks
 * ================================================================ */
#define TASK_STACK_UI           (8  * 1024)
#define TASK_STACK_LCD_DIAG     (4  * 1024)
#define TASK_STACK_WEATHER      (16 * 1024)
#define TASK_STACK_NTP          (4  * 1024)
#define TASK_PRIO_LCD_DIAG      3
#define TASK_PRIO_UI            7
#define TASK_PRIO_WEATHER       4
#define TASK_PRIO_NTP           5
