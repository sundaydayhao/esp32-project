#include "camera_init.h"
#include "email_sender.h"
#include "sdkconfig.h"
#include "wifi_connect.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "chaobiao_main";
static int64_t s_system_start_time_us = 0;

#if CONFIG_CAPTURE_INTERVAL_DEBUG_SECONDS > 0
#define CAPTURE_INTERVAL_MS    (CONFIG_CAPTURE_INTERVAL_DEBUG_SECONDS * 1000ULL)
#define CAPTURE_INTERVAL_VALUE CONFIG_CAPTURE_INTERVAL_DEBUG_SECONDS
#define CAPTURE_INTERVAL_UNIT  "second(s)"
#else
#define CAPTURE_INTERVAL_MS  (CONFIG_CAPTURE_INTERVAL_MINUTES * 60 * 1000ULL)
#define CAPTURE_INTERVAL_VALUE CONFIG_CAPTURE_INTERVAL_MINUTES
#define CAPTURE_INTERVAL_UNIT  "minute(s)"
#endif

#define STARTUP_DELAY_MS     3000

static void wait_until_second_capture_target(void)
{
    const int64_t elapsed_ms = (esp_timer_get_time() - s_system_start_time_us) / 1000;

    if (elapsed_ms >= (int64_t)CAPTURE_INTERVAL_MS) {
        ESP_LOGI(TAG,
                 "Second capture target already reached: elapsed=%lld ms target=%llu ms",
                 (long long)elapsed_ms,
                 (unsigned long long)CAPTURE_INTERVAL_MS);
        return;
    }

    const int64_t remaining_ms = (int64_t)CAPTURE_INTERVAL_MS - elapsed_ms;
    ESP_LOGI(TAG,
             "Waiting %lld ms until the second capture target after startup",
             (long long)remaining_ms);
    vTaskDelay(pdMS_TO_TICKS((uint32_t)remaining_ms));
}

static void do_capture_and_send(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Starting capture and upload");
    ESP_LOGI(TAG, "========================================");

    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi is disconnected, reconnecting");
        esp_err_t wifi_ret = wifi_connect();
        if (wifi_ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi reconnect failed, skipping this capture");
            return;
        }
    }

    camera_fb_t *fb = camera_capture();
    if (fb == NULL) {
        ESP_LOGE(TAG, "Camera capture failed");
        email_send_text("Camera capture failed. Check the ESP32-CAM camera connection.");
        return;
    }

    ESP_LOGI(TAG, "Capture complete: %zu bytes", fb->len);
    ESP_LOGI(TAG, "Sending capture email");

    esp_err_t send_ret = email_send_camera_frame(fb);

    if (send_ret == ESP_OK) {
        ESP_LOGI(TAG, "Capture email sent successfully");
    } else if (send_ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Capture email skipped because SMTP configuration is incomplete");
    } else if (send_ret == ESP_ERR_INVALID_RESPONSE) {
        ESP_LOGE(TAG, "Captured image is invalid and could not be converted to email attachment");
        email_send_text("Captured image is invalid. Check camera wiring, IO0/XCLK, and power.");
    } else {
        ESP_LOGE(TAG, "Capture email failed: %s", esp_err_to_name(send_ret));
        email_send_text("Image email failed. Check WiFi and SMTP configuration.");
    }

    esp_camera_fb_return(fb);

    ESP_LOGI(TAG, "Task complete, next capture in %d %s",
             CAPTURE_INTERVAL_VALUE, CAPTURE_INTERVAL_UNIT);
}

static void chaobiao_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "Waiting %d second(s) before first capture",
             STARTUP_DELAY_MS / 1000);
    vTaskDelay(pdMS_TO_TICKS(STARTUP_DELAY_MS));

    ESP_LOGI(TAG, "Sending first capture email after startup");
    do_capture_and_send();

    ESP_LOGI(TAG, "Second capture target is %d %s after startup",
             CAPTURE_INTERVAL_VALUE, CAPTURE_INTERVAL_UNIT);
    wait_until_second_capture_target();

    ESP_LOGI(TAG, "Sending second and final capture email");
    do_capture_and_send();

    ESP_LOGI(TAG, "Final capture complete, no more emails will be sent");
    vTaskDelete(NULL);
}

void app_main(void)
{
    s_system_start_time_us = esp_timer_get_time();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32-CAM Gas Meter Camera Push");
    ESP_LOGI(TAG, "  Version: 1.0.0");
    ESP_LOGI(TAG, "========================================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs to be erased, reinitializing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    ESP_LOGI(TAG, "Connecting to WiFi: %s", CONFIG_WIFI_SSID);
    ret = wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initial WiFi connection failed");
        ESP_LOGE(TAG, "The system will continue running, but uploads will fail");
    }

    ESP_LOGI(TAG, "Initializing camera");
    ret = camera_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s (0x%x)", esp_err_to_name(ret), ret);
        if (wifi_is_connected()) {
            email_send_text("ESP32-CAM camera init failed. Check wiring and camera module.");
        }

        while (1) {
            ESP_LOGE(TAG, "Camera fault, system halted");
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Configuration:");
    ESP_LOGI(TAG, "  WiFi SSID: %s", CONFIG_WIFI_SSID);
    ESP_LOGI(TAG, "  Second capture target after startup: %d %s",
             CAPTURE_INTERVAL_VALUE, CAPTURE_INTERVAL_UNIT);
    ESP_LOGI(TAG, "  Frame size: %d", CONFIG_CAMERA_FRAME_SIZE);
    ESP_LOGI(TAG, "  JPEG quality: %d", CONFIG_CAMERA_JPEG_QUALITY);
    ESP_LOGI(TAG, "  Notify method: SMTP email");
    ESP_LOGI(TAG, "  SMTP server: %s:%s", CONFIG_SMTP_SERVER, CONFIG_SMTP_PORT);
    ESP_LOGI(TAG, "  Recipient: %s", CONFIG_SMTP_RECIPIENT_EMAIL);
    ESP_LOGI(TAG, "========================================");

    xTaskCreate(
        chaobiao_task,
        "chaobiao_task",
        8192,
        NULL,
        5,
        NULL);

    ESP_LOGI(TAG, "System init complete, main task started");
}
