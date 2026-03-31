/**
 * @file display.c
 * @brief 显示屏驱动实现 (ST7789 240x240 + LVGL)
 */

#include "display.h"
#include "config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"

/* LVGL */
#include "lvgl.h"
#include "esp_lvgl_port.h"

static const char *TAG = "display";

static lv_disp_t *s_lvgl_disp = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_panel_io_handle_t s_io_handle = NULL;

/* ============================================================
 * 背光控制
 * ============================================================ */
static void backlight_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_LCD_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PIN_NUM_LCD_BL, LCD_BK_LIGHT_ON_LEVEL);
}

/* ============================================================
 * LVGL 刷新回调
 * ============================================================ */
static bool lvgl_flush_ready_cb(esp_lcd_panel_io_handle_t panel_io,
                                  esp_lcd_panel_io_event_data_t *edata,
                                  void *user_ctx)
{
    lv_disp_t *disp = (lv_disp_t *)user_ctx;
    lvgl_port_flush_ready(disp);
    return false;
}

/* ============================================================
 * 公共接口实现
 * ============================================================ */

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "初始化显示屏 ST7789 %dx%d", LCD_H_RES, LCD_V_RES);

    /* 1. 初始化背光 */
    backlight_init();

    /* 2. 初始化SPI总线 */
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_NUM_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI总线初始化完成");

    /* 3. 初始化LCD IO */
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = lvgl_flush_ready_cb,
        .user_ctx = NULL,  // 稍后设置
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                              &io_config, &s_io_handle));

    /* 4. 初始化ST7789面板 */
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io_handle, &panel_config, 
                                              &s_panel_handle));

    /* 5. 复位并初始化面板 */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel_handle, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));
    ESP_LOGI(TAG, "ST7789面板初始化完成");

    /* 6. 初始化LVGL Port */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* 7. 添加显示设备到LVGL */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_io_handle,
        .panel_handle = s_panel_handle,
        .buffer_size = LCD_H_RES * 40,  // 40行缓冲
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
        }
    };
    s_lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    if (s_lvgl_disp == NULL) {
        ESP_LOGE(TAG, "LVGL显示设备添加失败");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL初始化完成，显示屏就绪");
    return ESP_OK;
}

void *display_get_lvgl_disp(void)
{
    return s_lvgl_disp;
}

bool display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void display_unlock(void)
{
    lvgl_port_unlock();
}

void display_set_brightness(uint8_t brightness)
{
    /* 简单实现：0=关，>0=开 */
    if (brightness == 0) {
        gpio_set_level(PIN_NUM_LCD_BL, !LCD_BK_LIGHT_ON_LEVEL);
    } else {
        gpio_set_level(PIN_NUM_LCD_BL, LCD_BK_LIGHT_ON_LEVEL);
    }
    /* TODO: 如果使用PWM背光，可以在这里实现亮度调节 */
}