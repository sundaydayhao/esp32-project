#include "camera_app.h"
#include "audio_intercom.h"
#include "motor_control.h"
#include "ultrasonic.h"
#include "web_server.h"
#include "wifi_ap.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "car_main";

static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }
}

static void status_led_task(void *arg)
{
    (void)arg;

    if (CONFIG_CAR_STATUS_LED_GPIO < 0) {
        vTaskDelete(NULL);
        return;
    }

    gpio_config_t led = {
        .pin_bit_mask = 1ULL << CONFIG_CAR_STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led);

    while (true) {
        gpio_set_level((gpio_num_t)CONFIG_CAR_STATUS_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(80));
        gpio_set_level((gpio_num_t)CONFIG_CAR_STATUS_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(920));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 smart camera car booting");

    init_nvs();

    ESP_ERROR_CHECK(motor_control_init());
    ESP_ERROR_CHECK(ultrasonic_init());
    esp_err_t audio_ret = audio_intercom_init();
    if (audio_ret != ESP_OK) {
        ESP_LOGE(TAG, "audio intercom unavailable: %s", esp_err_to_name(audio_ret));
    }
    ESP_ERROR_CHECK(wifi_ap_start());

    esp_err_t cam_ret = camera_app_init();
    if (cam_ret != ESP_OK) {
        ESP_LOGE(TAG, "camera unavailable, web control will still work");
    }

    ESP_ERROR_CHECK(web_server_start());
    xTaskCreate(status_led_task, "status_led", 2048, NULL, 1, NULL);

    ESP_LOGI(TAG, "ready: connect to Wi-Fi '%s' and open http://192.168.4.1",
             CONFIG_CAR_WIFI_AP_SSID);
}
