#include "board/ch422g_ctrl.h"

#include "freertos/FreeRTOS.h"
#include "config.h"
#include "driver/i2c.h"

#if BOARD_HAS_CH422G_CTRL

static bool s_i2c_ready = false;
static uint16_t s_output_mask = 0;

#define CH422G_REG_MODE      0x24
#define CH422G_MODE_OUTPUT   0x01
#define CH422G_REG_OUT       0x38
#define CH422G_REG_OUT_UPPER 0x23

static esp_err_t ch422g_write_reg(uint8_t reg, uint8_t value)
{
    return i2c_master_write_to_device(BOARD_CH422G_I2C_PORT, reg, &value, sizeof(value),
                                      pdMS_TO_TICKS(100));
}

static esp_err_t ch422g_flush_outputs(void)
{
    esp_err_t ret = ch422g_write_reg(CH422G_REG_OUT, (uint8_t)(s_output_mask & 0xFF));
    if (ret != ESP_OK) {
        return ret;
    }

    ret = ch422g_write_reg(CH422G_REG_OUT_UPPER, (uint8_t)(s_output_mask >> 8));
    if (ret != ESP_OK) {
        return ret;
    }

    return ch422g_write_reg(CH422G_REG_MODE, CH422G_MODE_OUTPUT);
}

esp_err_t ch422g_ctrl_init(void)
{
    if (s_i2c_ready) {
        return ESP_OK;
    }

    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BOARD_CH422G_I2C_SDA,
        .scl_io_num = BOARD_CH422G_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_CH422G_I2C_FREQ,
        .clk_flags = 0,
    };

    esp_err_t ret = i2c_param_config(BOARD_CH422G_I2C_PORT, &i2c_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_driver_install(BOARD_CH422G_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    s_i2c_ready = true;
    return ch422g_flush_outputs();
}

esp_err_t ch422g_ctrl_set_pin(uint8_t pin, bool high)
{
    return ch422g_ctrl_set_pins((uint16_t)(1U << pin), high);
}

esp_err_t ch422g_ctrl_set_pins(uint16_t pin_mask, bool high)
{
    esp_err_t ret = ch422g_ctrl_init();
    if (ret != ESP_OK) {
        return ret;
    }

    if (high) {
        s_output_mask |= pin_mask;
    } else {
        s_output_mask &= (uint16_t)~pin_mask;
    }

    return ch422g_flush_outputs();
}

#else

esp_err_t ch422g_ctrl_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ch422g_ctrl_set_pin(uint8_t pin, bool high)
{
    (void)pin;
    (void)high;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ch422g_ctrl_set_pins(uint16_t pin_mask, bool high)
{
    (void)pin_mask;
    (void)high;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
