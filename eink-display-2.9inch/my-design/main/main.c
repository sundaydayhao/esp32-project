#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "miniz.h"
#include "nvs_flash.h"

#define EPD_HOST SPI2_HOST

#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK 18
#define PIN_NUM_CS 5
#define PIN_NUM_DC 17
#define PIN_NUM_RST 16
#define PIN_NUM_BUSY 4

#define EPD_NATIVE_WIDTH 128
#define EPD_NATIVE_HEIGHT 296
#define EPD_LOGICAL_WIDTH 296
#define EPD_LOGICAL_HEIGHT 128
#define EPD_BUFFER_SIZE (EPD_NATIVE_WIDTH * EPD_NATIVE_HEIGHT / 8)

#define EPD_DEFAULT_BUSY_ACTIVE_LEVEL 1
#define EPD_DEFAULT_UPDATE_CONTROL 0xF7
#define EPD_REFRESH_TIMEOUT_MS 15000

#define WIFI_SSID "haoge"
#define WIFI_PASSWORD "19901118"
#define WIFI_MAX_RETRY 10

#define WEATHER_API_HOST "nx5khwapkg.re.qweatherapi.com"
#define WEATHER_LOCATION_ID "101280601"
#define WEATHER_API_KEY "04c81d5d4f3346a48d69b886c6c98cb0"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define HTTP_RESPONSE_BUFFER_SIZE 4096
#define APP_TASK_STACK_SIZE 16384
#define DISPLAY_REFRESH_INTERVAL_MS 1000
#define WEATHER_REFRESH_INTERVAL_MS (60 * 60 * 1000)
#define NTP_REFRESH_INTERVAL_MS (60 * 60 * 1000)

static const char *TAG = "eink_clock";
static spi_device_handle_t s_epd_spi;
static uint8_t s_framebuffer[EPD_BUFFER_SIZE];
static char s_http_response_buffer[HTTP_RESPONSE_BUFFER_SIZE];
static int s_busy_active_level = EPD_DEFAULT_BUSY_ACTIVE_LEVEL;
static uint8_t s_update_control = EPD_DEFAULT_UPDATE_CONTROL;
static EventGroupHandle_t s_wifi_event_group;
static int s_wifi_retry_count;

// This IL3820 path still uses full-screen refresh, so the dashboard intentionally
// omits seconds to avoid flashing the whole panel every second.
static const char *TIME_DISPLAY_FORMAT = "%H:%M";
static const char *TIME_PLACEHOLDER = "--:--";

typedef enum {
    EPD_CTRL_IL3820,
    EPD_CTRL_SSD1680,
} epd_controller_t;

typedef struct {
    epd_controller_t controller;
    int busy_active_level;
    uint8_t update_control;
    const char *label;
} epd_panel_profile_t;

typedef struct {
    char ch;
    uint8_t columns[5];
} glyph5x7_t;

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
} http_response_t;

typedef struct {
    bool valid;
    char text[24];
    int temp_c;
    int humidity;
} weather_info_t;

typedef struct {
    bool time_valid;
    bool weather_valid;
    char date_text[16];
    char time_text[9];
    weather_info_t weather;
    char status_text[40];
} dashboard_info_t;

static const epd_panel_profile_t s_panel_profiles[] = {
    {.controller = EPD_CTRL_IL3820, .busy_active_level = 0, .update_control = 0xC4, .label = "GDEM029A01/IL3820 busy=0 update=0xC4"},
    {.controller = EPD_CTRL_IL3820, .busy_active_level = 1, .update_control = 0xC4, .label = "GDEM029A01/IL3820 busy=1 update=0xC4"},
    {.controller = EPD_CTRL_SSD1680, .busy_active_level = 1, .update_control = 0xF7, .label = "SSD1680 busy=1 update=0xF7"},
    {.controller = EPD_CTRL_SSD1680, .busy_active_level = 0, .update_control = 0xF7, .label = "SSD1680 busy=0 update=0xF7"},
    {.controller = EPD_CTRL_SSD1680, .busy_active_level = 1, .update_control = 0xC7, .label = "SSD1680 busy=1 update=0xC7"},
    {.controller = EPD_CTRL_SSD1680, .busy_active_level = 0, .update_control = 0xC7, .label = "SSD1680 busy=0 update=0xC7"},
};

static epd_controller_t s_controller = EPD_CTRL_IL3820;

static const uint8_t s_il3820_lut_full_update[] = {
    0x02, 0x02, 0x01, 0x11, 0x12, 0x12, 0x22, 0x22,
    0x66, 0x69, 0x69, 0x59, 0x58, 0x99, 0x99, 0x88,
    0x00, 0x00, 0x00, 0x00, 0xF8, 0xB4, 0x13, 0x51,
    0x35, 0x51, 0x51, 0x19, 0x01, 0x00,
};

static const glyph5x7_t s_font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'%', {0x62, 0x64, 0x08, 0x13, 0x23}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x03, 0x04, 0x78, 0x04, 0x03}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};

static const uint8_t *find_glyph(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }

    for (size_t i = 0; i < sizeof(s_font) / sizeof(s_font[0]); ++i) {
        if (s_font[i].ch == ch) {
            return s_font[i].columns;
        }
    }

    return s_font[0].columns;
}

static void epd_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static esp_err_t epd_spi_write(const uint8_t *data, size_t length, int dc_level)
{
    gpio_set_level(PIN_NUM_DC, dc_level);

    spi_transaction_t transaction = {
        .length = length * 8,
        .tx_buffer = data,
    };

    return spi_device_transmit(s_epd_spi, &transaction);
}

static esp_err_t epd_send_command(uint8_t command)
{
    return epd_spi_write(&command, 1, 0);
}

static esp_err_t epd_send_data(const uint8_t *data, size_t length)
{
    if (length == 0) {
        return ESP_OK;
    }

    return epd_spi_write(data, length, 1);
}

static esp_err_t epd_send_byte(uint8_t value)
{
    return epd_send_data(&value, 1);
}

static esp_err_t epd_wait_busy(const char *stage, uint32_t timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (gpio_get_level(PIN_NUM_BUSY) == s_busy_active_level) {
        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            ESP_LOGE(TAG,
                     "BUSY timeout at %s after %" PRIu32 " ms (pin=%d active=%d update=0x%02X)",
                     stage,
                     timeout_ms,
                     gpio_get_level(PIN_NUM_BUSY),
                     s_busy_active_level,
                     s_update_control);
            return ESP_ERR_TIMEOUT;
        }
        epd_delay_ms(10);
    }

    return ESP_OK;
}

static void epd_reset(void)
{
    gpio_set_level(PIN_NUM_RST, 1);
    epd_delay_ms(10);
    gpio_set_level(PIN_NUM_RST, 0);
    epd_delay_ms(10);
    gpio_set_level(PIN_NUM_RST, 1);
    epd_delay_ms(10);
}

static esp_err_t epd_set_ram_area(uint16_t x_start, uint16_t x_end, uint16_t y_start, uint16_t y_end)
{
    ESP_RETURN_ON_ERROR(epd_send_command(0x44), TAG, "set x area command failed");
    uint8_t x_area[] = {
        (uint8_t)(x_start / 8),
        (uint8_t)(x_end / 8),
    };
    ESP_RETURN_ON_ERROR(epd_send_data(x_area, sizeof(x_area)), TAG, "set x area data failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x45), TAG, "set y area command failed");
    uint8_t y_area[] = {
        (uint8_t)(y_start & 0xFF),
        (uint8_t)(y_start >> 8),
        (uint8_t)(y_end & 0xFF),
        (uint8_t)(y_end >> 8),
    };
    return epd_send_data(y_area, sizeof(y_area));
}

static esp_err_t epd_set_ram_pointer(uint16_t x, uint16_t y)
{
    ESP_RETURN_ON_ERROR(epd_send_command(0x4E), TAG, "set x pointer command failed");
    ESP_RETURN_ON_ERROR(epd_send_byte((uint8_t)(x / 8)), TAG, "set x pointer failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x4F), TAG, "set y pointer command failed");
    uint8_t y_ptr[] = {
        (uint8_t)(y & 0xFF),
        (uint8_t)(y >> 8),
    };
    return epd_send_data(y_ptr, sizeof(y_ptr));
}

static esp_err_t epd_refresh(void)
{
    ESP_RETURN_ON_ERROR(epd_send_command(0x22), TAG, "update control command failed");
    ESP_RETURN_ON_ERROR(epd_send_byte(s_update_control), TAG, "update control data failed");
    ESP_RETURN_ON_ERROR(epd_send_command(0x20), TAG, "master activate failed");
    return epd_wait_busy("refresh", EPD_REFRESH_TIMEOUT_MS);
}

static esp_err_t epd_write_framebuffer(bool write_old_ram)
{
    ESP_RETURN_ON_ERROR(epd_set_ram_area(0, EPD_NATIVE_WIDTH - 1, 0, EPD_NATIVE_HEIGHT - 1), TAG, "set area failed");
    ESP_RETURN_ON_ERROR(epd_set_ram_pointer(0, 0), TAG, "set pointer failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x24), TAG, "write bw ram command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(s_framebuffer, sizeof(s_framebuffer)), TAG, "write bw ram failed");

    if (write_old_ram) {
        ESP_RETURN_ON_ERROR(epd_send_command(0x26), TAG, "write old ram command failed");
        ESP_RETURN_ON_ERROR(epd_send_data(s_framebuffer, sizeof(s_framebuffer)), TAG, "write old ram failed");
    }

    return epd_refresh();
}

static esp_err_t epd_clear_white(void)
{
    memset(s_framebuffer, 0xFF, sizeof(s_framebuffer));
    return epd_write_framebuffer(s_controller == EPD_CTRL_SSD1680);
}

static esp_err_t epd_draw_framebuffer(void)
{
    return epd_write_framebuffer(s_controller == EPD_CTRL_SSD1680);
}

static esp_err_t epd_init_panel_il3820(void)
{
    const uint8_t booster_soft_start[] = {0xD7, 0xD6, 0x9D};

    epd_reset();
    epd_delay_ms(10);
    if (gpio_get_level(PIN_NUM_BUSY) == s_busy_active_level) {
        ESP_RETURN_ON_ERROR(epd_wait_busy("reset", 2000), TAG, "busy wait failed");
    }

    ESP_RETURN_ON_ERROR(epd_send_command(0x01), TAG, "driver output control command failed");
    uint8_t driver_output_control[] = {
        (uint8_t)((EPD_NATIVE_HEIGHT - 1) & 0xFF),
        (uint8_t)((EPD_NATIVE_HEIGHT - 1) >> 8),
        0x00,
    };
    ESP_RETURN_ON_ERROR(epd_send_data(driver_output_control, sizeof(driver_output_control)), TAG,
                        "driver output control data failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x0C), TAG, "booster soft start command failed");
    ESP_RETURN_ON_ERROR(epd_send_data(booster_soft_start, sizeof(booster_soft_start)), TAG,
                        "booster soft start data failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x2C), TAG, "write vcom register command failed");
    ESP_RETURN_ON_ERROR(epd_send_byte(0xA8), TAG, "write vcom register data failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x3A), TAG, "set dummy line period command failed");
    ESP_RETURN_ON_ERROR(epd_send_byte(0x1A), TAG, "set dummy line period data failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x3B), TAG, "set gate time command failed");
    ESP_RETURN_ON_ERROR(epd_send_byte(0x08), TAG, "set gate time data failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x11), TAG, "data entry mode command failed");
    ESP_RETURN_ON_ERROR(epd_send_byte(0x03), TAG, "data entry mode data failed");

    ESP_RETURN_ON_ERROR(epd_set_ram_area(0, EPD_NATIVE_WIDTH - 1, 0, EPD_NATIVE_HEIGHT - 1), TAG, "set area failed");
    ESP_RETURN_ON_ERROR(epd_set_ram_pointer(0, 0), TAG, "set pointer failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x32), TAG, "write LUT command failed");
    return epd_send_data(s_il3820_lut_full_update, sizeof(s_il3820_lut_full_update));
}

static esp_err_t epd_init_panel_ssd1680(void)
{
    epd_reset();
    ESP_RETURN_ON_ERROR(epd_wait_busy("power on", 2000), TAG, "busy wait failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x12), TAG, "soft reset failed");
    epd_delay_ms(10);
    ESP_RETURN_ON_ERROR(epd_wait_busy("soft reset", 5000), TAG, "busy wait failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x01), TAG, "driver output control command failed");
    uint8_t driver_output_control[] = {
        (uint8_t)((EPD_NATIVE_HEIGHT - 1) & 0xFF),
        (uint8_t)((EPD_NATIVE_HEIGHT - 1) >> 8),
        0x00,
    };
    ESP_RETURN_ON_ERROR(epd_send_data(driver_output_control, sizeof(driver_output_control)), TAG,
                        "driver output control data failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x11), TAG, "data entry mode command failed");
    ESP_RETURN_ON_ERROR(epd_send_byte(0x03), TAG, "data entry mode data failed");

    ESP_RETURN_ON_ERROR(epd_set_ram_area(0, EPD_NATIVE_WIDTH - 1, 0, EPD_NATIVE_HEIGHT - 1), TAG, "set area failed");
    ESP_RETURN_ON_ERROR(epd_set_ram_pointer(0, 0), TAG, "set pointer failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x3C), TAG, "border waveform command failed");
    ESP_RETURN_ON_ERROR(epd_send_byte(0x05), TAG, "border waveform data failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x18), TAG, "temperature sensor command failed");
    ESP_RETURN_ON_ERROR(epd_send_byte(0x80), TAG, "temperature sensor data failed");

    ESP_RETURN_ON_ERROR(epd_send_command(0x21), TAG, "display update control command failed");
    uint8_t update_control[] = {0x00, 0x80};
    return epd_send_data(update_control, sizeof(update_control));
}

static esp_err_t epd_init_panel(void)
{
    if (s_controller == EPD_CTRL_IL3820) {
        return epd_init_panel_il3820();
    }

    return epd_init_panel_ssd1680();
}

static esp_err_t epd_sleep(void)
{
    ESP_RETURN_ON_ERROR(epd_send_command(0x10), TAG, "deep sleep command failed");
    return epd_send_byte(0x01);
}

static void epd_apply_profile(const epd_panel_profile_t *profile)
{
    s_controller = profile->controller;
    s_busy_active_level = profile->busy_active_level;
    s_update_control = profile->update_control;
}

static void framebuffer_fill_white(void)
{
    memset(s_framebuffer, 0xFF, sizeof(s_framebuffer));
}

static void framebuffer_set_pixel(int x, int y, bool black)
{
    if (x < 0 || x >= EPD_LOGICAL_WIDTH || y < 0 || y >= EPD_LOGICAL_HEIGHT) {
        return;
    }

    const int native_x = y;
    const int native_y = EPD_NATIVE_HEIGHT - 1 - x;
    const int index = native_x + native_y * EPD_NATIVE_WIDTH;
    const int byte_index = index / 8;
    const uint8_t bit_mask = (uint8_t)(0x80 >> (index % 8));

    if (black) {
        s_framebuffer[byte_index] &= (uint8_t)~bit_mask;
    } else {
        s_framebuffer[byte_index] |= bit_mask;
    }
}

static void framebuffer_draw_hline(int x, int y, int width)
{
    for (int i = 0; i < width; ++i) {
        framebuffer_set_pixel(x + i, y, true);
    }
}

static void framebuffer_draw_char(int x, int y, char ch, int scale)
{
    const uint8_t *glyph = find_glyph(ch);

    for (int col = 0; col < 5; ++col) {
        for (int row = 0; row < 7; ++row) {
            const bool pixel_on = (glyph[col] >> row) & 0x01;
            if (!pixel_on) {
                continue;
            }

            for (int dx = 0; dx < scale; ++dx) {
                for (int dy = 0; dy < scale; ++dy) {
                    framebuffer_set_pixel(x + col * scale + dx, y + row * scale + dy, true);
                }
            }
        }
    }
}

static int framebuffer_measure_text(const char *text, int scale)
{
    return (int)strlen(text) * 6 * scale;
}

static void framebuffer_draw_text(int x, int y, const char *text, int scale)
{
    const int char_advance = 6 * scale;
    while (*text != '\0') {
        framebuffer_draw_char(x, y, *text, scale);
        x += char_advance;
        ++text;
    }
}

static void framebuffer_draw_centered_text(int center_x, int y, const char *text, int scale)
{
    const int width = framebuffer_measure_text(text, scale);
    framebuffer_draw_text(center_x - width / 2, y, text, scale);
}

static esp_err_t epd_bus_init(void)
{
    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EPD_BUFFER_SIZE + 16,
    };

    const spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 4 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 1,
    };

    gpio_config_t output_cfg = {
        .pin_bit_mask = (1ULL << PIN_NUM_DC) | (1ULL << PIN_NUM_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output_cfg), TAG, "gpio output config failed");

    gpio_config_t busy_cfg = {
        .pin_bit_mask = (1ULL << PIN_NUM_BUSY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&busy_cfg), TAG, "gpio input config failed");

    gpio_set_level(PIN_NUM_DC, 1);
    gpio_set_level(PIN_NUM_RST, 1);

    ESP_RETURN_ON_ERROR(spi_bus_initialize(EPD_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi bus init failed");
    return spi_bus_add_device(EPD_HOST, &dev_cfg, &s_epd_spi);
}

static esp_err_t panel_init_and_probe(void)
{
    ESP_RETURN_ON_ERROR(epd_bus_init(), TAG, "epd bus init failed");

    for (size_t i = 0; i < sizeof(s_panel_profiles) / sizeof(s_panel_profiles[0]); ++i) {
        const epd_panel_profile_t *profile = &s_panel_profiles[i];
        epd_apply_profile(profile);
        ESP_LOGI(TAG, "Trying panel profile: %s", profile->label);

        esp_err_t err = epd_init_panel();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Panel init failed with %s: %s", profile->label, esp_err_to_name(err));
            epd_delay_ms(100);
            continue;
        }

        err = epd_clear_white();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Panel ready with %s", profile->label);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Initial clear failed with %s: %s", profile->label, esp_err_to_name(err));
        epd_delay_ms(100);
    }

    return ESP_FAIL;
}

static void render_status_screen(const char *line1, const char *line2)
{
    framebuffer_fill_white();
    framebuffer_draw_centered_text(EPD_LOGICAL_WIDTH / 2, 20, "EINK DASHBOARD", 2);
    framebuffer_draw_hline(12, 44, EPD_LOGICAL_WIDTH - 24);
    framebuffer_draw_centered_text(EPD_LOGICAL_WIDTH / 2, 64, line1, 2);
    framebuffer_draw_centered_text(EPD_LOGICAL_WIDTH / 2, 90, line2, 2);
    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_draw_framebuffer());
}

static void render_dashboard(const dashboard_info_t *info)
{
    char temp_line[24] = "--C";
    char humidity_line[24] = "--%";
    char weather_text[24] = "NO WEATHER";

    if (info->weather_valid) {
        snprintf(temp_line, sizeof(temp_line), "%dC", info->weather.temp_c);
        snprintf(humidity_line, sizeof(humidity_line), "%d%%", info->weather.humidity);
        snprintf(weather_text, sizeof(weather_text), "%s", info->weather.text);
    }

    framebuffer_fill_white();
    framebuffer_draw_text(8, 8, "DATE", 2);
    framebuffer_draw_text(68, 8, info->time_valid ? info->date_text : "---- -- --", 2);
    framebuffer_draw_hline(8, 28, EPD_LOGICAL_WIDTH - 16);

    framebuffer_draw_text(8, 38, "TIME", 2);
    framebuffer_draw_text(78, 38, info->time_valid ? info->time_text : TIME_PLACEHOLDER, 3);
    framebuffer_draw_hline(8, 68, EPD_LOGICAL_WIDTH - 16);

    framebuffer_draw_text(8, 78, "WEATHER", 2);
    framebuffer_draw_text(8, 100, weather_text, 2);

    framebuffer_draw_text(168, 78, "TEMP", 2);
    framebuffer_draw_text(228, 78, temp_line, 2);
    framebuffer_draw_text(168, 100, "HUM", 2);
    framebuffer_draw_text(222, 100, humidity_line, 2);

    if (info->status_text[0] != '\0') {
        framebuffer_draw_hline(8, 120, EPD_LOGICAL_WIDTH - 16);
        framebuffer_draw_text(10, 121, info->status_text, 1);
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(epd_draw_framebuffer());
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_count < WIFI_MAX_RETRY) {
            ++s_wifi_retry_count;
            ESP_LOGW(TAG, "Wi-Fi disconnected, retry %d/%d", s_wifi_retry_count, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_connect_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop init failed");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "esp_wifi_init failed");

    esp_event_handler_instance_t wifi_handler_instance;
    esp_event_handler_instance_t ip_handler_instance;
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT,
                                            ESP_EVENT_ANY_ID,
                                            &wifi_event_handler,
                                            NULL,
                                            &wifi_handler_instance),
        TAG,
        "wifi event register failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT,
                                            IP_EVENT_STA_GOT_IP,
                                            &wifi_event_handler,
                                            NULL,
                                            &ip_handler_instance),
        TAG,
        "ip event register failed");

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(30000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to Wi-Fi SSID %s", WIFI_SSID);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to connect to Wi-Fi SSID %s", WIFI_SSID);
    return ESP_FAIL;
}

static esp_err_t sync_time_with_ntp(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();

    esp_sntp_stop();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = {0};
    for (int retry = 0; retry < 15; ++retry) {
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year >= (2024 - 1900)) {
            ESP_LOGI(TAG, "NTP sync ok: %04d-%02d-%02d %02d:%02d:%02d",
                     timeinfo.tm_year + 1900,
                     timeinfo.tm_mon + 1,
                     timeinfo.tm_mday,
                     timeinfo.tm_hour,
                     timeinfo.tm_min,
                     timeinfo.tm_sec);
            return ESP_OK;
        }

        ESP_LOGI(TAG, "Waiting for NTP sync... %d/15", retry + 1);
        epd_delay_ms(1000);
    }

    return ESP_ERR_TIMEOUT;
}

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    http_response_t *response = (http_response_t *)event->user_data;

    if (event->event_id == HTTP_EVENT_ON_DATA && response != NULL && event->data_len > 0) {
        size_t copy_len = (size_t)event->data_len;
        if (response->length + copy_len >= response->capacity) {
            copy_len = response->capacity - response->length - 1;
        }

        if (copy_len > 0) {
            memcpy(response->buffer + response->length, event->data, copy_len);
            response->length += copy_len;
            response->buffer[response->length] = '\0';
        }
    }

    return ESP_OK;
}

static bool parse_gzip_header(const uint8_t *data, size_t len, size_t *payload_offset)
{
    const uint8_t gzip_id1 = 0x1F;
    const uint8_t gzip_id2 = 0x8B;
    const uint8_t gzip_cm_deflate = 0x08;
    const uint8_t flag_fextra = 0x04;
    const uint8_t flag_fname = 0x08;
    const uint8_t flag_fcomment = 0x10;
    const uint8_t flag_fhcrc = 0x02;

    if (data == NULL || len < 10 || payload_offset == NULL) {
        return false;
    }
    if (data[0] != gzip_id1 || data[1] != gzip_id2 || data[2] != gzip_cm_deflate) {
        return false;
    }

    size_t offset = 10;
    const uint8_t flags = data[3];

    if (flags & flag_fextra) {
        if (offset + 2 > len) {
            return false;
        }
        size_t extra_len = (size_t)data[offset] | ((size_t)data[offset + 1] << 8);
        offset += 2 + extra_len;
    }
    if (flags & flag_fname) {
        while (offset < len && data[offset] != '\0') {
            ++offset;
        }
        ++offset;
    }
    if (flags & flag_fcomment) {
        while (offset < len && data[offset] != '\0') {
            ++offset;
        }
        ++offset;
    }
    if (flags & flag_fhcrc) {
        offset += 2;
    }
    if (offset >= len) {
        return false;
    }

    *payload_offset = offset;
    return true;
}

static void maybe_decompress_http_body(http_response_t *response)
{
    if (response == NULL || response->length < 2) {
        return;
    }

    const uint8_t *src = (const uint8_t *)response->buffer;
    if (src[0] != 0x1F || src[1] != 0x8B) {
        return;
    }

    size_t payload_offset = 0;
    if (!parse_gzip_header(src, response->length, &payload_offset) || payload_offset + 8 > response->length) {
        ESP_LOGE(TAG, "Invalid gzip weather payload");
        return;
    }

    size_t compressed_len = response->length - payload_offset - 8;
    size_t decoded_len = (size_t)src[response->length - 4] |
                         ((size_t)src[response->length - 3] << 8) |
                         ((size_t)src[response->length - 2] << 16) |
                         ((size_t)src[response->length - 1] << 24);
    if (decoded_len == 0) {
        ESP_LOGE(TAG, "Gzip payload reported zero decoded length");
        return;
    }

    uint8_t *decoded = malloc(decoded_len + 1);
    if (decoded == NULL) {
        ESP_LOGE(TAG, "No memory for gzip payload: %u bytes", (unsigned)decoded_len);
        return;
    }

    size_t out_len = tinfl_decompress_mem_to_mem(decoded, decoded_len, src + payload_offset, compressed_len, 0);
    if (out_len == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        ESP_LOGE(TAG, "Gzip decompress failed");
        free(decoded);
        return;
    }

    size_t copy_len = out_len;
    if (copy_len >= response->capacity) {
        copy_len = response->capacity - 1;
        ESP_LOGW(TAG, "Decoded weather payload truncated: %u bytes", (unsigned)out_len);
    }

    memcpy(response->buffer, decoded, copy_len);
    response->buffer[copy_len] = '\0';
    response->length = copy_len;
    free(decoded);
}

static esp_err_t parse_weather_response(const char *json, weather_info_t *weather)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "Weather JSON parse failed");
        return ESP_FAIL;
    }

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!cJSON_IsString(code) || strcmp(code->valuestring, "200") != 0) {
        ESP_LOGE(TAG, "Weather API returned unexpected code: %s",
                 cJSON_IsString(code) ? code->valuestring : "unknown");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *now = cJSON_GetObjectItem(root, "now");
    if (!cJSON_IsObject(now)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *text = cJSON_GetObjectItem(now, "text");
    cJSON *temp = cJSON_GetObjectItem(now, "temp");
    cJSON *humidity = cJSON_GetObjectItem(now, "humidity");
    if (!cJSON_IsString(text) || !cJSON_IsString(temp) || !cJSON_IsString(humidity)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    memset(weather, 0, sizeof(*weather));
    snprintf(weather->text, sizeof(weather->text), "%s", text->valuestring);
    weather->temp_c = atoi(temp->valuestring);
    weather->humidity = atoi(humidity->valuestring);
    weather->valid = true;
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t fetch_weather(weather_info_t *weather)
{
    char url[192];
    snprintf(url, sizeof(url), "https://" WEATHER_API_HOST "/v7/weather/now?location=%s&lang=zh", WEATHER_LOCATION_ID);

    http_response_t response = {
        .buffer = s_http_response_buffer,
        .capacity = sizeof(s_http_response_buffer),
        .length = 0,
    };
    memset(s_http_response_buffer, 0, sizeof(s_http_response_buffer));

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = &response,
        .timeout_ms = 10000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "ESP32-EINK/1.0",
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_http_client_set_header(client, "X-QW-Api-Key", WEATHER_API_KEY), TAG, "set api key header failed");
    ESP_RETURN_ON_ERROR(esp_http_client_set_header(client, "Accept", "application/json"), TAG, "set accept header failed");
    ESP_RETURN_ON_ERROR(esp_http_client_set_header(client, "Accept-Encoding", "identity"), TAG, "set encoding header failed");

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    const int status_code = esp_http_client_get_status_code(client);
    if (response.length > 0) {
        maybe_decompress_http_body(&response);
    }
    esp_http_client_cleanup(client);

    if (status_code != 200) {
        ESP_LOGE(TAG, "Weather API status code: %d body=%s", status_code, s_http_response_buffer);
        return ESP_FAIL;
    }

    if (parse_weather_response(s_http_response_buffer, weather) != ESP_OK) {
        ESP_LOGE(TAG, "Weather parse failed, body=%s", s_http_response_buffer);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Weather: %s, %dC, %d%%", weather->text, weather->temp_c, weather->humidity);
    return ESP_OK;
}

static void fill_time_strings(dashboard_info_t *info)
{
    time_t now = 0;
    struct tm timeinfo = {0};

    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < (2024 - 1900)) {
        info->time_valid = false;
        snprintf(info->date_text, sizeof(info->date_text), "---- -- --");
        snprintf(info->time_text, sizeof(info->time_text), "%s", TIME_PLACEHOLDER);
        return;
    }

    info->time_valid = true;
    strftime(info->date_text, sizeof(info->date_text), "%Y-%m-%d", &timeinfo);
    strftime(info->time_text, sizeof(info->time_text), TIME_DISPLAY_FORMAT, &timeinfo);
}

static void log_app_task_stack_watermark(const char *stage)
{
    UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "app_task stack watermark after %s: %u", stage, (unsigned)watermark);
}

static void app_task(void *arg)
{
    (void)arg;
    dashboard_info_t info = {0};
    char last_time_text[sizeof(info.time_text)] = "";

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    log_app_task_stack_watermark("nvs init");

    ESP_ERROR_CHECK(panel_init_and_probe());
    log_app_task_stack_watermark("panel init");

    render_status_screen("CONNECT WIFI", WIFI_SSID);
    err = wifi_connect_sta();
    log_app_task_stack_watermark("wifi connect");
    if (err != ESP_OK) {
        snprintf(info.status_text, sizeof(info.status_text), "WIFI FAILED");
        fill_time_strings(&info);
        render_dashboard(&info);
        ESP_ERROR_CHECK_WITHOUT_ABORT(epd_sleep());
        return;
    }

    render_status_screen("SYNC TIME", "NTP ALIYUN");
    err = sync_time_with_ntp();
    log_app_task_stack_watermark("ntp sync");
    if (err != ESP_OK) {
        snprintf(info.status_text, sizeof(info.status_text), "NTP FAILED");
    }
    fill_time_strings(&info);

    render_status_screen("LOAD WEATHER", "QWEATHER API");
    err = fetch_weather(&info.weather);
    log_app_task_stack_watermark("weather fetch");
    if (err == ESP_OK) {
        info.weather_valid = true;
        if (info.status_text[0] == '\0') {
            snprintf(info.status_text, sizeof(info.status_text), "WIFI OK  NTP OK");
        }
    } else if (info.status_text[0] == '\0') {
        snprintf(info.status_text, sizeof(info.status_text), "WEATHER FAILED");
    }

    render_dashboard(&info);
    log_app_task_stack_watermark("initial render");
    snprintf(last_time_text, sizeof(last_time_text), "%s", info.time_text);

    TickType_t last_display_tick = xTaskGetTickCount();
    TickType_t last_weather_tick = last_display_tick;
    TickType_t last_ntp_tick = last_display_tick;

    while (1) {
        bool need_refresh = false;
        TickType_t now_tick = xTaskGetTickCount();

        if ((now_tick - last_ntp_tick) >= pdMS_TO_TICKS(NTP_REFRESH_INTERVAL_MS)) {
            err = sync_time_with_ntp();
            log_app_task_stack_watermark("periodic ntp sync");
            if (err == ESP_OK) {
                if (strncmp(info.status_text, "NTP FAILED", strlen("NTP FAILED")) == 0) {
                    snprintf(info.status_text, sizeof(info.status_text), "WIFI OK  NTP OK");
                }
            } else {
                snprintf(info.status_text, sizeof(info.status_text), "NTP FAILED");
            }
            fill_time_strings(&info);
            last_ntp_tick = now_tick;
            need_refresh = true;
        }

        if ((now_tick - last_weather_tick) >= pdMS_TO_TICKS(WEATHER_REFRESH_INTERVAL_MS)) {
            err = fetch_weather(&info.weather);
            log_app_task_stack_watermark("periodic weather fetch");
            if (err == ESP_OK) {
                info.weather_valid = true;
                if (strncmp(info.status_text, "WEATHER FAILED", strlen("WEATHER FAILED")) == 0 ||
                    info.status_text[0] == '\0') {
                    snprintf(info.status_text, sizeof(info.status_text), "WIFI OK  NTP OK");
                }
            } else {
                snprintf(info.status_text, sizeof(info.status_text), "WEATHER FAILED");
            }
            last_weather_tick = now_tick;
            need_refresh = true;
        }

        fill_time_strings(&info);
        if (strcmp(last_time_text, info.time_text) != 0) {
            snprintf(last_time_text, sizeof(last_time_text), "%s", info.time_text);
            need_refresh = true;
        }

        if (need_refresh) {
            render_dashboard(&info);
            ESP_LOGI(TAG, "Display refreshed: %s", info.time_text);
        }

        vTaskDelayUntil(&last_display_tick, pdMS_TO_TICKS(DISPLAY_REFRESH_INTERVAL_MS));
    }
}

void app_main(void)
{
    BaseType_t ok = xTaskCreate(app_task, "app_task", APP_TASK_STACK_SIZE, NULL, 5, NULL);
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_FAIL);
}
