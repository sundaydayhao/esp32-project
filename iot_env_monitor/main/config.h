/**
 * @file config.h
 * @brief Global project configuration
 */
#pragma once

#include <stdint.h>

#include "sdkconfig.h"
#include "driver/i2c.h"
#include "driver/uart.h"

#if CONFIG_IDF_TARGET_ESP32S3
#define IEM_DEFAULT_I2C_SCL_PIN     15
#define IEM_DEFAULT_I2C_SDA_PIN     21
#define IEM_DEFAULT_PMS5003_TX_PIN  14
#define IEM_DEFAULT_PMS5003_RX_PIN  26
#define IEM_DEFAULT_PMS5003_SET_PIN 13
#else
#define IEM_DEFAULT_I2C_SCL_PIN     22
#define IEM_DEFAULT_I2C_SDA_PIN     21
#define IEM_DEFAULT_PMS5003_TX_PIN  25
#define IEM_DEFAULT_PMS5003_RX_PIN  26
#define IEM_DEFAULT_PMS5003_SET_PIN 27
#endif

/* ================================================================
 * Firmware identity
 * ================================================================ */
#define FW_VERSION_MAJOR    2
#define FW_VERSION_MINOR    1
#define FW_VERSION_PATCH    0
#define FW_VERSION_STR      "2.1.0"
#define DEVICE_MODEL        "IEM-3200"

/* ================================================================
 * I2C bus (SHT31 + BME280 + SSD1306 OLED)
 * ================================================================ */
#define I2C_MASTER_PORT     I2C_NUM_0
#define I2C_SCL_PIN         IEM_DEFAULT_I2C_SCL_PIN
#define I2C_SDA_PIN         IEM_DEFAULT_I2C_SDA_PIN
#define I2C_FREQ_HZ         400000
#define I2C_TIMEOUT_MS      50

/* SHT31 */
#define SHT31_I2C_ADDR      0x44
#define SHT31_HEATER_EN     0

/* BME280 */
#define BME280_I2C_ADDR     0x76

/* SSD1306 OLED */
#define OLED_I2C_ADDR       0x3C
#define OLED_WIDTH          128
#define OLED_HEIGHT         64

/* ================================================================
 * UART buses
 * ================================================================ */
/* MH-Z19B CO2 sensor (UART1) */
#define MHZ19_UART_PORT     UART_NUM_1
#define MHZ19_TX_PIN        17
#define MHZ19_RX_PIN        16
#define MHZ19_BAUD          9600

/* PMS5003 particle sensor (UART2) */
#define PMS5003_UART_PORT   UART_NUM_2
#define PMS5003_TX_PIN      IEM_DEFAULT_PMS5003_TX_PIN
#define PMS5003_RX_PIN      IEM_DEFAULT_PMS5003_RX_PIN
#define PMS5003_BAUD        9600

/* Modbus RTU RS-485 (UART0) */
#define MODBUS_UART_PORT    UART_NUM_0
#define MODBUS_TX_PIN       1
#define MODBUS_RX_PIN       3
#define MODBUS_DE_PIN       4
#define MODBUS_BAUD         9600
#define MODBUS_SLAVE_ADDR   0x01

/* ================================================================
 * GPIO
 * ================================================================ */
#define LED_STATUS_PIN      2
#define LED_ALARM_PIN       5
#define BTN_RESET_PIN       0
#define BUZZER_PIN          18
#define PMS5003_SET_PIN     IEM_DEFAULT_PMS5003_SET_PIN

/* ================================================================
 * Sampling and publish intervals
 * ================================================================ */
#define SENSOR_SAMPLE_MS        5000
#define MQTT_PUBLISH_MS         10000
#define DISPLAY_REFRESH_MS      2000
#define ALARM_CHECK_MS          1000
#define DATA_LOG_MS             60000
#define WATCHDOG_FEED_MS        5000
#define NTP_SYNC_INTERVAL_S     3600

/* ================================================================
 * Data logging
 * ================================================================ */
#define DATA_LOG_MAX_RECORDS    1440
#define DATA_LOG_NVS_NS         "datalog"
#define DATA_LOG_SPIFFS_PATH    "/spiffs/datalog.bin"

/* ================================================================
 * WiFi
 * ================================================================ */
#define WIFI_SSID_MAX_LEN       32
#define WIFI_PASS_MAX_LEN       64
#define WIFI_RECONNECT_DELAY_MS 5000
#define WIFI_MAX_RETRY          10
#define WIFI_NVS_NS             "wifi_cfg"

/*
 * Provide build-time fallbacks when the project does not define
 * CONFIG_WIFI_*_DEFAULT via a component Kconfig/menuconfig entry.
 */
#ifndef CONFIG_WIFI_SSID_DEFAULT
#define CONFIG_WIFI_SSID_DEFAULT ""
#endif

#ifndef CONFIG_WIFI_PASS_DEFAULT
#define CONFIG_WIFI_PASS_DEFAULT ""
#endif

/* ================================================================
 * MQTT
 * ================================================================ */
#define MQTT_NVS_NS             "mqtt_cfg"
#define MQTT_BROKER_DEFAULT     "mqtt://broker.hivemq.com:1883"
#define MQTT_CLIENT_ID_PREFIX   "IEM3200_"
#define MQTT_KEEPALIVE_S        60
#define MQTT_QOS_TELEMETRY      1
#define MQTT_QOS_ALARM          2
#define MQTT_TOPIC_TELEMETRY    "iem/%s/telemetry"
#define MQTT_TOPIC_ALARM        "iem/%s/alarm"
#define MQTT_TOPIC_STATUS       "iem/%s/status"
#define MQTT_TOPIC_CMD          "iem/%s/cmd"
#define MQTT_TOPIC_OTA          "iem/%s/ota"

/* ================================================================
 * Web server
 * ================================================================ */
#define WEB_SERVER_PORT         80
#define WEB_MAX_CONNECTIONS     4

/* ================================================================
 * OTA
 * ================================================================ */
#define OTA_NVS_NS              "ota_cfg"
#define OTA_RECV_TIMEOUT_MS     10000
#define OTA_BUF_SIZE            4096

/* ================================================================
 * Alarm defaults
 * ================================================================ */
typedef struct {
    float temp_high;
    float temp_low;
    float humi_high;
    float humi_low;
    float co2_high;
    float pm25_high;
    float pm10_high;
    float pressure_high;
    float pressure_low;
    float tvoc_high;
} alarm_thresholds_t;

#define ALARM_THRESH_DEFAULT { \
    .temp_high     = 40.0f,    \
    .temp_low      = -10.0f,   \
    .humi_high     = 90.0f,    \
    .humi_low      = 10.0f,    \
    .co2_high      = 1000.0f,  \
    .pm25_high     = 75.0f,    \
    .pm10_high     = 150.0f,   \
    .pressure_high = 1060.0f,  \
    .pressure_low  = 870.0f,   \
    .tvoc_high     = 500.0f,   \
}

/* ================================================================
 * Task priorities and stack sizes
 * ================================================================ */
#define TASK_PRIO_SENSOR        6
#define TASK_PRIO_MQTT          5
#define TASK_PRIO_MODBUS        7
#define TASK_PRIO_ALARM         8
#define TASK_PRIO_DISPLAY       3
#define TASK_PRIO_DATALOG       4
#define TASK_PRIO_OTA           2

#define TASK_STACK_SENSOR       4096
#define TASK_STACK_MQTT         6144
#define TASK_STACK_MODBUS       4096
#define TASK_STACK_ALARM        3072
#define TASK_STACK_DISPLAY      3072
#define TASK_STACK_DATALOG      4096
#define TASK_STACK_OTA          8192

/* ================================================================
 * Log tags
 * ================================================================ */
#define TAG_MAIN        "MAIN"
#define TAG_SENSOR      "SENSOR"
#define TAG_SHT31       "SHT31"
#define TAG_BME280      "BME280"
#define TAG_MHZ19       "MHZ19"
#define TAG_PMS5003     "PMS5003"
#define TAG_MQTT        "MQTT"
#define TAG_MODBUS      "MODBUS"
#define TAG_WEB         "WEB"
#define TAG_DATALOG     "DATALOG"
#define TAG_ALARM       "ALARM"
#define TAG_OTA         "OTA"
#define TAG_DISPLAY     "DISPLAY"
