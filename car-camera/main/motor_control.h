#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    CAR_MOTION_STOP = 0,
    CAR_MOTION_FORWARD,
    CAR_MOTION_BACKWARD,
    CAR_MOTION_LEFT,
    CAR_MOTION_RIGHT,
    CAR_MOTION_SPIN_LEFT,
    CAR_MOTION_SPIN_RIGHT,
} car_motion_t;

typedef struct {
    car_motion_t motion;
    uint8_t speed;
    int left_percent;
    int right_percent;
    bool obstacle_blocked;
    uint32_t last_command_ms;
} motor_status_t;

esp_err_t motor_control_init(void);
esp_err_t motor_control_drive(car_motion_t motion, uint8_t speed);
void motor_control_stop(void);
void motor_control_set_obstacle_blocked(bool blocked);
void motor_control_get_status(motor_status_t *status);

const char *car_motion_to_string(car_motion_t motion);
car_motion_t car_motion_from_string(const char *value);
