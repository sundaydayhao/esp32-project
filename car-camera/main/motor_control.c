#include "motor_control.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "motor";

#define PWM_TIMER       LEDC_TIMER_1
#define PWM_MODE        LEDC_LOW_SPEED_MODE
#define PWM_LEFT_CH     LEDC_CHANNEL_1
#define PWM_RIGHT_CH    LEDC_CHANNEL_2
#define PWM_RESOLUTION  LEDC_TIMER_10_BIT
#define PWM_MAX_DUTY    ((1 << 10) - 1)

static SemaphoreHandle_t s_lock = NULL;
static motor_status_t s_status = {
    .motion = CAR_MOTION_STOP,
    .speed = 0,
    .left_percent = 0,
    .right_percent = 0,
    .obstacle_blocked = false,
    .last_command_ms = 0,
};

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool gpio_is_valid_configured(int gpio)
{
    return gpio >= 0 && gpio <= 48;
}

static void configure_output(int gpio)
{
    if (!gpio_is_valid_configured(gpio)) {
        return;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

static void set_gpio_if_valid(int gpio, int level)
{
    if (gpio_is_valid_configured(gpio)) {
        gpio_set_level((gpio_num_t)gpio, level);
    }
}

static uint32_t duty_from_percent(int percent)
{
    int duty_percent = abs(percent);
    if (duty_percent > 100) {
        duty_percent = 100;
    }
    return (uint32_t)((duty_percent * PWM_MAX_DUTY) / 100);
}

static void set_wheel(int percent, int in1_gpio, int in2_gpio, ledc_channel_t channel)
{
    uint32_t duty = duty_from_percent(percent);

    if (percent > 0) {
        set_gpio_if_valid(in1_gpio, 1);
        set_gpio_if_valid(in2_gpio, 0);
    } else if (percent < 0) {
        set_gpio_if_valid(in1_gpio, 0);
        set_gpio_if_valid(in2_gpio, 1);
    } else {
        set_gpio_if_valid(in1_gpio, 0);
        set_gpio_if_valid(in2_gpio, 0);
    }

    ledc_set_duty(PWM_MODE, channel, duty);
    ledc_update_duty(PWM_MODE, channel);
}

static void apply_locked(int left_percent, int right_percent)
{
#if CONFIG_CAR_MOTOR_INVERT_LEFT
    left_percent = -left_percent;
#endif
#if CONFIG_CAR_MOTOR_INVERT_RIGHT
    right_percent = -right_percent;
#endif

    set_wheel(left_percent, CONFIG_CAR_MOTOR_LEFT_IN1_GPIO,
              CONFIG_CAR_MOTOR_LEFT_IN2_GPIO, PWM_LEFT_CH);
    set_wheel(right_percent, CONFIG_CAR_MOTOR_RIGHT_IN1_GPIO,
              CONFIG_CAR_MOTOR_RIGHT_IN2_GPIO, PWM_RIGHT_CH);

    s_status.left_percent = left_percent;
    s_status.right_percent = right_percent;
}

static bool motion_is_forwardish(car_motion_t motion)
{
    return motion == CAR_MOTION_FORWARD ||
           motion == CAR_MOTION_LEFT ||
           motion == CAR_MOTION_RIGHT;
}

static void mix_motion(car_motion_t motion, uint8_t speed, int *left, int *right)
{
    int s = speed;
    int slow = (s * 38) / 100;

    switch (motion) {
    case CAR_MOTION_FORWARD:
        *left = s;
        *right = s;
        break;
    case CAR_MOTION_BACKWARD:
        *left = -s;
        *right = -s;
        break;
    case CAR_MOTION_LEFT:
        *left = slow;
        *right = s;
        break;
    case CAR_MOTION_RIGHT:
        *left = s;
        *right = slow;
        break;
    case CAR_MOTION_SPIN_LEFT:
        *left = -s;
        *right = s;
        break;
    case CAR_MOTION_SPIN_RIGHT:
        *left = s;
        *right = -s;
        break;
    case CAR_MOTION_STOP:
    default:
        *left = 0;
        *right = 0;
        break;
    }
}

static void failsafe_task(void *arg)
{
    (void)arg;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));

        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
            continue;
        }

        uint32_t elapsed = now_ms() - s_status.last_command_ms;
        if (s_status.motion != CAR_MOTION_STOP && elapsed > CONFIG_CAR_MOTOR_FAILSAFE_MS) {
            ESP_LOGW(TAG, "failsafe stop after %lu ms", (unsigned long)elapsed);
            apply_locked(0, 0);
            s_status.motion = CAR_MOTION_STOP;
            s_status.speed = 0;
        }

        xSemaphoreGive(s_lock);
    }
}

esp_err_t motor_control_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    configure_output(CONFIG_CAR_MOTOR_LEFT_IN1_GPIO);
    configure_output(CONFIG_CAR_MOTOR_LEFT_IN2_GPIO);
    configure_output(CONFIG_CAR_MOTOR_RIGHT_IN1_GPIO);
    configure_output(CONFIG_CAR_MOTOR_RIGHT_IN2_GPIO);
    configure_output(CONFIG_CAR_MOTOR_STBY_GPIO);
    set_gpio_if_valid(CONFIG_CAR_MOTOR_STBY_GPIO, 1);

    ledc_timer_config_t timer = {
        .speed_mode = PWM_MODE,
        .timer_num = PWM_TIMER,
        .duty_resolution = PWM_RESOLUTION,
        .freq_hz = CONFIG_CAR_MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t left = {
        .speed_mode = PWM_MODE,
        .channel = PWM_LEFT_CH,
        .timer_sel = PWM_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = CONFIG_CAR_MOTOR_LEFT_PWM_GPIO,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&left));

    ledc_channel_config_t right = {
        .speed_mode = PWM_MODE,
        .channel = PWM_RIGHT_CH,
        .timer_sel = PWM_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = CONFIG_CAR_MOTOR_RIGHT_PWM_GPIO,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&right));

    s_status.last_command_ms = now_ms();
    apply_locked(0, 0);

    xTaskCreate(failsafe_task, "motor_failsafe", 3072, NULL, 5, NULL);

    ESP_LOGI(TAG, "motor ready: left pwm=%d in=%d/%d right pwm=%d in=%d/%d",
             CONFIG_CAR_MOTOR_LEFT_PWM_GPIO,
             CONFIG_CAR_MOTOR_LEFT_IN1_GPIO,
             CONFIG_CAR_MOTOR_LEFT_IN2_GPIO,
             CONFIG_CAR_MOTOR_RIGHT_PWM_GPIO,
             CONFIG_CAR_MOTOR_RIGHT_IN1_GPIO,
             CONFIG_CAR_MOTOR_RIGHT_IN2_GPIO);
    return ESP_OK;
}

esp_err_t motor_control_drive(car_motion_t motion, uint8_t speed)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (speed > 100) {
        speed = 100;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_status.last_command_ms = now_ms();

    if (motion == CAR_MOTION_STOP || speed == 0) {
        apply_locked(0, 0);
        s_status.motion = CAR_MOTION_STOP;
        s_status.speed = 0;
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }

    if (s_status.obstacle_blocked && motion_is_forwardish(motion)) {
        apply_locked(0, 0);
        s_status.motion = CAR_MOTION_STOP;
        s_status.speed = 0;
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    int left = 0;
    int right = 0;
    mix_motion(motion, speed, &left, &right);
    apply_locked(left, right);
    s_status.motion = motion;
    s_status.speed = speed;

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

void motor_control_stop(void)
{
    (void)motor_control_drive(CAR_MOTION_STOP, 0);
}

void motor_control_set_obstacle_blocked(bool blocked)
{
    if (s_lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    s_status.obstacle_blocked = blocked;
    if (blocked && motion_is_forwardish(s_status.motion)) {
        ESP_LOGW(TAG, "obstacle stop");
        apply_locked(0, 0);
        s_status.motion = CAR_MOTION_STOP;
        s_status.speed = 0;
    }

    xSemaphoreGive(s_lock);
}

void motor_control_get_status(motor_status_t *status)
{
    if (status == NULL || s_lock == NULL) {
        return;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        *status = s_status;
        xSemaphoreGive(s_lock);
    }
}

const char *car_motion_to_string(car_motion_t motion)
{
    switch (motion) {
    case CAR_MOTION_FORWARD:
        return "forward";
    case CAR_MOTION_BACKWARD:
        return "backward";
    case CAR_MOTION_LEFT:
        return "left";
    case CAR_MOTION_RIGHT:
        return "right";
    case CAR_MOTION_SPIN_LEFT:
        return "spin_left";
    case CAR_MOTION_SPIN_RIGHT:
        return "spin_right";
    case CAR_MOTION_STOP:
    default:
        return "stop";
    }
}

car_motion_t car_motion_from_string(const char *value)
{
    if (value == NULL) {
        return CAR_MOTION_STOP;
    }
    if (strcmp(value, "forward") == 0) {
        return CAR_MOTION_FORWARD;
    }
    if (strcmp(value, "backward") == 0 || strcmp(value, "back") == 0) {
        return CAR_MOTION_BACKWARD;
    }
    if (strcmp(value, "left") == 0) {
        return CAR_MOTION_LEFT;
    }
    if (strcmp(value, "right") == 0) {
        return CAR_MOTION_RIGHT;
    }
    if (strcmp(value, "spin_left") == 0) {
        return CAR_MOTION_SPIN_LEFT;
    }
    if (strcmp(value, "spin_right") == 0) {
        return CAR_MOTION_SPIN_RIGHT;
    }
    return CAR_MOTION_STOP;
}
