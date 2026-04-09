#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_ap.h"
#include "http_server.h"
#include "joystick.h"

static const char *TAG = "main";

/* -----------------------------------------------------------------------
 * Joystick polling task
 *
 * Polls the joystick every POLL_MS milliseconds.
 * Only sends a WebSocket message when the direction changes, which avoids
 * flooding the browser with repeated identical events.
 * A NONE message is sent once when the stick returns to centre so the
 * game can release any held key.
 * ----------------------------------------------------------------------- */
#define POLL_MS          50   /* polling interval in ms          */
#define REPEAT_MS       200   /* repeat interval while held (ms) */

static void joystick_task(void *arg)
{
    joystick_dir_t last_dir    = JOY_NONE;
    TickType_t     last_repeat = 0;

    while (1) {
        joystick_dir_t dir = joystick_read();
        TickType_t     now = xTaskGetTickCount();

        if (dir != last_dir) {
            /* Direction changed – send immediately */
            const char *msg = joystick_dir_to_json(dir);
            ws_broadcast(msg);
            ESP_LOGD(TAG, "Joy: %s", msg);
            last_dir    = dir;
            last_repeat = now;
        } else if (dir != JOY_NONE) {
            /* Same non-neutral direction – send repeat after REPEAT_MS */
            if ((now - last_repeat) * portTICK_PERIOD_MS >= REPEAT_MS) {
                ws_broadcast(joystick_dir_to_json(dir));
                last_repeat = now;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

/* -----------------------------------------------------------------------
 * Application entry point
 * ----------------------------------------------------------------------- */
void app_main(void)
{
    /* Initialise NVS (required by WiFi driver) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== ESP32 GameStick ===");

    /* Start WiFi Access Point */
    wifi_init_softap();

    /* Initialise joystick hardware */
    joystick_init();

    /* Start HTTP + WebSocket server */
    http_server_start();

    /* Start joystick polling task */
    xTaskCreate(joystick_task, "joystick", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Ready!  Connect to WiFi \"GameStick\" (password: 12345678)");
    ESP_LOGI(TAG, "Then open http://192.168.4.1 in your browser.");
}