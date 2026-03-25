#include "radar.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "servo.h"
#include "ultrasonic.h"
#include "web_server.h"

static const char *TAG = "radar";

/* ---- Scan parameters ---- */
#define ANGLE_MIN       0
#define ANGLE_MAX       180
#define ANGLE_STEP      2       /* degrees per step          */
#define SETTLE_MS       40      /* ms to wait after servo move */
#define MAX_DISTANCE    100     /* cm – clamp readings above this */

static void radar_task(void *pvParameters)
{
    char json_buf[64];
    int  angle = ANGLE_MIN;
    int  dir   = 1;   /* +1 = sweeping right, -1 = sweeping left */

    ESP_LOGI(TAG, "Radar scan task started");

    while (1) {
        /* Move servo */
        servo_set_angle((uint8_t)angle);
        vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));

        /* Measure distance */
        uint32_t dist = ultrasonic_measure_cm();

        /* Clamp to max range */
        if (dist == 0 || dist > MAX_DISTANCE) {
            dist = MAX_DISTANCE;
        }

        /* Build JSON: {"a":<angle>,"d":<dist>,"dir":<1|-1>} */
        int len = snprintf(json_buf, sizeof(json_buf),
                           "{\"a\":%d,\"d\":%lu,\"dir\":%d}",
                           angle, (unsigned long)dist, dir);
        if (len > 0 && len < (int)sizeof(json_buf)) {
            ws_broadcast(json_buf);
        }

        ESP_LOGD(TAG, "angle=%3d°  dist=%4lu cm", angle, (unsigned long)dist);

        /* Advance angle */
        angle += dir * ANGLE_STEP;

        if (angle >= ANGLE_MAX) {
            angle = ANGLE_MAX;
            dir   = -1;
        } else if (angle <= ANGLE_MIN) {
            angle = ANGLE_MIN;
            dir   = 1;
        }
    }
}

void radar_start(void)
{
    servo_init();
    ultrasonic_init();

    xTaskCreate(radar_task, "radar_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Radar started");
}
