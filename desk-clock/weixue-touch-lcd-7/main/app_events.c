/**
 * @file app_events.c
 * @brief Application-level async event bus
 */
#include "app_events.h"

#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

ESP_EVENT_DEFINE_BASE(APP_EVENTS);

static esp_event_loop_handle_t s_app_event_loop = NULL;

esp_err_t app_events_init(void)
{
    if (s_app_event_loop) {
        return ESP_OK;
    }

    const esp_event_loop_args_t loop_args = {
        .queue_size = 24,
        .task_name = "app_evt",
        .task_priority = TASK_PRIO_APP_EVENT,
        .task_stack_size = TASK_STACK_APP_EVENT,
        .task_core_id = 0,
    };

    esp_err_t ret = esp_event_loop_create(&loop_args, &s_app_event_loop);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG_MAIN, "App event loop ready");
    }
    return ret;
}

esp_event_loop_handle_t app_events_loop(void)
{
    return s_app_event_loop;
}

esp_err_t app_events_post(int32_t event_id, const void *event_data, size_t event_data_size)
{
    if (!s_app_event_loop) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_event_post_to(s_app_event_loop, APP_EVENTS, event_id,
                             (void *)event_data, event_data_size,
                             pdMS_TO_TICKS(10));
}
