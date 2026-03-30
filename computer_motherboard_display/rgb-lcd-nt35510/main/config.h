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
 * Display
 * ================================================================ */
#define DISPLAY_BOARD_NAME          "Generic NT35510 I80 Board"

#define LCD_H_RES                   CONFIG_DESK_CLOCK_NT35510_H_RES
#define LCD_V_RES                   CONFIG_DESK_CLOCK_NT35510_V_RES
#define LCD_NUM_FB                  2

#define LCD_I80_PIXEL_CLOCK_HZ      CONFIG_DESK_CLOCK_NT35510_PIXEL_CLOCK_HZ
#define LCD_I80_BUS_WIDTH           CONFIG_DESK_CLOCK_NT35510_I80_BUS_WIDTH
#define LCD_I80_DRAW_BUF_LINES      CONFIG_DESK_CLOCK_NT35510_DRAW_BUF_LINES
#define LCD_I80_CMD_BITS            16
#define LCD_I80_PARAM_BITS          16

#define LCD_I80_PIN_WR              ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_WR)
#define LCD_I80_PIN_CS              ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_CS)
#define LCD_I80_PIN_DC              ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_DC)
#define LCD_I80_PIN_RST             ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_RST)
#define LCD_I80_PIN_BACKLIGHT       ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_BK_LIGHT)
#define LCD_I80_PIN_POWER           ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_POWER)

#define LCD_I80_PIN_D0              ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D0)
#define LCD_I80_PIN_D1              ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D1)
#define LCD_I80_PIN_D2              ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D2)
#define LCD_I80_PIN_D3              ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D3)
#define LCD_I80_PIN_D4              ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D4)
#define LCD_I80_PIN_D5              ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D5)
#define LCD_I80_PIN_D6              ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D6)
#define LCD_I80_PIN_D7              ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D7)

#if CONFIG_DESK_CLOCK_NT35510_I80_BUS_WIDTH == 16
#define LCD_I80_PIN_D8              ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D8)
#define LCD_I80_PIN_D9              ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D9)
#define LCD_I80_PIN_D10             ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D10)
#define LCD_I80_PIN_D11             ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D11)
#define LCD_I80_PIN_D12             ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D12)
#define LCD_I80_PIN_D13             ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D13)
#define LCD_I80_PIN_D14             ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D14)
#define LCD_I80_PIN_D15             ((gpio_num_t)CONFIG_DESK_CLOCK_NT35510_PIN_D15)
#else
#define LCD_I80_PIN_D8              GPIO_NUM_NC
#define LCD_I80_PIN_D9              GPIO_NUM_NC
#define LCD_I80_PIN_D10             GPIO_NUM_NC
#define LCD_I80_PIN_D11             GPIO_NUM_NC
#define LCD_I80_PIN_D12             GPIO_NUM_NC
#define LCD_I80_PIN_D13             GPIO_NUM_NC
#define LCD_I80_PIN_D14             GPIO_NUM_NC
#define LCD_I80_PIN_D15             GPIO_NUM_NC
#endif

#define BOOT_MODE_LCD_DIAGNOSTIC    0
#define BOOT_MODE_UI_ONLY           1
#define BOOT_MODE_FULL_APP          2
#define SYSTEM_BOOT_MODE            BOOT_MODE_FULL_APP
#define LCD_DIAGNOSTIC_SWITCH_MS    1000

/* ================================================================
 * Audio / MAX98357A
 * ================================================================ */
#define AUDIO_I2S_BCLK_PIN          GPIO_NUM_47
#define AUDIO_I2S_WS_PIN            GPIO_NUM_48
#define AUDIO_I2S_DOUT_PIN          GPIO_NUM_46
#define AUDIO_I2S_SAMPLE_RATE_HZ    22050
#define AUDIO_TONE_FREQ_HZ          1200
#define AUDIO_TONE_DURATION_MS      120
#define AUDIO_TONE_VOLUME_PCT       18
#define AUDIO_SPEECH_GAIN_PCT       180
#define AUDIO_ANNOUNCE_START_HOUR   8
#define AUDIO_ANNOUNCE_END_HOUR     20
#define AUDIO_ANNOUNCE_POLL_MS      1000
#define AUDIO_SEGMENT_GAP_MS        80
#define AUDIO_SPEECH_SAMPLE_RATE_HZ 8000

/* ================================================================
 * Input
 * ================================================================ */
#define BUTTON_TIME_ANNOUNCE_PIN        GPIO_NUM_21
#define BUTTON_TIME_ACTIVE_LEVEL        0
#define BUTTON_TIME_POLL_MS             10
#define BUTTON_TIME_DEBOUNCE_MS         40

/* ================================================================
 * Sensor / DHT11
 * ================================================================ */
#define DHT11_DATA_PIN              GPIO_NUM_45
#define DHT11_START_SIGNAL_MS       20
#define DHT11_STARTUP_DELAY_MS      1500
#define DHT11_SAMPLE_INTERVAL_MS    2000

/* ================================================================
 * WiFi
 * ================================================================ */
#define WIFI_SSID_MAX_LEN       64
#define WIFI_PASS_MAX_LEN       64
#define WIFI_MAX_RETRY          10
#define WIFI_RECONNECT_DELAY_MS 3000
#define WIFI_NVS_NS             "wifi_cfg"

#ifndef CONFIG_WIFI_SSID_DEFAULT
#define CONFIG_WIFI_SSID_DEFAULT    "haoge"
#endif
#ifndef CONFIG_WIFI_PASS_DEFAULT
#define CONFIG_WIFI_PASS_DEFAULT    "19901118"
#endif

/* ================================================================
 * NTP
 * ================================================================ */
#define NTP_SERVER_1        "ntp.aliyun.com"
#define NTP_SERVER_2        "cn.pool.ntp.org"
#define NTP_SERVER_3        "time.windows.com"
#define NTP_TIMEZONE        "CST-8"
#define NTP_SYNC_INTERVAL_S (3600)

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
#define TAG_AUDIO       "AUDIO"
#define TAG_DHT11       "DHT11"

/* ================================================================
 * Tasks
 * ================================================================ */
#define TASK_STACK_UI           (8  * 1024)
#define TASK_STACK_LCD_DIAG     (4  * 1024)
#define TASK_STACK_WEATHER      (16 * 1024)
#define TASK_STACK_NTP          (4  * 1024)
#define TASK_STACK_CHIME        (6  * 1024)
#define TASK_STACK_BUTTON       (4  * 1024)
#define TASK_STACK_DHT11        (4  * 1024)
#define TASK_PRIO_LCD_DIAG      3
#define TASK_PRIO_UI            7
#define TASK_PRIO_WEATHER       4
#define TASK_PRIO_NTP           5
#define TASK_PRIO_CHIME         4
#define TASK_PRIO_BUTTON        4
#define TASK_PRIO_DHT11         4
