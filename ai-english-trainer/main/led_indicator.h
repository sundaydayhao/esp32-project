#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED Status Indicator
 *
 * Hardware: WS2812B RGB LED (NeoPixel)
 * - Data pin -> CONFIG_LED_GPIO (default GPIO48 on ESP32-S3-DevKitC-1)
 * - VDD -> 3.3V or 5V
 * - GND -> GND
 *
 * Uses ESP-IDF led_strip driver (RMT peripheral).
 *
 * Color scheme:
 *   IDLE      -> Slow breathing green (ready)
 *   RECORDING -> Fast blinking red (capturing audio)
 *   PROCESSING-> Spinning yellow (thinking)
 *   PLAYING   -> Solid blue (teacher speaking)
 *   ERROR     -> Solid red (error occurred)
 *   WIFI_CONN -> Fast blinking white (connecting to WiFi)
 */

/**
 * @brief LED state enumeration
 */
typedef enum {
    LED_STATE_OFF,          /**< LED off */
    LED_STATE_IDLE,         /**< Slow breathing green - ready for input */
    LED_STATE_WIFI_CONNECT, /**< Fast white blink - connecting to WiFi */
    LED_STATE_RECORDING,    /**< Fast red blink - recording audio */
    LED_STATE_PROCESSING,   /**< Yellow pulse - processing with AI */
    LED_STATE_PLAYING,      /**< Solid blue - playing teacher response */
    LED_STATE_SUCCESS,      /**< Brief green flash - success */
    LED_STATE_ERROR,        /**< Solid red - error */
} led_state_t;

/**
 * @brief Initialize WS2812 LED driver
 * @return ESP_OK on success
 */
esp_err_t led_indicator_init(void);

/**
 * @brief Set LED to a specific state
 *        The LED will animate according to the state until changed.
 * @param state  Desired LED state
 */
void led_indicator_set_state(led_state_t state);

/**
 * @brief Set LED to a custom RGB color (static)
 * @param r  Red   (0-255)
 * @param g  Green (0-255)
 * @param b  Blue  (0-255)
 */
void led_indicator_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Turn off LED
 */
void led_indicator_off(void);

/**
 * @brief Deinitialize LED driver
 * @return ESP_OK on success
 */
esp_err_t led_indicator_deinit(void);

#ifdef __cplusplus
}
#endif