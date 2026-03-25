#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_ap.h"
#include "http_server.h"
#include "adc_sampler.h"

static const char *TAG = "oscilloscope_main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 Oscilloscope Starting...");

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize WiFi Access Point */
    ESP_LOGI(TAG, "Initializing WiFi AP...");
    wifi_ap_init();

    /* Let the SoftAP beacon before starting heavier tasks. */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Start HTTP + WebSocket server */
    ESP_LOGI(TAG, "Starting HTTP server...");
    http_server_start();

    /* Initialize ADC sampler after networking is up. */
    ESP_LOGI(TAG, "Initializing ADC sampler...");
    adc_sampler_init();

    ESP_LOGI(TAG, "Oscilloscope ready!");
    ESP_LOGI(TAG, "Connect to WiFi SSID: Oscilloscope, Password: 12345678");
    ESP_LOGI(TAG, "Open browser and navigate to http://192.168.4.1");

    /* Main loop - monitor system status */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    }
}
