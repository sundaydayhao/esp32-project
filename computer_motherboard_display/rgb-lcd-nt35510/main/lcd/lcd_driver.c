#include "lcd_driver.h"

#include <string.h>

#include "config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_panel_io = NULL;
static esp_lcd_i80_bus_handle_t s_i80_bus = NULL;
static uint16_t *s_fb[2] = {NULL, NULL};
static size_t s_fb_size_bytes = 0;
static lcd_driver_flush_done_cb_t s_flush_done_cb = NULL;
static void *s_flush_done_user_ctx = NULL;
static bool s_backlight_warned = false;
static bool s_nt35510_backlight_ready = false;
static bool s_nt35510_power_ready = false;
static bool s_direct_mode = false;
static bool s_need_rgb565_byte_swap = false;

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

static bool IRAM_ATTR on_i80_color_trans_done(esp_lcd_panel_io_handle_t io,
                                              esp_lcd_panel_io_event_data_t *edata,
                                              void *user_ctx)
{
    (void)io;
    (void)edata;
    (void)user_ctx;
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

static void swap_rgb565_bytes(uint16_t *buf, size_t pixel_count)
{
    for (size_t i = 0; i < pixel_count; i++) {
        uint16_t color = buf[i];
        buf[i] = (uint16_t)((color << 8) | (color >> 8));
    }
}

static uint64_t gpio_pin_mask_from_num(gpio_num_t pin)
{
    uint64_t mask = 1ULL;
    for (int i = 0; i < (int)pin; i++) {
        mask <<= 1;
    }
    return mask;
}

static esp_err_t nt35510_backlight_init(void)
{
    if (s_nt35510_backlight_ready || LCD_I80_PIN_BACKLIGHT < 0) {
        return ESP_OK;
    }

    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
    };
    bk_gpio_config.pin_bit_mask = gpio_pin_mask_from_num(LCD_I80_PIN_BACKLIGHT);
    ESP_RETURN_ON_ERROR(gpio_config(&bk_gpio_config), TAG_LCD, "configure nt35510 backlight failed");
    s_nt35510_backlight_ready = true;
    return ESP_OK;
}

static esp_err_t nt35510_power_init(void)
{
    if (s_nt35510_power_ready || LCD_I80_PIN_POWER < 0) {
        return ESP_OK;
    }

    gpio_config_t power_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
    };
    power_gpio_config.pin_bit_mask = gpio_pin_mask_from_num(LCD_I80_PIN_POWER);
    ESP_RETURN_ON_ERROR(gpio_config(&power_gpio_config), TAG_LCD, "configure nt35510 power pin failed");
    s_nt35510_power_ready = true;
    return ESP_OK;
}

static esp_err_t nt35510_set_power(bool power_on)
{
    if (LCD_I80_PIN_POWER < 0) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(nt35510_power_init(), TAG_LCD, "nt35510 power pin init failed");
    gpio_set_level(LCD_I80_PIN_POWER,
#if CONFIG_DESK_CLOCK_NT35510_POWER_ACTIVE_HIGH
                   power_on ? 1 : 0
#else
                   power_on ? 0 : 1
#endif
    );
    return ESP_OK;
}

static esp_err_t nt35510_set_backlight(bool display_on)
{
    if (LCD_I80_PIN_BACKLIGHT < 0) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(nt35510_backlight_init(), TAG_LCD, "nt35510 backlight init failed");
    gpio_set_level(LCD_I80_PIN_BACKLIGHT,
#if CONFIG_DESK_CLOCK_NT35510_BK_LIGHT_ACTIVE_HIGH
                   display_on ? 1 : 0
#else
                   display_on ? 0 : 1
#endif
    );
    return ESP_OK;
}

static void log_nt35510_pin_configuration(void)
{
    ESP_LOGI(TAG_LCD, "NT35510 pins: WR=%d CS=%d DC=%d RST=%d BK=%d PWR=%d",
             LCD_I80_PIN_WR, LCD_I80_PIN_CS, LCD_I80_PIN_DC, LCD_I80_PIN_RST,
             LCD_I80_PIN_BACKLIGHT, LCD_I80_PIN_POWER);
    ESP_LOGI(TAG_LCD, "NT35510 data pins: D0=%d D1=%d D2=%d D3=%d D4=%d D5=%d D6=%d D7=%d",
             LCD_I80_PIN_D0, LCD_I80_PIN_D1, LCD_I80_PIN_D2, LCD_I80_PIN_D3,
             LCD_I80_PIN_D4, LCD_I80_PIN_D5, LCD_I80_PIN_D6, LCD_I80_PIN_D7);
#if CONFIG_DESK_CLOCK_NT35510_I80_BUS_WIDTH == 16
    ESP_LOGI(TAG_LCD, "NT35510 data pins: D8=%d D9=%d D10=%d D11=%d D12=%d D13=%d D14=%d D15=%d",
             LCD_I80_PIN_D8, LCD_I80_PIN_D9, LCD_I80_PIN_D10, LCD_I80_PIN_D11,
             LCD_I80_PIN_D12, LCD_I80_PIN_D13, LCD_I80_PIN_D14, LCD_I80_PIN_D15);
#endif
}

static esp_err_t validate_nt35510_pins(void)
{
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_WR >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "WR pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_CS >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "CS pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_DC >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "DC pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D0 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D0 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D1 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D1 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D2 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D2 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D3 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D3 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D4 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D4 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D5 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D5 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D6 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D6 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D7 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D7 pin is not configured");

#if CONFIG_DESK_CLOCK_NT35510_I80_BUS_WIDTH == 16
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D8 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D8 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D9 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D9 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D10 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D10 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D11 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D11 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D12 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D12 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D13 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D13 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D14 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D14 pin is not configured");
    ESP_RETURN_ON_FALSE(LCD_I80_PIN_D15 >= 0, ESP_ERR_INVALID_ARG, TAG_LCD, "D15 pin is not configured");
#endif

    return ESP_OK;
}

static esp_err_t init_nt35510_i80_panel(void)
{
    log_nt35510_pin_configuration();
    ESP_RETURN_ON_ERROR(validate_nt35510_pins(), TAG_LCD, "invalid nt35510 pin configuration");
    ESP_RETURN_ON_ERROR(nt35510_set_power(true), TAG_LCD, "turn on nt35510 power failed");
    ESP_RETURN_ON_ERROR(nt35510_set_backlight(false), TAG_LCD, "turn off nt35510 backlight failed");

    ESP_LOGI(TAG_LCD, "Initialize NT35510 I80 LCD %dx%d", LCD_H_RES, LCD_V_RES);
    ESP_LOGI(TAG_LCD, "Board profile: %s", DISPLAY_BOARD_NAME);
    ESP_LOGI(TAG_LCD, "I80 bus width=%d pclk=%d draw_buf_lines=%d",
             LCD_I80_BUS_WIDTH, LCD_I80_PIXEL_CLOCK_HZ, LCD_I80_DRAW_BUF_LINES);
    ESP_LOGI(TAG_LCD, "Panel color order=%s rgb565_byte_swap=%s",
#if CONFIG_DESK_CLOCK_NT35510_COLOR_ORDER_RGB
             "RGB",
#else
             "BGR",
#endif
#if CONFIG_DESK_CLOCK_NT35510_SWAP_RGB565_BYTES
             "on");
#else
             "off");
#endif
    ESP_LOGI(TAG_LCD, "If any pin shows -1, update sdkconfig before building");

    const esp_lcd_i80_bus_config_t bus_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num = LCD_I80_PIN_DC,
        .wr_gpio_num = LCD_I80_PIN_WR,
        .data_gpio_nums = {
            LCD_I80_PIN_D0, LCD_I80_PIN_D1, LCD_I80_PIN_D2, LCD_I80_PIN_D3,
            LCD_I80_PIN_D4, LCD_I80_PIN_D5, LCD_I80_PIN_D6, LCD_I80_PIN_D7,
#if CONFIG_DESK_CLOCK_NT35510_I80_BUS_WIDTH == 16
            LCD_I80_PIN_D8, LCD_I80_PIN_D9, LCD_I80_PIN_D10, LCD_I80_PIN_D11,
            LCD_I80_PIN_D12, LCD_I80_PIN_D13, LCD_I80_PIN_D14, LCD_I80_PIN_D15,
#endif
        },
        .bus_width = LCD_I80_BUS_WIDTH,
        .max_transfer_bytes = (size_t)LCD_H_RES * LCD_I80_DRAW_BUF_LINES * sizeof(uint16_t),
        .dma_burst_size = 64,
    };
    ESP_LOGI(TAG_LCD, "Creating I80 bus");
    ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus_config, &s_i80_bus), TAG_LCD, "create i80 bus failed");

    const esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = LCD_I80_PIN_CS,
        .pclk_hz = LCD_I80_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_i80_color_trans_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = LCD_I80_CMD_BITS,
        .lcd_param_bits = LCD_I80_PARAM_BITS,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
    };
    ESP_LOGI(TAG_LCD, "Creating I80 panel IO");
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i80(s_i80_bus, &io_config, &s_panel_io), TAG_LCD, "create i80 io failed");

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_I80_PIN_RST,
#if CONFIG_DESK_CLOCK_NT35510_COLOR_ORDER_RGB
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#endif
        .bits_per_pixel = 16,
    };
    ESP_LOGI(TAG_LCD, "Creating NT35510 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_nt35510(s_panel_io, &panel_config, &s_panel), TAG_LCD, "create nt35510 panel failed");

    ESP_LOGI(TAG_LCD, "Resetting NT35510 panel");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_LOGI(TAG_LCD, "Initializing NT35510 panel");
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

#if CONFIG_DESK_CLOCK_NT35510_SWAP_XY
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
#endif
#if CONFIG_DESK_CLOCK_NT35510_MIRROR_X || CONFIG_DESK_CLOCK_NT35510_MIRROR_Y
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel,
#if CONFIG_DESK_CLOCK_NT35510_MIRROR_X
                                         true,
#else
                                         false,
#endif
#if CONFIG_DESK_CLOCK_NT35510_MIRROR_Y
                                         true
#else
                                         false
#endif
    ));
#endif
#if CONFIG_DESK_CLOCK_NT35510_INVERT_COLOR
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    s_fb_size_bytes = (size_t)LCD_H_RES * LCD_I80_DRAW_BUF_LINES * sizeof(uint16_t);
    s_fb[0] = esp_lcd_i80_alloc_draw_buffer(s_panel_io, s_fb_size_bytes, MALLOC_CAP_SPIRAM);
    s_fb[1] = esp_lcd_i80_alloc_draw_buffer(s_panel_io, s_fb_size_bytes, MALLOC_CAP_SPIRAM);
    if (!s_fb[0] || !s_fb[1]) {
        if (s_fb[0]) {
            heap_caps_free(s_fb[0]);
            s_fb[0] = NULL;
        }
        if (s_fb[1]) {
            heap_caps_free(s_fb[1]);
            s_fb[1] = NULL;
        }
        ESP_LOGW(TAG_LCD, "PSRAM draw buffer alloc failed, retrying in internal RAM");
        s_fb[0] = esp_lcd_i80_alloc_draw_buffer(s_panel_io, s_fb_size_bytes, MALLOC_CAP_INTERNAL);
        s_fb[1] = esp_lcd_i80_alloc_draw_buffer(s_panel_io, s_fb_size_bytes, MALLOC_CAP_INTERNAL);
    }
    ESP_RETURN_ON_FALSE(s_fb[0] && s_fb[1], ESP_ERR_NO_MEM, TAG_LCD, "allocate nt35510 draw buffer failed");
    memset(s_fb[0], 0, s_fb_size_bytes);
    memset(s_fb[1], 0, s_fb_size_bytes);

    s_direct_mode = false;
    s_need_rgb565_byte_swap =
#if CONFIG_DESK_CLOCK_NT35510_SWAP_RGB565_BYTES
        true;
#else
        false;
#endif

    lcd_driver_set_brightness(100);
    ESP_LOGI(TAG_LCD, "NT35510 initialized buf0=%p buf1=%p bytes=%u",
             s_fb[0], s_fb[1], (unsigned)s_fb_size_bytes);
    return ESP_OK;
}

esp_err_t lcd_driver_init(void)
{
    return init_nt35510_i80_panel();
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
    return s_direct_mode;
}

bool lcd_driver_needs_rgb565_byte_swap(void)
{
    return s_need_rgb565_byte_swap;
}

esp_err_t lcd_driver_show_test_pattern(uint32_t pattern_index)
{
    if (!s_panel || !s_fb[0]) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int y = 0, chunk = 0; y < LCD_V_RES; y += LCD_I80_DRAW_BUF_LINES) {
        int lines = LCD_I80_DRAW_BUF_LINES;
        if (y + lines > LCD_V_RES) {
            lines = LCD_V_RES - y;
        }

        uint16_t *buf = s_fb[chunk % LCD_NUM_FB];
        fill_test_pattern_lines(buf, pattern_index, y, lines);
        if (s_need_rgb565_byte_swap) {
            swap_rgb565_bytes(buf, (size_t)LCD_H_RES * lines);
        }

        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + lines, buf),
            TAG_LCD, "draw diagnostic chunk failed");
        chunk++;
    }

    ESP_LOGI(TAG_LCD, "Display diagnostic pattern #%lu via I80 partial flush",
             (unsigned long)(pattern_index % 4));
    return ESP_OK;
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

    if (LCD_I80_PIN_BACKLIGHT < 0) {
        if (!s_backlight_warned) {
            ESP_LOGW(TAG_LCD, "NT35510 board backlight pin is not configured");
            s_backlight_warned = true;
        }
        return;
    }
    ESP_ERROR_CHECK(nt35510_set_backlight(display_on));
}
