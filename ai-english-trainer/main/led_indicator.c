/**
 * @file led_indicator.c
 * @brief WS2812B RGB LED status indicator with animation states
 *
 * Uses ESP-IDF led_strip driver (RMT peripheral) to drive WS2812B.
 * A background FreeRTOS task handles animations (breathing, blinking).
 *
 * Default GPIO: 48 (built-in RGB LED on ESP32-S3-DevKitC-1)
 */

#include "led_indicator.h"
#include "sdkconfig.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "led_indicator";

/* LED strip handle */
static led_strip_handle_t s_led_strip = NULL;

/* Animation task */
static TaskHandle_t  s_anim_task   = NULL;
static volatile led_state_t s_current_state = LED_STATE_OFF;
static volatile bool s_state_changed = false;

/* Custom RGB for LED_STATE_OFF override */
static volatile uint8_t s_custom_r = 0;
static volatile uint8_t s_custom_g = 0;
static volatile uint8_t s_custom_b = 0;
static volatile bool    s_custom_color = false;

/* ------------------------------------------------------------------ */
/*  Helper: set single LED pixel                                        */
/* ------------------------------------------------------------------ */

static void set_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_led_strip) return;
    led_strip_set_pixel(s_led_strip, 0, r, g, b);
    led_strip_refresh(s_led_strip);
}

static void clear_pixel(void)
{
    if (!s_led_strip) return;
    led_strip_clear(s_led_strip);
    led_strip_refresh(s_led_strip);
}

/* ------------------------------------------------------------------ */
/*  Animation helpers                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Breathing effect: smoothly fade in/out
 * @param r,g,b  Peak color
 * @param period_ms  Full breath period in ms
 * @param steps  Number of steps per half-period
 */
static void anim_breathe_step(uint8_t r, uint8_t g, uint8_t b,
                               int step, int total_steps)
{
    /* Use sine wave for smooth breathing */
    float phase = (float)step / (float)total_steps * 3.14159f;
    float brightness = sinf(phase);
    if (brightness < 0) brightness = 0;

    set_pixel((uint8_t)(r * brightness),
              (uint8_t)(g * brightness),
              (uint8_t)(b * brightness));
}

/* ------------------------------------------------------------------ */
/*  Animation Task                                                       */
/* ------------------------------------------------------------------ */

static void animation_task(void *arg)
{
    int step = 0;

    while (1) {
        led_state_t state = s_current_state;

        if (s_custom_color) {
            set_pixel(s_custom_r, s_custom_g, s_custom_b);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        switch (state) {
            case LED_STATE_OFF:
                clear_pixel();
                vTaskDelay(pdMS_TO_TICKS(100));
                step = 0;
                break;

            case LED_STATE_IDLE:
                /* Slow breathing green (3 second period, 60 steps) */
                anim_breathe_step(0, 40, 0, step % 60, 60);
                step++;
                vTaskDelay(pdMS_TO_TICKS(50));  /* 50ms * 60 = 3s period */
                break;

            case LED_STATE_WIFI_CONNECT:
                /* Fast white blink (200ms on, 200ms off) */
                if ((step % 4) < 2) {
                    set_pixel(30, 30, 30);
                } else {
                    clear_pixel();
                }
                step++;
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case LED_STATE_RECORDING:
                /* Fast red blink (100ms on, 100ms off) */
                if (step % 2 == 0) {
                    set_pixel(80, 0, 0);
                } else {
                    clear_pixel();
                }
                step++;
                vTaskDelay(pdMS_TO_TICKS(150));
                break;

            case LED_STATE_PROCESSING:
                /* Yellow breathing pulse */
                anim_breathe_step(60, 40, 0, step % 30, 30);
                step++;
                vTaskDelay(pdMS_TO_TICKS(50));  /* 50ms * 30 = 1.5s period */
                break;

            case LED_STATE_PLAYING:
                /* Solid blue with gentle pulse */
                anim_breathe_step(0, 10, 80, step % 40, 40);
                step++;
                vTaskDelay(pdMS_TO_TICKS(50));
                break;

            case LED_STATE_SUCCESS:
                /* Bright green flash x3 then back to idle */
                for (int i = 0; i < 3; i++) {
                    set_pixel(0, 100, 0);
                    vTaskDelay(pdMS_TO_TICKS(150));
                    clear_pixel();
                    vTaskDelay(pdMS_TO_TICKS(150));
                }
                s_current_state = LED_STATE_IDLE;
                step = 0;
                break;

            case LED_STATE_ERROR:
                /* Solid red */
                set_pixel(80, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            default:
                clear_pixel();
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }

        /* Reset step counter on state change */
        if (s_state_changed) {
            s_state_changed = false;
            step = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

esp_err_t led_indicator_init(void)
{
    ESP_LOGI(TAG, "Initializing WS2812 LED on GPIO%d", CONFIG_LED_GPIO);

    /* Configure LED strip */
    led_strip_config_t strip_config = {
        .strip_gpio_num   = CONFIG_LED_GPIO,
        .max_leds         = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model        = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src        = RMT_CLK_SRC_DEFAULT,
        .resolution_hz  = 10 * 1000 * 1000,  /* 10 MHz */
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Clear LED on startup */
    led_strip_clear(s_led_strip);
    led_strip_refresh(s_led_strip);

    /* Start animation task */
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        animation_task, "led_anim",
        2048, NULL, 3,
        &s_anim_task, 0);

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED animation task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LED indicator initialized OK");
    return ESP_OK;
}

void led_indicator_set_state(led_state_t state)
{
    if (s_current_state != state) {
        ESP_LOGI(TAG, "LED state: %d -> %d", s_current_state, state);
        s_custom_color  = false;
        s_current_state = state;
        s_state_changed = true;
    }
}

void led_indicator_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    s_custom_r     = r;
    s_custom_g     = g;
    s_custom_b     = b;
    s_custom_color = true;
    s_state_changed = true;
}

void led_indicator_off(void)
{
    s_custom_color  = false;
    s_current_state = LED_STATE_OFF;
    s_state_changed = true;
}

esp_err_t led_indicator_deinit(void)
{
    if (s_anim_task) {
        vTaskDelete(s_anim_task);
        s_anim_task = NULL;
    }
    if (s_led_strip) {
        led_strip_clear(s_led_strip);
        led_strip_del(s_led_strip);
        s_led_strip = NULL;
    }
    return ESP_OK;
}