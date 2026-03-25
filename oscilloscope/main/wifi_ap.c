#include "wifi_ap.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#define WIFI_SSID       "Oscilloscope"
#define WIFI_PASS       "12345678"
#define WIFI_CHANNEL    1
#define MAX_STA_CONN    4

static const char *TAG = "wifi_ap";

static const char *wifi_reason_to_str(uint16_t reason)
{
    switch (reason) {
        case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY:
            return "inactivity";
        case WIFI_REASON_ASSOC_LEAVE:
            return "assoc_leave";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "handshake_timeout";
        case WIFI_REASON_TIMEOUT:
            return "timeout";
        case WIFI_REASON_PEER_INITIATED:
            return "peer_initiated";
        case WIFI_REASON_AP_INITIATED:
            return "ap_initiated";
        case WIFI_REASON_SA_QUERY_TIMEOUT:
            return "sa_query_timeout";
        default:
            return "unknown";
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "SoftAP started");
    } else if (event_id == WIFI_EVENT_AP_STOP) {
        ESP_LOGW(TAG, "SoftAP stopped");
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d, reason=%u (%s)",
                 MAC2STR(event->mac), event->aid, event->reason,
                 wifi_reason_to_str(event->reason));
    }
}

void wifi_ap_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .ssid_hidden = 0,
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .beacon_interval = 100,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started. SSID:%s password:%s channel:%d PMF:optional",
             WIFI_SSID, WIFI_PASS, WIFI_CHANNEL);
}
