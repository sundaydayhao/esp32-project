/**
 * @file button_handler.c
 * @brief Push-to-talk button with debounce and long-press detection
 *
 * Uses GPIO interrupt + FreeRTOS task for debounce.
 * Default: GPIO0 (BOOT button on ESP32-S3-DevKitC-1)
 *
 * Button wiring:
 *   GPIO0 -> button -> GND
 *   (internal pull-up enabled, active LOW)
 */

#include "button_handler.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "button_handler";

#define DEBOUNCE_MS         20      /* Debounce time in milliseconds */
#define LONG_PRESS_MS       3000    /* Long press threshold in milliseconds */

/* GPIO interrupt event queue */
static QueueHandle_t    s_gpio_evt_queue = NULL;
static TaskHandle_t     s_button_task    = NULL;
static button_callback_t s_callback      = NULL;
static volatile bool    s_is_pressed     = false;

/* ------------------------------------------------------------------ */
/*  GPIO ISR Handler                                                    */
/* ------------------------------------------------------------------ */

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    xQueueSendFromISR(s_gpio_evt_queue, &gpio_num, NULL);
}

/* ------------------------------------------------------------------ */
/*  Button Task (handles debounce + long press)                         */
/* ------------------------------------------------------------------ */

static void button_task(void *arg)
{
    uint32_t gpio_num;
    int64_t  press_time_us = 0;

    while (1) {
        if (xQueueReceive(s_gpio_evt_queue, &gpio_num, portMAX_DELAY)) {
            /* Debounce: wait and re-read */
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));

            int level = gpio_get_level((gpio_num_t)gpio_num);

            if (level == 0) {
                /* Button pressed (active LOW) */
                if (!s_is_pressed) {
                    s_is_pressed   = true;
                    press_time_us  = esp_timer_get_time();
                    ESP_LOGI(TAG, "Button PRESSED");
                    if (s_callback) {
                        s_callback(BUTTON_EVENT_PRESSED);
                    }
                }
            } else {
                /* Button released */
                if (s_is_pressed) {
                    s_is_pressed = false;
                    int64_t held_ms = (esp_timer_get_time() - press_time_us) / 1000;
                    ESP_LOGI(TAG, "Button RELEASED (held %lld ms)", held_ms);

                    if (held_ms >= LONG_PRESS_MS) {
                        if (s_callback) {
                            s_callback(BUTTON_EVENT_LONG_PRESS);
                        }
                    } else {
                        if (s_callback) {
                            s_callback(BUTTON_EVENT_RELEASED);
                        }
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

esp_err_t button_handler_init(button_callback_t callback)
{
    ESP_LOGI(TAG, "Initializing button on GPIO%d", CONFIG_BUTTON_GPIO);

    s_callback = callback;

    /* Configure GPIO */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BUTTON_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,  /* Detect both press and release */
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* Create event queue */
    s_gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    if (!s_gpio_evt_queue) {
        ESP_LOGE(TAG, "Failed to create GPIO event queue");
        return ESP_ERR_NO_MEM;
    }

    /* Install GPIO ISR service */
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)CONFIG_BUTTON_GPIO,
                          gpio_isr_handler,
                          (void *)(uintptr_t)CONFIG_BUTTON_GPIO);

    /* Create button handling task */
    BaseType_t ret = xTaskCreatePinnedToCore(
        button_task, "btn_task",
        2048, NULL, 10,
        &s_button_task, 0);  /* Run on core 0 */

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Button handler initialized OK");
    return ESP_OK;
}

bool button_handler_is_pressed(void)
{
    return s_is_pressed;
}

esp_err_t button_handler_deinit(void)
{
    gpio_isr_handler_remove((gpio_num_t)CONFIG_BUTTON_GPIO);

    if (s_button_task) {
        vTaskDelete(s_button_task);
        s_button_task = NULL;
    }
    if (s_gpio_evt_queue) {
        vQueueDelete(s_gpio_evt_queue);
        s_gpio_evt_queue = NULL;
    }

    s_callback   = NULL;
    s_is_pressed = false;
    return ESP_OK;
}