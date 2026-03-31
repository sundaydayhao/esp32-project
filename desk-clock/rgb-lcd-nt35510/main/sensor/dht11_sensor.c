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
#include "net/ntp_client.h"
#include <time.h>

#define DHT11_RESPONSE_TIMEOUT_US 200
#define DHT11_BIT_START_TIMEOUT_US 120
#define DHT11_BIT_END_TIMEOUT_US 120
#define DHT11_RELEASE_TO_RESPONSE_US 40
#define DHT11_SAMPLE_OFFSET_US 40

static dht11_data_t s_data = {0};
static portMUX_TYPE s_data_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_io_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_success_log_divider = 0;

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

static esp_err_t dht11_read_payload(uint8_t payload[5], const char **phase_out, int *level_out)
{
    esp_err_t ret = ESP_OK;
    const char *phase = "response-low";

    portENTER_CRITICAL(&s_io_lock);

    ret = dht11_wait_for_level(0, DHT11_RESPONSE_TIMEOUT_US);
    if (ret != ESP_OK) {
        goto done;
    }

    phase = "response-high";
    ret = dht11_wait_for_level(1, DHT11_RESPONSE_TIMEOUT_US);
    if (ret != ESP_OK) {
        goto done;
    }

    phase = "frame-start";
    ret = dht11_wait_for_level(0, DHT11_RESPONSE_TIMEOUT_US);
    if (ret != ESP_OK) {
        goto done;
    }

    for (int bit = 0; bit < 40; bit++) {
        phase = "bit-start";
        ret = dht11_wait_for_level(1, DHT11_BIT_START_TIMEOUT_US);
        if (ret != ESP_OK) {
            goto done;
        }

        esp_rom_delay_us(DHT11_SAMPLE_OFFSET_US);

        payload[bit / 8] <<= 1;
        if (gpio_get_level(DHT11_DATA_PIN)) {
            payload[bit / 8] |= 1;
        }

        phase = "bit-end";
        ret = dht11_wait_for_level(0, DHT11_BIT_END_TIMEOUT_US);
        if (ret != ESP_OK) {
            goto done;
        }
    }

done:
    if (level_out) {
        *level_out = gpio_get_level(DHT11_DATA_PIN);
    }
    if (phase_out) {
        *phase_out = phase;
    }

    portEXIT_CRITICAL(&s_io_lock);
    return ret;
}

static esp_err_t dht11_read_once(dht11_data_t *out)
{
    uint8_t payload[5] = {0};
    const char *phase = "idle";
    int bus_level = 1;
    esp_err_t ret = ESP_OK;

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
    esp_rom_delay_us(DHT11_RELEASE_TO_RESPONSE_US);
    gpio_set_direction(DHT11_DATA_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(DHT11_DATA_PIN, GPIO_PULLUP_ONLY);

    ret = dht11_read_payload(payload, &phase, &bus_level);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG_DHT11,
                     "DHT11 timeout at %s on GPIO%d, level=%d; check wiring, GND, and add a 4.7k-10k external pull-up to 3.3V",
                     phase, DHT11_DATA_PIN, bus_level);
        }
        return ret;
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
    ESP_LOGW(TAG_DHT11,
             "DHT11 bus expects an external 4.7k-10k pull-up on GPIO%d; the internal pull-up is only a weak fallback",
             DHT11_DATA_PIN);
    return ESP_OK;
}

void dht11_sensor_get_data(dht11_data_t *data)
{
    if (!data) {
        return;
    }

    portENTER_CRITICAL(&s_data_lock);
    *data = s_data;
    portEXIT_CRITICAL(&s_data_lock);
}

static void dht11_store_sample_result(esp_err_t ret, const dht11_data_t *sample)
{
    dht11_data_t snapshot = {0};

    portENTER_CRITICAL(&s_data_lock);
    if (ret == ESP_OK && sample) {
        dht11_data_t next = *sample;
        next.sample_count = s_data.sample_count + 1;
        next.error_count = s_data.error_count;
        next.update_time_us = esp_timer_get_time();
        s_data = next;
    } else {
        s_data.error_count++;
        s_data.valid = false;
    }
    snapshot = s_data;
    portEXIT_CRITICAL(&s_data_lock);

    if (ret == ESP_OK) {
        s_success_log_divider++;
        ESP_LOGI(TAG_DHT11, "DHT11 sample ok: %dC %u%%RH samples=%lu errors=%lu",
                 snapshot.temperature_c, snapshot.humidity,
                 (unsigned long)snapshot.sample_count,
                 (unsigned long)snapshot.error_count);
    } else if ((snapshot.error_count % 10U) == 1U) {
        ESP_LOGW(TAG_DHT11, "DHT11 read failed: %s errors=%lu",
                 esp_err_to_name(ret), (unsigned long)snapshot.error_count);
    }
}

static TickType_t dht11_delay_until_next_sample(void)
{
    time_t now = ntp_client_get_time();
    const uint32_t interval_s = (uint32_t)(DHT11_SAMPLE_INTERVAL_MS / 1000U);

    if (ntp_client_is_synced() && now > 0 && interval_s > 0) {
        time_t next = ((now / (time_t)interval_s) + 1) * (time_t)interval_s;
        time_t delay_s = next - now;
        if (delay_s <= 0) {
            delay_s = interval_s;
        }
        return pdMS_TO_TICKS((uint32_t)delay_s * 1000U);
    }

    return pdMS_TO_TICKS(DHT11_SAMPLE_INTERVAL_MS);
}

void dht11_sensor_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG_DHT11, "DHT11 task started, startup delay=%dms interval=%dms",
             DHT11_STARTUP_DELAY_MS, DHT11_SAMPLE_INTERVAL_MS);
    vTaskDelay(pdMS_TO_TICKS(DHT11_STARTUP_DELAY_MS));

    dht11_data_t sample = {0};
    esp_err_t ret = dht11_read_once(&sample);
    dht11_store_sample_result(ret, (ret == ESP_OK) ? &sample : NULL);

    while (1) {
        vTaskDelay(dht11_delay_until_next_sample());

        dht11_data_t next_sample = {0};
        ret = dht11_read_once(&next_sample);
        dht11_store_sample_result(ret, (ret == ESP_OK) ? &next_sample : NULL);
    }
}
