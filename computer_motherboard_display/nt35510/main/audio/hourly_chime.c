#include "hourly_chime.h"

#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_output.h"
#include "config.h"
#include "net/ntp_client.h"

void hourly_chime_task(void *arg)
{
    (void)arg;

    int last_yday = -1;
    int last_hour = -1;

    ESP_LOGI(TAG_AUDIO, "Hourly chime task started");

    while (1) {
        if (ntp_client_is_synced()) {
            struct tm now = {0};
            ntp_client_get_local_time(&now);

            if (now.tm_hour >= AUDIO_ANNOUNCE_START_HOUR &&
                now.tm_hour <= AUDIO_ANNOUNCE_END_HOUR &&
                now.tm_min == 0 &&
                now.tm_sec < 2 &&
                !(last_yday == now.tm_yday && last_hour == now.tm_hour)) {
                ESP_LOGI(TAG_AUDIO, "Hourly announcement at %02d:00", now.tm_hour);
                esp_err_t ret = audio_output_play_beijing_hour_announcement(now.tm_hour);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG_AUDIO, "Hourly announcement failed: %s", esp_err_to_name(ret));
                } else {
                    last_yday = now.tm_yday;
                    last_hour = now.tm_hour;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(AUDIO_ANNOUNCE_POLL_MS));
    }
}
