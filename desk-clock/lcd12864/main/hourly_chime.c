#include "hourly_chime.h"

#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_output.h"
#include "ntp_client.h"

static const char *TAG = "hourly_chime";

void hourly_chime_task(void *arg)
{
    (void)arg;

    int last_yday = -1;
    int last_hour = -1;
    bool startup_announced = false;

    ESP_LOGI(TAG, "Hourly chime task started");

    while (1) {
        if (ntp_client_is_synced()) {
            struct tm now = {0};
            ntp_client_get_time(&now);

            if (!startup_announced) {
                esp_err_t ret = audio_output_play_beijing_time_announcement(now.tm_hour, now.tm_min);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Startup announcement failed: %s", esp_err_to_name(ret));
                } else {
                    startup_announced = true;
                    last_yday = now.tm_yday;
                    last_hour = (now.tm_min == 0) ? now.tm_hour : -1;
                }
            }

            if (now.tm_hour >= CONFIG_AUDIO_ANNOUNCE_START_HOUR &&
                now.tm_hour <= CONFIG_AUDIO_ANNOUNCE_END_HOUR &&
                now.tm_min == 0 &&
                now.tm_sec < 2 &&
                !(last_yday == now.tm_yday && last_hour == now.tm_hour)) {
                esp_err_t ret = audio_output_play_beijing_time_announcement(now.tm_hour, now.tm_min);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Hourly announcement failed: %s", esp_err_to_name(ret));
                } else {
                    last_yday = now.tm_yday;
                    last_hour = now.tm_hour;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
