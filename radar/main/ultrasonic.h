#pragma once

#include <stdint.h>

/**
 * @brief Initialize HC-SR04 ultrasonic sensor.
 *        TRIG -> GPIO 16
 *        ECHO -> GPIO 17
 */
void ultrasonic_init(void);

/**
 * @brief Measure distance.
 * @return Distance in centimetres, or 0 on timeout / error.
 */
uint32_t ultrasonic_measure_cm(void);
