#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"

#include "wifi_manager.h"
#include "audio_player.h"
#include "fft_analyzer.h"
#include "web_server.h"

static const char *TAG = "main";

/* ── FFT → WebSocket callback ────────────────────────────────────────── */
static void on_spectrum(const fft_spectrum_t *spectrum, void *user_data)
{
    web_server_broadcast_spectrum(spectrum);
}

/* ── Mount SPIFFS ────────────────────────────────────────────────────── */
static esp_err_t spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/spiffs",
        .partition_label        = NULL,   /* use first spiffs partition */
        .max_files              = 10,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "SPIFFS partition not found");
        } else {
            ESP_LOGE(TAG, "SPIFFS init error: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: total=%d, used=%d", total, used);
    return ESP_OK;
}

/* ── app_main ────────────────────────────────────────────────────────── */
void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 Music Spectrum Player ===");

    /* 1. NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. SPIFFS */
    ESP_ERROR_CHECK(spiffs_init());

    /* 3. WiFi */
    ESP_ERROR_CHECK(wifi_manager_init());
    ret = wifi_manager_connect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi connection failed – continuing without network");
    } else {
        char ip[16];
        wifi_manager_get_ip(ip, sizeof(ip));
        ESP_LOGI(TAG, "Connected! Open http://%s in your browser", ip);
    }

    /* 4. FFT analyzer */
    ESP_ERROR_CHECK(fft_analyzer_init());
    fft_analyzer_set_callback(on_spectrum, NULL);

    /* 5. Audio player */
    ESP_ERROR_CHECK(audio_player_init());

    /* 6. Web server */
    ESP_ERROR_CHECK(web_server_start());

    ESP_LOGI(TAG, "System ready.");

    /* Keep main task alive (all work is done in sub-tasks) */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        /* Periodic status log */
        audio_status_t status;
        audio_player_get_status(&status);
        if (status.state == AUDIO_STATE_PLAYING) {
            ESP_LOGI(TAG, "Playing: %s - %s  [%lus]",
                     status.current_track.artist,
                     status.current_track.title,
                     (unsigned long)status.position_sec);
        }
    }
}