#include "dht11.h"

#include <string.h>
#include <sys/time.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dht11";
static portMUX_TYPE s_dht11_mux = portMUX_INITIALIZER_UNLOCKED;
static dht11_reading_t s_last_reading;
static uint64_t s_last_read_us;

static bool wait_for_level(int level, uint32_t timeout_us)
{
    while (timeout_us-- > 0) {
        if (gpio_get_level((gpio_num_t)CONFIG_DHT11_GPIO) == level) {
            return true;
        }
        esp_rom_delay_us(1);
    }

    return false;
}

esp_err_t dht11_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << CONFIG_DHT11_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "configure gpio failed");
    gpio_set_level((gpio_num_t)CONFIG_DHT11_GPIO, 1);
    memset(&s_last_reading, 0, sizeof(s_last_reading));
    s_last_read_us = 0;

    ESP_LOGI(TAG, "DHT11 ready on GPIO %d", CONFIG_DHT11_GPIO);
    return ESP_OK;
}

esp_err_t dht11_read(dht11_reading_t *out)
{
    uint8_t data[5] = {0};

    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "null output");

    gpio_set_direction((gpio_num_t)CONFIG_DHT11_GPIO, GPIO_MODE_OUTPUT_OD);
    gpio_set_level((gpio_num_t)CONFIG_DHT11_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)CONFIG_DHT11_GPIO, 1);
    esp_rom_delay_us(40);
    gpio_set_direction((gpio_num_t)CONFIG_DHT11_GPIO, GPIO_MODE_INPUT);

    portENTER_CRITICAL(&s_dht11_mux);

    if (!wait_for_level(0, 120) ||
        !wait_for_level(1, 120) ||
        !wait_for_level(0, 120)) {
        portEXIT_CRITICAL(&s_dht11_mux);
        return ESP_ERR_TIMEOUT;
    }

    for (int bit = 0; bit < 40; ++bit) {
        if (!wait_for_level(1, 80)) {
            portEXIT_CRITICAL(&s_dht11_mux);
            return ESP_ERR_TIMEOUT;
        }

        esp_rom_delay_us(40);
        data[bit / 8] <<= 1;
        if (gpio_get_level((gpio_num_t)CONFIG_DHT11_GPIO) != 0) {
            data[bit / 8] |= 1U;
        }

        if (!wait_for_level(0, 120)) {
            portEXIT_CRITICAL(&s_dht11_mux);
            return ESP_ERR_TIMEOUT;
        }
    }

    portEXIT_CRITICAL(&s_dht11_mux);

    if (((uint8_t)(data[0] + data[1] + data[2] + data[3])) != data[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    out->humidity_pct = data[0];
    out->temperature_c = (int8_t)data[2];
    out->valid = true;
    out->sample_age_sec = 0;

    s_last_reading = *out;
    s_last_read_us = (uint64_t)esp_timer_get_time();
    return ESP_OK;
}

void dht11_get_reading(dht11_reading_t *out)
{
    if (!out) {
        return;
    }

    *out = s_last_reading;
    if (s_last_reading.valid && s_last_read_us != 0) {
        uint64_t now_us = (uint64_t)esp_timer_get_time();
        out->sample_age_sec = (uint32_t)((now_us - s_last_read_us) / 1000000ULL);
    }
}

void dht11_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "DHT11 task started");

    while (1) {
        dht11_reading_t reading = {0};
        esp_err_t ret = dht11_read(&reading);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "DHT11 read failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Indoor: %dC %u%%", (int)reading.temperature_c, (unsigned)reading.humidity_pct);
        }

        vTaskDelay(pdMS_TO_TICKS((uint32_t)CONFIG_DHT11_UPDATE_INTERVAL_SEC * 1000U));
    }
}