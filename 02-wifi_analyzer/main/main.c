#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "oled_display.h"
#include "web_server.h"
#include "wifi_scanner.h"

static const char *TAG = "MAIN";

#define PAGE_BTN_GPIO           GPIO_NUM_0
#define SCAN_INTERVAL_MS        10000
#define SCAN_TASK_STACK_SIZE    4096
#define BUTTON_TASK_STACK_SIZE  4096

static void task_scan(void *arg)
{
    wifi_scan_result_t result;

    while (1) {
        esp_err_t err = wifi_scanner_scan(&result);
        if (err == ESP_OK) {
            oled_display_update(&result);
            web_server_update_data(&result);
        } else {
            ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(SCAN_INTERVAL_MS));
    }
}

static void task_button(void *arg)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PAGE_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    bool last_state = true;

    while (1) {
        bool cur_state = gpio_get_level(PAGE_BTN_GPIO);

        if (!cur_state && last_state) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (!gpio_get_level(PAGE_BTN_GPIO)) {
                oled_display_next_page();
            }
        }

        last_state = cur_state;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "WiFi Analyzer boot");
    ESP_LOGI(TAG, "========================================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, reinitializing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ret = oled_display_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OLED init failed, Web UI will still run");
    }

    ESP_ERROR_CHECK(wifi_scanner_init());
    ESP_ERROR_CHECK(web_server_init());

    oled_display_show_boot(wifi_scanner_get_ap_ip());
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "Initialization complete");
    ESP_LOGI(TAG, "AP SSID=%s PASSWORD=%s", WIFI_AP_SSID, WIFI_AP_PASSWORD);
    ESP_LOGI(TAG, "Web UI: http://%s", wifi_scanner_get_ap_ip());
    ESP_LOGI(TAG, "Press BOOT (GPIO%d) to switch OLED page", (int)PAGE_BTN_GPIO);

    BaseType_t scan_task_ok = xTaskCreate(task_scan, "scan", SCAN_TASK_STACK_SIZE, NULL, 5, NULL);
    BaseType_t button_task_ok = xTaskCreate(task_button, "button", BUTTON_TASK_STACK_SIZE, NULL, 3, NULL);

    ESP_ERROR_CHECK(scan_task_ok == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(button_task_ok == pdPASS ? ESP_OK : ESP_FAIL);
}
