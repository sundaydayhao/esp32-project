#include "wifi_ap.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "wifi_ap";

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;

    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " joined, aid=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " left, aid=%d", MAC2STR(event->mac), event->aid);
    }
}

esp_err_t wifi_ap_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .channel = CONFIG_CAR_WIFI_AP_CHANNEL,
            .max_connection = CONFIG_CAR_WIFI_MAX_STA,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    strlcpy((char *)wifi_config.ap.ssid, CONFIG_CAR_WIFI_AP_SSID, sizeof(wifi_config.ap.ssid));
    strlcpy((char *)wifi_config.ap.password, CONFIG_CAR_WIFI_AP_PASSWORD, sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = strlen(CONFIG_CAR_WIFI_AP_SSID);

    if (strlen(CONFIG_CAR_WIFI_AP_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started: ssid=%s password=%s channel=%d",
             CONFIG_CAR_WIFI_AP_SSID,
             strlen(CONFIG_CAR_WIFI_AP_PASSWORD) ? CONFIG_CAR_WIFI_AP_PASSWORD : "(open)",
             CONFIG_CAR_WIFI_AP_CHANNEL);
    ESP_LOGI(TAG, "open http://192.168.4.1");
    return ESP_OK;
}
