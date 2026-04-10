#include "lcd_driver.h"

#include <string.h>

#include "config.h"
#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "waveshare_board.h"

static esp_lcd_panel_handle_t s_panel = NULL;
static uint16_t *s_fb[2] = {NULL, NULL};
static size_t s_fb_size_bytes = 0;
static lcd_driver_flush_done_cb_t s_flush_done_cb = NULL;
static void *s_flush_done_user_ctx = NULL;
static bool s_backlight_warned = false;

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static bool IRAM_ATTR notify_flush_done(void)
{
    if (s_flush_done_cb) {
        return s_flush_done_cb(s_flush_done_user_ctx);
    }
    return false;
}

static bool IRAM_ATTR on_rgb_color_trans_done(esp_lcd_panel_handle_t panel,
                                              const esp_lcd_rgb_panel_event_data_t *edata,
                                              void *ctx)
{
    (void)panel;
    (void)edata;
    (void)ctx;
    return notify_flush_done();
}

static void fill_test_pattern_lines(uint16_t *fb, uint32_t pattern_index, int y_start, int line_count)
{
    static const uint16_t bars[] = {
        0xF800, 0x07E0, 0x001F, 0xFFE0, 0x07FF, 0xF81F, 0xFFFF, 0x0000,
    };
    const int bar_count = sizeof(bars) / sizeof(bars[0]);
    const int cell = 40;
    const uint16_t color_a = rgb565(255, 255, 255);
    const uint16_t color_b = rgb565(0, 0, 0);
    const uint16_t color_c = rgb565(255, 0, 255);
    const uint16_t color_d = rgb565(0, 255, 255);
    const uint16_t bg = rgb565(0, 0, 96);
    const uint16_t border = rgb565(255, 255, 255);
    const uint16_t diag_a = rgb565(255, 0, 0);
    const uint16_t diag_b = rgb565(0, 255, 0);

    for (int y = 0; y < line_count; y++) {
        const int global_y = y_start + y;
        uint16_t *row = fb + (size_t)y * LCD_H_RES;

        for (int x = 0; x < LCD_H_RES; x++) {
            uint16_t color = 0;

            switch (pattern_index % 4) {
            case 0: {
                int bar = (x * bar_count) / LCD_H_RES;
                color = bars[bar];
                break;
            }
            case 1: {
                bool odd = ((x / cell) + (global_y / cell)) & 0x1;
                bool upper = global_y < (LCD_V_RES / 2);
                color = upper ? (odd ? color_a : color_b) : (odd ? color_c : color_d);
                break;
            }
            case 2:
                color = bg;
                if (global_y == 0 || global_y == LCD_V_RES - 1 || x == 0 || x == LCD_H_RES - 1) {
                    color = border;
                }
                if (global_y < LCD_H_RES && x == global_y) {
                    color = diag_a;
                }
                if (global_y < LCD_H_RES && x == (LCD_H_RES - 1 - global_y)) {
                    color = diag_b;
                }
                break;
            default:
                color = rgb565(0, 0, 255);
                break;
            }

            row[x] = color;
        }
    }
}

static esp_err_t init_waveshare_rgb_panel(void)
{
    ESP_RETURN_ON_ERROR(waveshare_board_init(), TAG_LCD, "board init failed");
    ESP_RETURN_ON_ERROR(waveshare_board_set_lcd_state(false), TAG_LCD, "board lcd-off failed");

    ESP_LOGI(TAG_LCD, "Initialize RGB LCD %dx%d", LCD_H_RES, LCD_V_RES);
    ESP_LOGI(TAG_LCD, "Using %s RGB pin map", DISPLAY_BOARD_NAME);
    ESP_LOGI(TAG_LCD, "Timing: pclk=%d hsync=%d/%d/%d vsync=%d/%d/%d",
             LCD_PCLK_HZ,
             LCD_HSYNC_BACK_PORCH, LCD_HSYNC_FRONT_PORCH, LCD_HSYNC_PULSE_WIDTH,
             LCD_VSYNC_BACK_PORCH, LCD_VSYNC_FRONT_PORCH, LCD_VSYNC_PULSE_WIDTH);

    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_back_porch = LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = LCD_HSYNC_FRONT_PORCH,
            .hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH,
            .vsync_back_porch = LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = LCD_VSYNC_FRONT_PORCH,
            .vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .num_fbs = LCD_NUM_FB,
        .bounce_buffer_size_px = LCD_H_RES * 10,
        .psram_trans_align = 64,
        .hsync_gpio_num = LCD_PIN_HSYNC,
        .vsync_gpio_num = LCD_PIN_VSYNC,
        .de_gpio_num = LCD_PIN_DE,
        .pclk_gpio_num = LCD_PIN_PCLK,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums = {
            LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
            LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7,
            LCD_PIN_D8, LCD_PIN_D9, LCD_PIN_D10, LCD_PIN_D11,
            LCD_PIN_D12, LCD_PIN_D13, LCD_PIN_D14, LCD_PIN_D15,
        },
        .flags.fb_in_psram = 1,
        .flags.double_fb = 1,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &s_panel), TAG_LCD, "create rgb panel failed");

    const esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_color_trans_done = on_rgb_color_trans_done,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, NULL));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    {
        esp_err_t disp_ret = esp_lcd_panel_disp_on_off(s_panel, true);
        if (disp_ret == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGI(TAG_LCD, "Panel DISP control is not wired to a GPIO; continuing");
        } else {
            ESP_ERROR_CHECK(disp_ret);
        }
    }

    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(
        s_panel, 2, (void **)&s_fb[0], (void **)&s_fb[1]));

    s_fb_size_bytes = (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
    memset(s_fb[0], 0, s_fb_size_bytes);
    memset(s_fb[1], 0, s_fb_size_bytes);

    lcd_driver_set_brightness(100);
    ESP_LOGI(TAG_LCD, "RGB LCD initialized FB0=%p FB1=%p", s_fb[0], s_fb[1]);
    return ESP_OK;
}

esp_err_t lcd_driver_init(void)
{
    return init_waveshare_rgb_panel();
}

esp_lcd_panel_handle_t lcd_driver_get_panel(void)
{
    return s_panel;
}

void lcd_driver_get_framebuf(uint16_t **fb0, uint16_t **fb1)
{
    if (fb0) {
        *fb0 = s_fb[0];
    }
    if (fb1) {
        *fb1 = s_fb[1];
    }
}

size_t lcd_driver_get_framebuf_size(void)
{
    return s_fb_size_bytes;
}

bool lcd_driver_uses_direct_mode(void)
{
    return true;
}

bool lcd_driver_needs_rgb565_byte_swap(void)
{
    return false;
}

esp_err_t lcd_driver_show_test_pattern(uint32_t pattern_index)
{
    uint32_t fb_index = 0;

    if (!s_panel || !s_fb[0]) {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 0; i < LCD_NUM_FB; i++) {
        fill_test_pattern_lines(s_fb[i], pattern_index, 0, LCD_V_RES);
    }

    fb_index = pattern_index % LCD_NUM_FB;
    ESP_LOGI(TAG_LCD, "Display diagnostic pattern #%lu using FB%lu=%p",
             (unsigned long)(pattern_index % 4),
             (unsigned long)fb_index,
             s_fb[fb_index]);
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, s_fb[fb_index]);
}

esp_err_t lcd_driver_register_flush_done_callback(lcd_driver_flush_done_cb_t cb, void *user_ctx)
{
    s_flush_done_cb = cb;
    s_flush_done_user_ctx = user_ctx;
    return ESP_OK;
}

void lcd_driver_set_brightness(uint8_t pct)
{
    const bool display_on = pct > 0;

    if (!s_backlight_warned) {
        ESP_LOGW(TAG_LCD, "Backlight control is on CH422G EXIO2 for this board");
        s_backlight_warned = true;
    }
    ESP_ERROR_CHECK(waveshare_board_set_lcd_state(display_on));
}
