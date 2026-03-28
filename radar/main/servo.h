#pragma once

#include <stdint.h>

/**
 * @brief Initialize the servo motor PWM (50 Hz, GPIO 14).
 */
void servo_init(void);

/**
 * @brief Set servo angle.
 * @param angle  Target angle in degrees [0, 180].
 */
void servo_set_angle(uint8_t angle);
