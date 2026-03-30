/**
 * @file dht11_sensor.c
 * @brief Bit-banged DHT11 reader for ESP32-S3
 */
#include "sensor/dht11_sensor.h"

#include <string.h>

#include "config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DHT11_RESPONSE_TIMEOUT_US 120
#define DHT11_BIT_START_TIMEOUT_US 80
#define DHT11_BIT_END_TIMEOUT_US 90
#define DHT11_SAMPLE_OFFSET_US 40

static dht11_data_t s_data = {0};
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t dht11_wait_for_level(int expected_level, int timeout_us)
{
    int64_t start = esp_timer_get_time();

    while (gpio_get_level(DHT11_DATA_PIN) != expected_level) {
        if ((esp_timer_get_time() - start) >= timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }

    return ESP_OK;
}

static esp_err_t dht11_read_once(dht11_data_t *out)
{
    uint8_t payload[5] = {0};

    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_set_direction(DHT11_DATA_PIN, GPIO_MODE_OUTPUT_OD);
    gpio_set_pull_mode(DHT11_DATA_PIN, GPIO_PULLUP_ONLY);
    gpio_set_level(DHT11_DATA_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(DHT11_DATA_PIN, 0);
    esp_rom_delay_us(DHT11_START_SIGNAL_MS * 1000);
    gpio_set_level(DHT11_DATA_PIN, 1);
    esp_rom_delay_us(30);
    gpio_set_direction(DHT11_DATA_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(DHT11_DATA_PIN, GPIO_PULLUP_ONLY);

    ESP_RETURN_ON_ERROR(dht11_wait_for_level(0, DHT11_RESPONSE_TIMEOUT_US), TAG_DHT11, "DHT11 no response(low)");
    ESP_RETURN_ON_ERROR(dht11_wait_for_level(1, DHT11_RESPONSE_TIMEOUT_US), TAG_DHT11, "DHT11 no response(high)");
    ESP_RETURN_ON_ERROR(dht11_wait_for_level(0, DHT11_RESPONSE_TIMEOUT_US), TAG_DHT11, "DHT11 start bit timeout");

    for (int bit = 0; bit < 40; bit++) {
        ESP_RETURN_ON_ERROR(dht11_wait_for_level(1, DHT11_BIT_START_TIMEOUT_US), TAG_DHT11, "DHT11 bit start timeout");
        esp_rom_delay_us(DHT11_SAMPLE_OFFSET_US);

        payload[bit / 8] <<= 1;
        if (gpio_get_level(DHT11_DATA_PIN)) {
            payload[bit / 8] |= 1;
        }

        ESP_RETURN_ON_ERROR(dht11_wait_for_level(0, DHT11_BIT_END_TIMEOUT_US), TAG_DHT11, "DHT11 bit end timeout");
    }

    if (((uint8_t)(payload[0] + payload[1] + payload[2] + payload[3])) != payload[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    memset(out, 0, sizeof(*out));
    out->humidity = payload[0];
    out->temperature_c = payload[2];
    out->valid = true;
    return ESP_OK;
}

esp_err_t dht11_sensor_init(void)
{
    const gpio_config_t io_cfg = {
        .pin_bit_mask = 1ULL << DHT11_DATA_PIN,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_GPIO(DHT11_DATA_PIN), ESP_ERR_INVALID_ARG, TAG_DHT11,
                        "invalid DHT11 pin %d", DHT11_DATA_PIN);
    ESP_RETURN_ON_ERROR(gpio_config(&io_cfg), TAG_DHT11, "gpio_config failed");
    gpio_set_level(DHT11_DATA_PIN, 1);

    ESP_LOGI(TAG_DHT11, "DHT11 initialized on GPIO%d", DHT11_DATA_PIN);
    return ESP_OK;
}

void dht11_sensor_get_data(dht11_data_t *data)
{
    if (!data) {
        return;
    }

    portENTER_CRITICAL(&s_lock);
    *data = s_data;
    portEXIT_CRITICAL(&s_lock);
}

void dht11_sensor_task(void *arg)
{
    uint32_t success_log_divider = 0;
    (void)arg;

    ESP_LOGI(TAG_DHT11, "DHT11 task started, startup delay=%dms interval=%dms",
             DHT11_STARTUP_DELAY_MS, DHT11_SAMPLE_INTERVAL_MS);
    vTaskDelay(pdMS_TO_TICKS(DHT11_STARTUP_DELAY_MS));

    while (1) {
        dht11_data_t sample = {0};
        dht11_data_t snapshot = {0};
        esp_err_t ret = dht11_read_once(&sample);

        portENTER_CRITICAL(&s_lock);
        if (ret == ESP_OK) {
            sample.sample_count = s_data.sample_count + 1;
            sample.error_count = s_data.error_count;
            sample.update_time_us = esp_timer_get_time();
            s_data = sample;
        } else {
            s_data.error_count++;
            s_data.valid = false;
        }
        snapshot = s_data;
        portEXIT_CRITICAL(&s_lock);

        if (ret == ESP_OK) {
            success_log_divider++;
            if ((success_log_divider % 10U) == 1U) {
                ESP_LOGI(TAG_DHT11, "DHT11 sample ok: %dC %u%%RH samples=%lu errors=%lu",
                         snapshot.temperature_c, snapshot.humidity,
                         (unsigned long)snapshot.sample_count,
                         (unsigned long)snapshot.error_count);
            }
        } else {
            ESP_LOGW(TAG_DHT11, "DHT11 read failed: %s errors=%lu",
                     esp_err_to_name(ret), (unsigned long)snapshot.error_count);
        }

        vTaskDelay(pdMS_TO_TICKS(DHT11_SAMPLE_INTERVAL_MS));
    }
}
