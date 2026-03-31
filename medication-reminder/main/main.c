#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "storage.h"
#include "time_manager.h"
#include "reminder.h"
#include "display.h"
#include "buzzer.h"
#include "wifi_manager.h"
#include "web_server.h"

static const char *TAG = "main";

#define BTN_CONFIRM  CONFIG_BTN_CONFIRM_GPIO
#define BTN_PAUSE    CONFIG_BTN_PAUSE_GPIO
#define DEBOUNCE_MS  50

static SemaphoreHandle_t s_display_mutex;

/* ------------------------------------------------------------------ */
/* Button task                                                          */
/* ------------------------------------------------------------------ */
static void button_task(void *arg)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN_CONFIRM) | (1ULL << BTN_PAUSE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    bool prev_confirm = false, prev_pause = false;

    while (1) {
        bool confirm = (bool)gpio_get_level(BTN_CONFIRM);
        bool pause   = (bool)gpio_get_level(BTN_PAUSE);

        /* Rising edge: confirm button */
        if (confirm && !prev_confirm) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            if (gpio_get_level(BTN_CONFIRM)) {
                ESP_LOGI(TAG, "Confirm button pressed");
                reminder_t *active = reminder_get_active();
                if (active && (active->state == REMINDER_STATE_TRIGGERED ||
                               active->state == REMINDER_STATE_ESCALATED)) {
                    char time_str[32];
                    char name_copy[MAX_MED_NAME_LEN];
                    time_manager_get_time_str(time_str, sizeof(time_str));
                    strncpy(name_copy, active->config.medication_name, sizeof(name_copy) - 1);
                    name_copy[sizeof(name_copy) - 1] = '\0';
                    storage_log_medication(time_str,
                                           active->config.medication_name,
                                           active->config.dosage,
                                           "taken");
                    reminder_confirm();
                    buzzer_play_confirm();
                    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
                    display_show_confirmed(name_copy);
                    xSemaphoreGive(s_display_mutex);
                    vTaskDelay(pdMS_TO_TICKS(3000));
                }
            }
        }

        /* Rising edge: pause button (silence buzzer, reminder continues) */
        if (pause && !prev_pause) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            if (gpio_get_level(BTN_PAUSE)) {
                ESP_LOGI(TAG, "Pause button pressed");
                reminder_t *active = reminder_get_active();
                if (active && (active->state == REMINDER_STATE_TRIGGERED ||
                               active->state == REMINDER_STATE_ESCALATED)) {
                    char time_str[32];
                    time_manager_get_time_str(time_str, sizeof(time_str));
                    storage_log_medication(time_str,
                                           active->config.medication_name,
                                           active->config.dosage,
                                           "snoozed");
                    buzzer_set_mode(BUZZER_OFF);
                }
            }
        }

        prev_confirm = confirm;
        prev_pause   = pause;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ------------------------------------------------------------------ */
/* Reminder check task (runs every second)                             */
/* ------------------------------------------------------------------ */
static void reminder_check_task(void *arg)
{
    while (1) {
        struct tm now;
        time_manager_get_time(&now);
        reminder_check(&now);
        reminder_check_timeout();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ------------------------------------------------------------------ */
/* Buzzer task (runs every 100 ms)                                     */
/* ------------------------------------------------------------------ */
static void buzzer_task(void *arg)
{
    while (1) {
        buzzer_tick();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ------------------------------------------------------------------ */
/* Display task (refreshes every 500 ms)                               */
/* ------------------------------------------------------------------ */
static void display_task(void *arg)
{
    char last_time[16] = {0};
    char cur_time[16];

    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
    display_show_splash();
    xSemaphoreGive(s_display_mutex);
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        reminder_t *active = reminder_get_active();
        reminder_state_t state = active ? active->state : REMINDER_STATE_IDLE;

        if (state == REMINDER_STATE_TRIGGERED || state == REMINDER_STATE_ESCALATED) {
            xSemaphoreTake(s_display_mutex, portMAX_DELAY);
            display_show_alert(active->config.medication_name,
                               active->config.dosage,
                               state == REMINDER_STATE_ESCALATED);
            xSemaphoreGive(s_display_mutex);
            buzzer_set_mode(state == REMINDER_STATE_ESCALATED ? BUZZER_ESCALATED : BUZZER_NORMAL);
        } else {
            time_manager_get_time_str(cur_time, sizeof(cur_time));
            if (strcmp(cur_time, last_time) != 0) {
                strncpy(last_time, cur_time, sizeof(last_time) - 1);
                char date_str[32], weekday_str[16];
                time_manager_get_date_str(date_str, sizeof(date_str));
                time_manager_get_weekday_str(weekday_str, sizeof(weekday_str));
                wifi_state_t ws = wifi_manager_get_state();
                const char *ip = (ws == WIFI_STATE_CONNECTED || ws == WIFI_STATE_AP_MODE)
                                 ? wifi_manager_get_ip() : NULL;
                xSemaphoreTake(s_display_mutex, portMAX_DELAY);
                display_show_standby(cur_time, date_str, weekday_str, ip);
                xSemaphoreGive(s_display_mutex);
            }
            buzzer_set_mode(BUZZER_OFF);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ------------------------------------------------------------------ */
/* app_main                                                            */
/* ------------------------------------------------------------------ */
void app_main(void)
{
    ESP_LOGI(TAG, "Medication Reminder starting...");

    ESP_ERROR_CHECK(storage_init());

    s_display_mutex = xSemaphoreCreateMutex();
    display_init();

    buzzer_init();
    buzzer_play_startup();

    ESP_ERROR_CHECK(time_manager_init());

    reminder_init();

    ESP_ERROR_CHECK(wifi_manager_init());

    web_server_start();

    xTaskCreate(display_task,        "display",   4096, NULL, 3, NULL);
    xTaskCreate(reminder_check_task, "reminder",  2048, NULL, 4, NULL);
    xTaskCreate(buzzer_task,         "buzzer",    1024, NULL, 5, NULL);
    xTaskCreate(button_task,         "button",    2048, NULL, 6, NULL);

    ESP_LOGI(TAG, "All tasks started");
}