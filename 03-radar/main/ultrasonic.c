#include "ultrasonic.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

/* ---- Hardware configuration ---- */
#define TRIG_GPIO   GPIO_NUM_16
#define ECHO_GPIO   GPIO_NUM_17

/* Timeouts in microseconds */
#define ECHO_WAIT_TIMEOUT_US    5000    /* max wait for echo to go HIGH  (5 ms)  */
#define ECHO_PULSE_TIMEOUT_US   38000   /* max echo pulse width          (38 ms) */

/* Speed of sound: 340 m/s → 0.034 cm/µs */
#define SOUND_CM_PER_US         0.034f

static const char *TAG = "ultrasonic";

void ultrasonic_init(void)
{
    /* TRIG: output */
    gpio_config_t trig_conf = {
        .pin_bit_mask = (1ULL << TRIG_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&trig_conf));
    gpio_set_level(TRIG_GPIO, 0);

    /* ECHO: input */
    gpio_config_t echo_conf = {
        .pin_bit_mask = (1ULL << ECHO_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&echo_conf));

    ESP_LOGI(TAG, "HC-SR04 initialized: TRIG=GPIO%d  ECHO=GPIO%d",
             TRIG_GPIO, ECHO_GPIO);
}

uint32_t ultrasonic_measure_cm(void)
{
    /* 1. Send 10 µs trigger pulse */
    gpio_set_level(TRIG_GPIO, 0);
    esp_rom_delay_us(2);
    gpio_set_level(TRIG_GPIO, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIG_GPIO, 0);

    /* 2. Wait for ECHO to go HIGH */
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(ECHO_GPIO) == 0) {
        if ((esp_timer_get_time() - t0) > ECHO_WAIT_TIMEOUT_US) {
            return 0; /* no echo */
        }
    }

    /* 3. Measure how long ECHO stays HIGH */
    int64_t echo_start = esp_timer_get_time();
    while (gpio_get_level(ECHO_GPIO) == 1) {
        if ((esp_timer_get_time() - echo_start) > ECHO_PULSE_TIMEOUT_US) {
            return 0; /* out of range */
        }
    }
    int64_t echo_end = esp_timer_get_time();

    /* 4. Convert pulse width to distance */
    int64_t pulse_us = echo_end - echo_start;
    uint32_t distance_cm = (uint32_t)(pulse_us * SOUND_CM_PER_US / 2.0f);

    return distance_cm;
}
