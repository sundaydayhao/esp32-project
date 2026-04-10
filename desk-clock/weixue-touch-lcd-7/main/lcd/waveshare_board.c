#include "waveshare_board.h"

#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define WAVESHARE_I2C_PORT      I2C_NUM_0
#define WAVESHARE_I2C_SDA       GPIO_NUM_8
#define WAVESHARE_I2C_SCL       GPIO_NUM_9
#define WAVESHARE_I2C_FREQ_HZ   400000

#define CH422G_REG_MODE         0x24
#define CH422G_MODE_OUTPUT      0x01
#define CH422G_REG_OUT_LOWER    0x38
#define CH422G_REG_OUT_UPPER    0x23

#define WAVESHARE_PIN_TP_RST    1
#define WAVESHARE_PIN_LCD_BL    2
#define WAVESHARE_PIN_LCD_RST   3
#define WAVESHARE_PIN_SD_CS     4
#define WAVESHARE_PIN_USB_SEL   5

static const char *TAG = "BOARD";
static bool s_i2c_ready = false;
static uint16_t s_output_mask = 0;

static esp_err_t ch422g_write_reg(uint8_t reg, uint8_t value)
{
    return i2c_master_write_to_device(WAVESHARE_I2C_PORT, reg, &value, sizeof(value), pdMS_TO_TICKS(100));
}

static esp_err_t ch422g_flush_outputs(void)
{
    ESP_RETURN_ON_ERROR(ch422g_write_reg(CH422G_REG_OUT_LOWER, (uint8_t)(s_output_mask & 0xFF)), TAG, "write lower failed");
    ESP_RETURN_ON_ERROR(ch422g_write_reg(CH422G_REG_OUT_UPPER, (uint8_t)(s_output_mask >> 8)), TAG, "write upper failed");
    return ch422g_write_reg(CH422G_REG_MODE, CH422G_MODE_OUTPUT);
}

static esp_err_t set_pin(uint8_t pin, bool high)
{
    if (high) {
        s_output_mask |= (uint16_t)(1U << pin);
    } else {
        s_output_mask &= (uint16_t)~(1U << pin);
    }
    return ch422g_flush_outputs();
}

esp_err_t waveshare_board_init(void)
{
    if (s_i2c_ready) {
        return ESP_OK;
    }

    const i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = WAVESHARE_I2C_SDA,
        .scl_io_num = WAVESHARE_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = WAVESHARE_I2C_FREQ_HZ,
        .clk_flags = 0,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(WAVESHARE_I2C_PORT, &cfg), TAG, "i2c param failed");

    esp_err_t ret = i2c_driver_install(WAVESHARE_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_i2c_ready = true;
    ESP_LOGI(TAG, "CH422G control ready on I2C%d SDA=%d SCL=%d", WAVESHARE_I2C_PORT, WAVESHARE_I2C_SDA, WAVESHARE_I2C_SCL);
    return ch422g_flush_outputs();
}

esp_err_t waveshare_board_set_lcd_state(bool display_on)
{
    ESP_RETURN_ON_ERROR(waveshare_board_init(), TAG, "board init failed");
    ESP_RETURN_ON_ERROR(set_pin(WAVESHARE_PIN_TP_RST, true), TAG, "set TP_RST failed");
    ESP_RETURN_ON_ERROR(set_pin(WAVESHARE_PIN_LCD_RST, true), TAG, "set LCD_RST failed");
    ESP_RETURN_ON_ERROR(set_pin(WAVESHARE_PIN_USB_SEL, false), TAG, "set USB_SEL failed");
    ESP_RETURN_ON_ERROR(set_pin(WAVESHARE_PIN_SD_CS, true), TAG, "set SD_CS failed");
    return set_pin(WAVESHARE_PIN_LCD_BL, display_on);
}
