#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Push-to-Talk Button Handler
 *
 * Hardware: Momentary push button
 * - One pin -> CONFIG_BUTTON_GPIO (default GPIO0 = BOOT button)
 * - Other pin -> GND
 * - Internal pull-up enabled
 *
 * Button logic: active LOW (pressed = GPIO reads 0)
 * Includes debounce (20ms) to prevent false triggers.
 */

/**
 * @brief Button event types
 */
typedef enum {
    BUTTON_EVENT_PRESSED,   /**< Button was pressed (falling edge) */
    BUTTON_EVENT_RELEASED,  /**< Button was released (rising edge) */
    BUTTON_EVENT_LONG_PRESS /**< Button held for > 3 seconds */
} button_event_t;

/**
 * @brief Button event callback function type
 * @param event  The button event that occurred
 */
typedef void (*button_callback_t)(button_event_t event);

/**
 * @brief Initialize button GPIO with interrupt-based detection
 *
 * @param callback  Function to call on button events (called from ISR context task)
 * @return ESP_OK on success
 */
esp_err_t button_handler_init(button_callback_t callback);

/**
 * @brief Check if button is currently pressed
 * @return true if button is held down
 */
bool button_handler_is_pressed(void);

/**
 * @brief Deinitialize button handler
 * @return ESP_OK on success
 */
esp_err_t button_handler_deinit(void);

#ifdef __cplusplus
}
#endif