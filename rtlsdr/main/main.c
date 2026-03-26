#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"

#include "config.h"
#include "wifi_manager.h"
#include "adsb/adsb_decoder.h"
#include "rtlsdr_usb.h"
#include "web_server.h"
#include "fm/fm_receiver.h"
#include "spectrum/spectrum_analyzer.h"
#include "ais/ais_decoder.h"
#include "drone/drone_detector.h"

static const char *TAG = "main";

// ---------------------------------------------------------------------------
// 按键处理：按下 BOOT 键切换到 1090 MHz ADS-B
// ---------------------------------------------------------------------------
static void IRAM_ATTR button_isr_handler(void *arg)
{
    static uint32_t last_press = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - last_press < 300) return;
    last_press = now;

    BaseType_t wakeup = pdFALSE;
    TaskHandle_t task = (TaskHandle_t)arg;
    if (task) vTaskNotifyGiveFromISR(task, &wakeup);
    portYIELD_FROM_ISR(wakeup);
}

static void button_task(void *arg)
{
    ESP_LOGI(TAG, "Button task started (GPIO%d = switch to ADS-B)", BUTTON_GPIO);

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (fm_receiver_is_active()) {
            ESP_LOGI(TAG, "Button pressed: stopping FM, switching to ADS-B 1090 MHz");
            fm_receiver_stop();
        } else {
            ESP_LOGI(TAG, "Button pressed: switching RTL-SDR to 1090 MHz ADS-B");
            if (rtlsdr_get_state() >= RTLSDR_STATE_CONFIGURED) {
                rtlsdr_stop_streaming();
                vTaskDelay(pdMS_TO_TICKS(100));
                rtlsdr_set_freq(RTLSDR_ADSB_FREQ);
                rtlsdr_start_streaming();
                ESP_LOGI(TAG, "RTL-SDR tuned to 1090.000 MHz");
            } else {
                ESP_LOGW(TAG, "RTL-SDR not connected yet");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 启动横幅
// ---------------------------------------------------------------------------
static void print_banner(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  ESP32-S3 ADS-B + FM Receiver");
    ESP_LOGI(TAG, "  RTL-SDR USB Host");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "USB Host: GPIO19(D-) / GPIO20(D+)");
    ESP_LOGI(TAG, "Button:   GPIO%d (switch to ADS-B)", BUTTON_GPIO);
    ESP_LOGI(TAG, "WiFi AP:  %s / %s", WIFI_AP_SSID, WIFI_AP_PASSWORD);
    ESP_LOGI(TAG, "ADS-B:    http://192.168.4.1/");
    ESP_LOGI(TAG, "FM Radio: http://192.168.4.1/fm/");
    ESP_LOGI(TAG, "============================================");
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------
void app_main(void)
{
    print_banner();

    // NVS 初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // WiFi 初始化
#if WIFI_STA_ENABLE
    ret = wifi_manager_init_sta(WIFI_STA_SSID, WIFI_STA_PASSWORD);
    if (ret != ESP_OK) wifi_manager_init_ap();
#else
    ESP_ERROR_CHECK(wifi_manager_init_ap());
#endif

    char ip_buf[16];
    wifi_manager_get_ip(ip_buf, sizeof(ip_buf));
    ESP_LOGI(TAG, "IP: %s", ip_buf);

    // ADS-B 解码器初始化
    ESP_ERROR_CHECK(adsb_decoder_init());

    // FM 接收器初始化
    ESP_ERROR_CHECK(fm_receiver_init());

    // 频谱仪初始化
    ESP_ERROR_CHECK(spectrum_analyzer_init());

    // AIS 解码器初始化
    ESP_ERROR_CHECK(ais_decoder_init());

    // 无人机检测器初始化
    ESP_ERROR_CHECK(drone_detector_init());

    // 配置按键 GPIO（BOOT 键，低电平有效）
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&btn_cfg);
    gpio_install_isr_service(0);

    TaskHandle_t btn_task_hdl = NULL;
    xTaskCreate(button_task, "button", 2048, NULL, 4, &btn_task_hdl);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, (void *)btn_task_hdl);

    // 初始化 RTL-SDR USB Host 驱动
    ESP_ERROR_CHECK(rtlsdr_init());

    // 启动 Web 服务器（内部会注册 FM 处理器）
    ESP_ERROR_CHECK(web_server_init());

    ESP_LOGI(TAG, "System ready!");
    ESP_LOGI(TAG, "  ADS-B:    http://%s/", ip_buf);
    ESP_LOGI(TAG, "  FM:       http://%s/fm/", ip_buf);
    ESP_LOGI(TAG, "  Spectrum: http://%s/spectrum/", ip_buf);
    ESP_LOGI(TAG, "  AIS:      http://%s/ais/", ip_buf);
    ESP_LOGI(TAG, "  Drone:    http://%s/drone/", ip_buf);

    // 主循环：每 30 秒打印统计
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        adsb_stats_t stats;
        adsb_get_stats(&stats);
        rtlsdr_state_t rtl_state = rtlsdr_get_state();

        const char *state_str[] = {"DISCONNECTED", "CONNECTED", "CONFIGURED", "STREAMING"};
        ESP_LOGI(TAG, "RTL-SDR: %s @ %.3f MHz",
                 state_str[rtl_state], rtlsdr_get_freq() / 1e6);

        if (fm_receiver_is_active()) {
            ESP_LOGI(TAG, "Mode: FM @ %.1f MHz", fm_receiver_get_freq() / 1e6);
        } else if (spectrum_analyzer_is_active()) {
            ESP_LOGI(TAG, "Mode: Spectrum @ %.3f MHz / %.0f kHz BW",
                     spectrum_analyzer_get_freq() / 1e6, spectrum_analyzer_get_bw() / 1e3);
        } else {
            ESP_LOGI(TAG, "Mode: ADS-B | Aircraft: %lu  Messages: %lu  CRC errors: %lu",
                     stats.aircraft_count, stats.valid_messages, stats.crc_errors);
        }
        ESP_LOGI(TAG, "Free heap: %lu bytes", (uint32_t)esp_get_free_heap_size());
    }
}