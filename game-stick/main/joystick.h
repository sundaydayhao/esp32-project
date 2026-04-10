#pragma once

#include <stdbool.h>

/**
 * Joystick hardware mapping:
 *   X-axis  -> ADC1 Channel 6 (GPIO34)
 *   Y-axis  -> ADC1 Channel 7 (GPIO35)
 *   Button  -> GPIO0  (active LOW, internal pull-up)
 *
 * Adjust the GPIO/channel defines in joystick.c to match your wiring.
 */

typedef enum {
    JOY_NONE   = 0,
    JOY_UP,
    JOY_DOWN,
    JOY_LEFT,
    JOY_RIGHT,
    JOY_BUTTON,
} joystick_dir_t;

/**
 * @brief Initialize ADC channels and button GPIO.
 */
void joystick_init(void);

/**
 * @brief Read current joystick direction.
 * @return One of the joystick_dir_t values.
 */
joystick_dir_t joystick_read(void);

/**
 * @brief Convert direction enum to JSON string.
 * @return Pointer to a static string like {"dir":"UP"}
 */
const char *joystick_dir_to_json(joystick_dir_t dir);