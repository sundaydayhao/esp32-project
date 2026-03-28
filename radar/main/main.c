#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_ap.h"
#include "web_server.h"
#include "radar.h"

static const char *TAG = "main";

void app_main(void)
{
    /* Initialize NVS (required by WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== ESP32-S3 Ultrasonic Radar ===");
    ESP_LOGI(TAG, "Hardware:");
    ESP_LOGI(TAG, "  Servo  SIGNAL -> GPIO 14");
    ESP_LOGI(TAG, "  HC-SR04 TRIG  -> GPIO 16");
    ESP_LOGI(TAG, "  HC-SR04 ECHO  -> GPIO 17");

    /* Start WiFi Access Point */
    wifi_ap_init();

    /* Start HTTP + WebSocket server */
    web_server_start();

    /* Start radar scanning task */
    radar_start();

    ESP_LOGI(TAG, "System ready. Connect to WiFi 'ESP32-Radar' (pw: 12345678)");
    ESP_LOGI(TAG, "Then open http://192.168.4.1 in your browser.");
}
