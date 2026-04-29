#include "ultrasonic.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "motor_control.h"

static const char *TAG = "ultrasonic";

static volatile int s_distance_cm = -1;
static volatile bool s_blocked = false;

#if CONFIG_CAR_ENABLE_ULTRASONIC
static bool wait_for_level(int level, int timeout_us, int64_t *timestamp)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level((gpio_num_t)CONFIG_CAR_ULTRASONIC_ECHO_GPIO) != level) {
        if ((esp_timer_get_time() - start) > timeout_us) {
            return false;
        }
    }
    if (timestamp != NULL) {
        *timestamp = esp_timer_get_time();
    }
    return true;
}

static int measure_cm(void)
{
    gpio_set_level((gpio_num_t)CONFIG_CAR_ULTRASONIC_TRIG_GPIO, 0);
    esp_rom_delay_us(2);
    gpio_set_level((gpio_num_t)CONFIG_CAR_ULTRASONIC_TRIG_GPIO, 1);
    esp_rom_delay_us(10);
    gpio_set_level((gpio_num_t)CONFIG_CAR_ULTRASONIC_TRIG_GPIO, 0);

    int64_t echo_start = 0;
    int64_t echo_end = 0;

    if (!wait_for_level(1, 30000, &echo_start)) {
        return -1;
    }
    if (!wait_for_level(0, 30000, &echo_end)) {
        return -1;
    }

    int64_t pulse_us = echo_end - echo_start;
    if (pulse_us <= 0) {
        return -1;
    }

    return (int)((pulse_us + 29) / 58);
}

static void ultrasonic_task(void *arg)
{
    (void)arg;

    while (true) {
        int cm = measure_cm();
        if (cm > 0) {
            s_distance_cm = cm;
            bool blocked = cm <= CONFIG_CAR_OBSTACLE_STOP_CM;
            s_blocked = blocked;
            motor_control_set_obstacle_blocked(blocked);
        } else {
            s_distance_cm = -1;
            s_blocked = false;
            motor_control_set_obstacle_blocked(false);
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_CAR_ULTRASONIC_PERIOD_MS));
    }
}
#endif

esp_err_t ultrasonic_init(void)
{
#if CONFIG_CAR_ENABLE_ULTRASONIC
    gpio_config_t trig = {
        .pin_bit_mask = 1ULL << CONFIG_CAR_ULTRASONIC_TRIG_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&trig));
    gpio_set_level((gpio_num_t)CONFIG_CAR_ULTRASONIC_TRIG_GPIO, 0);

    gpio_config_t echo = {
        .pin_bit_mask = 1ULL << CONFIG_CAR_ULTRASONIC_ECHO_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&echo));

    xTaskCreate(ultrasonic_task, "ultrasonic", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "HC-SR04 ready: trig=%d echo=%d stop=%dcm",
             CONFIG_CAR_ULTRASONIC_TRIG_GPIO,
             CONFIG_CAR_ULTRASONIC_ECHO_GPIO,
             CONFIG_CAR_OBSTACLE_STOP_CM);
#else
    ESP_LOGI(TAG, "ultrasonic disabled");
#endif
    return ESP_OK;
}

int ultrasonic_get_distance_cm(void)
{
    return s_distance_cm;
}

bool ultrasonic_is_blocked(void)
{
    return s_blocked;
}
