#include "servo.h"

#include "driver/ledc.h"
#include "esp_log.h"

/* ---- Hardware configuration ---- */
#define SERVO_GPIO          14
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_14_BIT   /* 14-bit → 16384 steps */
#define LEDC_FREQUENCY      50                  /* 50 Hz for servo */

/*
 * SG90 pulse width:
 *   0°  → 1.0 ms  →  duty = 16384 * 1.0 / 20 =  819
 *  90°  → 1.5 ms  →  duty = 16384 * 1.5 / 20 = 1229
 * 180°  → 2.0 ms  →  duty = 16384 * 2.0 / 20 = 1638
 */
#define SERVO_DUTY_MIN      819     /* 1.0 ms pulse → 0°   */
#define SERVO_DUTY_MAX      1638    /* 2.0 ms pulse → 180° */

static const char *TAG = "servo";

void servo_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz         = LEDC_FREQUENCY,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf = {
        .gpio_num   = SERVO_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .duty       = SERVO_DUTY_MIN,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));

    ESP_LOGI(TAG, "Servo initialized on GPIO %d", SERVO_GPIO);
}

void servo_set_angle(uint8_t angle)
{
    if (angle > 180) angle = 180;

    uint32_t duty = SERVO_DUTY_MIN +
                    (uint32_t)((SERVO_DUTY_MAX - SERVO_DUTY_MIN) * angle / 180);

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}
