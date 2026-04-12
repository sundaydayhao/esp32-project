/**
 * @file touch_gt911.c
 * @brief Minimal GT911 touch driver for Waveshare ESP32-S3-Touch-LCD-7
 */
#include "touch_gt911.h"

#include <string.h>

#include "config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd/waveshare_board.h"

#define GT911_ADDR_ALT              0x14
#define GT911_REG_PRODUCT_ID        0x8140
#define GT911_REG_STATUS            0x814E
#define GT911_REG_POINT1            0x8150
#define GT911_POINT_INFO_LEN        8
#define GT911_STATUS_READY_MASK     0x80
#define GT911_STATUS_POINTS_MASK    0x0F

typedef struct {
    bool ready;
    uint8_t addr;
} touch_gt911_state_t;

static const char *TAG = "TOUCH";
static touch_gt911_state_t s_touch = {
    .ready = false,
    .addr = TOUCH_I2C_ADDR,
};

static esp_err_t gt911_read_reg(uint8_t addr, uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t reg_buf[2] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF),
    };

    return waveshare_board_i2c_write_read(addr, reg_buf, sizeof(reg_buf), data, len);
}

static esp_err_t gt911_write_u8(uint8_t addr, uint16_t reg, uint8_t value)
{
    uint8_t buf[3] = {
        (uint8_t)(reg >> 8),
        (uint8_t)(reg & 0xFF),
        value,
    };

    return waveshare_board_i2c_write(addr, buf, sizeof(buf));
}

static esp_err_t gt911_reset_sequence(void)
{
    gpio_config_t int_cfg = {
        .pin_bit_mask = 1ULL << TOUCH_PIN_INT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "configure touch INT output failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(TOUCH_PIN_INT, 0), TAG, "drive touch INT low failed");

    ESP_RETURN_ON_ERROR(waveshare_board_set_touch_reset(false), TAG, "hold touch reset low failed");
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_RETURN_ON_ERROR(waveshare_board_set_touch_reset(true), TAG, "release touch reset failed");
    vTaskDelay(pdMS_TO_TICKS(10));

    int_cfg.mode = GPIO_MODE_INPUT;
    int_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    int_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "restore touch INT input failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    return ESP_OK;
}

static esp_err_t gt911_probe_address(uint8_t addr, char *product_id, size_t product_id_size)
{
    uint8_t raw_id[4] = {0};
    esp_err_t ret = gt911_read_reg(addr, GT911_REG_PRODUCT_ID, raw_id, sizeof(raw_id));
    if (ret != ESP_OK) {
        return ret;
    }

    if (product_id && product_id_size > 0) {
        size_t copy_len = sizeof(raw_id);
        if (copy_len >= product_id_size) {
            copy_len = product_id_size - 1;
        }
        memcpy(product_id, raw_id, copy_len);
        product_id[copy_len] = '\0';
    }

    return ESP_OK;
}

static void touch_apply_orientation(uint16_t *x, uint16_t *y)
{
    uint16_t tx = *x;
    uint16_t ty = *y;

#if TOUCH_SWAP_XY
    uint16_t tmp = tx;
    tx = ty;
    ty = tmp;
#endif

#if TOUCH_MIRROR_X
    if (tx < LCD_H_RES) {
        tx = (uint16_t)(LCD_H_RES - 1 - tx);
    }
#endif

#if TOUCH_MIRROR_Y
    if (ty < LCD_V_RES) {
        ty = (uint16_t)(LCD_V_RES - 1 - ty);
    }
#endif

    if (tx >= LCD_H_RES) {
        tx = LCD_H_RES - 1;
    }
    if (ty >= LCD_V_RES) {
        ty = LCD_V_RES - 1;
    }

    *x = tx;
    *y = ty;
}

esp_err_t touch_gt911_init(void)
{
    esp_err_t ret;
    char product_id[5] = {0};

    if (s_touch.ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(waveshare_board_init(), TAG, "board init failed");
    ret = gt911_reset_sequence();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gt911_probe_address(TOUCH_I2C_ADDR, product_id, sizeof(product_id));
    if (ret == ESP_OK) {
        s_touch.addr = TOUCH_I2C_ADDR;
    } else {
        ret = gt911_probe_address(GT911_ADDR_ALT, product_id, sizeof(product_id));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "GT911 probe failed on 0x%02X and 0x%02X", TOUCH_I2C_ADDR, GT911_ADDR_ALT);
            return ret;
        }
        s_touch.addr = GT911_ADDR_ALT;
    }

    ESP_RETURN_ON_ERROR(gt911_write_u8(s_touch.addr, GT911_REG_STATUS, 0), TAG, "clear touch status failed");

    s_touch.ready = true;
    ESP_LOGI(TAG, "GT911 ready addr=0x%02X product_id=%s int=%d", s_touch.addr, product_id, TOUCH_PIN_INT);
    return ESP_OK;
}

bool touch_gt911_is_ready(void)
{
    return s_touch.ready;
}

esp_err_t touch_gt911_read(uint16_t *x, uint16_t *y, bool *pressed)
{
    uint8_t status = 0;
    uint8_t point_data[GT911_POINT_INFO_LEN] = {0};
    uint8_t touch_count = 0;
    esp_err_t ret;

    if (!x || !y || !pressed) {
        return ESP_ERR_INVALID_ARG;
    }

    *pressed = false;
    if (!s_touch.ready) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = gt911_read_reg(s_touch.addr, GT911_REG_STATUS, &status, sizeof(status));
    if (ret != ESP_OK) {
        return ret;
    }

    if ((status & GT911_STATUS_READY_MASK) == 0) {
        return ESP_OK;
    }

    touch_count = status & GT911_STATUS_POINTS_MASK;
    if (touch_count == 0 || touch_count > 5) {
        return gt911_write_u8(s_touch.addr, GT911_REG_STATUS, 0);
    }

    ret = gt911_read_reg(s_touch.addr, GT911_REG_POINT1, point_data, sizeof(point_data));
    if (ret == ESP_OK) {
        *x = (uint16_t)(point_data[1] | (point_data[2] << 8));
        *y = (uint16_t)(point_data[3] | (point_data[4] << 8));
        touch_apply_orientation(x, y);
        *pressed = true;
    }

    esp_err_t clear_ret = gt911_write_u8(s_touch.addr, GT911_REG_STATUS, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    return clear_ret;
}
