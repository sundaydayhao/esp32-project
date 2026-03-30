#include "ui_renderer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd/lcd_driver.h"
#include "lvgl.h"
#include "net/ntp_client.h"
#include "net/weather_client.h"
#include "net/wifi_manager.h"
#include "sensor/dht11_sensor.h"

LV_FONT_DECLARE(ui_font_noto_sc_16);

#define LVGL_TICK_PERIOD_MS 5
#define UI_MIN_DELAY_MS 10
#define UI_MAX_DELAY_MS 100
#define UI_STATUS_UPDATE_MS 2000
#define UI_INDOOR_UPDATE_MS 2000
#define UI_CLOCK_UPDATE_MS 1000
#define UI_WEATHER_UPDATE_MS 30000
#define UI_FLUSH_TIMEOUT_MS 1000
#define UI_PI 3.14159265358979323846f
#define UI_DEGREE_SYMBOL "\xC2\xB0"

typedef enum {
    WEATHER_ICON_SUNNY = 0,
    WEATHER_ICON_PARTLY_CLOUDY,
    WEATHER_ICON_CLOUDY,
    WEATHER_ICON_RAIN,
    WEATHER_ICON_SNOW,
    WEATHER_ICON_HAZE,
} weather_icon_mode_t;

typedef struct {
    lv_obj_t *status_label;
    lv_obj_t *top_clock_label;
    lv_obj_t *battery_label;
    lv_obj_t *battery_fill;
    lv_obj_t *wifi_bars[4];
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_obj_t *clock_meta_label;
    lv_obj_t *clock_hour_hand;
    lv_obj_t *clock_minute_hand;
    lv_obj_t *clock_second_hand;
    lv_obj_t *indoor_temp;
    lv_obj_t *indoor_humidity;
    lv_obj_t *indoor_summary;
    lv_obj_t *weather_title;
    lv_obj_t *weather_temp;
    lv_obj_t *weather_cond;
    lv_obj_t *weather_meta;
    lv_obj_t *weather_update;
    lv_obj_t *detail_humidity;
    lv_obj_t *detail_feels;
    lv_obj_t *detail_wind;
    lv_obj_t *detail_vis;
    lv_obj_t *weather_sun;
    lv_obj_t *weather_rays[4];
    lv_obj_t *weather_cloud[4];
    lv_obj_t *weather_rain[3];
    lv_obj_t *weather_snow[3];
    lv_obj_t *weather_haze[3];
    lv_obj_t *forecast_date[3];
    lv_obj_t *forecast_cond[3];
    lv_obj_t *forecast_temp[3];

    char status_text[96];
    char top_clock_text[16];
    char battery_text[16];
    char time_text[16];
    char date_text[40];
    char clock_meta_text[32];
    char indoor_temp_text[16];
    char indoor_humidity_text[16];
    char indoor_summary_text[48];
    char weather_title_text[48];
    char weather_temp_text[16];
    char weather_cond_text[32];
    char weather_meta_text[32];
    char weather_update_text[32];
    char detail_humidity_text[24];
    char detail_feels_text[24];
    char detail_wind_text[24];
    char detail_vis_text[24];
    char forecast_date_text[3][16];
    char forecast_cond_text[3][24];
    char forecast_temp_text[3][16];
} ui_state_t;

static lv_display_t *s_display = NULL;
static esp_timer_handle_t s_lvgl_tick_timer = NULL;
static ui_state_t s_ui = {0};
static uint32_t s_flush_count = 0;
static volatile TaskHandle_t s_flush_wait_task = NULL;

static bool set_label_text_if_changed(lv_obj_t *label, char *storage, size_t storage_size, const char *text)
{
    if (!label || !storage || !text) {
        return false;
    }

    if (strncmp(storage, text, storage_size) == 0) {
        return false;
    }

    strlcpy(storage, text, storage_size);
    lv_label_set_text_static(label, storage);
    return true;
}

static const char *weather_condition_text(const char *icon_code)
{
    int code = icon_code ? atoi(icon_code) : 0;

    if (code == 100 || code == 150) return "晴朗";
    if ((code >= 101 && code <= 103) || (code >= 151 && code <= 153)) return "多云";
    if (code == 104 || code == 154) return "阴天";
    if (code == 312 || code == 313) return "雨雪";
    if (code >= 302 && code <= 304) return "雷雨";
    if (code >= 300 && code <= 399) return "降雨";
    if (code >= 400 && code <= 499) return "降雪";
    if (code == 507 || code == 508) return "沙尘";
    if (code == 502 || code == 511 || code == 512 || code == 513) return "雾霾";
    if (code >= 500 && code <= 515) return "雾霾";
    return "天气";
}

static weather_icon_mode_t weather_icon_mode_from_code(const char *icon_code)
{
    int code = icon_code ? atoi(icon_code) : 0;

    if (code == 100 || code == 150) {
        return WEATHER_ICON_SUNNY;
    }
    if ((code >= 101 && code <= 103) || (code >= 151 && code <= 153)) {
        return WEATHER_ICON_PARTLY_CLOUDY;
    }
    if (code >= 300 && code <= 399) {
        return WEATHER_ICON_RAIN;
    }
    if (code >= 400 && code <= 499) {
        return WEATHER_ICON_SNOW;
    }
    if ((code >= 500 && code <= 515) || code == 154) {
        return WEATHER_ICON_HAZE;
    }

    return WEATHER_ICON_CLOUDY;
}

static bool lvgl_flush_ready_cb(void *user_ctx)
{
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

static bool IRAM_ATTR lvgl_flush_notify_cb(void *user_ctx)
{
    (void)user_ctx;
    TaskHandle_t task = (TaskHandle_t)s_flush_wait_task;

    if (!task) {
        return false;
    }

    BaseType_t high_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(task, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(display);
    bool direct_mode = lcd_driver_uses_direct_mode();

    s_flush_count++;
    if (s_flush_count <= 5) {
        ESP_LOGI(TAG_UI, "LVGL flush #%lu area=(%d,%d)-(%d,%d) px_map=%p",
                 (unsigned long)s_flush_count,
                 area->x1, area->y1, area->x2, area->y2, px_map);
    }

    if (lcd_driver_needs_rgb565_byte_swap()) {
        lv_draw_sw_rgb565_swap(px_map, (area->x2 + 1 - area->x1) * (area->y2 + 1 - area->y1));
    }

    if (!direct_mode) {
        s_flush_wait_task = xTaskGetCurrentTaskHandle();
        xTaskNotifyStateClear(s_flush_wait_task);
    }

    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);

    if (!direct_mode) {
        uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(UI_FLUSH_TIMEOUT_MS));
        s_flush_wait_task = NULL;
        if (notified == 0) {
            ESP_LOGE(TAG_UI, "Timed out waiting for LCD flush completion");
        }
        lv_display_flush_ready(display);
    }
}

static void lvgl_tick_cb(void *arg)
{
    LV_UNUSED(arg);
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void style_panel(lv_obj_t *obj)
{
    lv_obj_set_style_radius(obj, 24, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x20384E), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x101A28), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x172A40), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(obj, 18, 0);
}

static void style_small_cjk_label(lv_obj_t *obj, lv_color_t color)
{
    /* Use one Simplified Chinese font for mixed Chinese/ASCII small text. */
    lv_obj_set_style_text_font(obj, &ui_font_noto_sc_16, 0);
    lv_obj_set_style_text_color(obj, color, 0);
}

static void style_chip(lv_obj_t *obj, lv_color_t bg_color)
{
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x25435C), 0);
    lv_obj_set_style_bg_color(obj, bg_color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static void style_transparent_obj(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static void set_obj_hidden(lv_obj_t *obj, bool hidden)
{
    if (!obj) {
        return;
    }

    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_weather_icon_mode(weather_icon_mode_t mode)
{
    bool show_sun = false;
    bool show_cloud = false;
    bool show_rain = false;
    bool show_snow = false;
    bool show_haze = false;

    switch (mode) {
    case WEATHER_ICON_SUNNY:
        show_sun = true;
        break;
    case WEATHER_ICON_PARTLY_CLOUDY:
        show_sun = true;
        show_cloud = true;
        break;
    case WEATHER_ICON_CLOUDY:
        show_cloud = true;
        break;
    case WEATHER_ICON_RAIN:
        show_cloud = true;
        show_rain = true;
        break;
    case WEATHER_ICON_SNOW:
        show_cloud = true;
        show_snow = true;
        break;
    case WEATHER_ICON_HAZE:
        show_cloud = true;
        show_haze = true;
        break;
    default:
        show_cloud = true;
        break;
    }

    set_obj_hidden(s_ui.weather_sun, !show_sun);
    for (int i = 0; i < 4; i++) {
        set_obj_hidden(s_ui.weather_rays[i], !show_sun);
        set_obj_hidden(s_ui.weather_cloud[i], !show_cloud);
    }
    for (int i = 0; i < 3; i++) {
        set_obj_hidden(s_ui.weather_rain[i], !show_rain);
        set_obj_hidden(s_ui.weather_snow[i], !show_snow);
        set_obj_hidden(s_ui.weather_haze[i], !show_haze);
    }
}

static void update_clock_visual(const struct tm *now)
{
    if (!now) {
        return;
    }

    int hour_angle = ((now->tm_hour % 12) * 300) + (now->tm_min * 5);
    int minute_angle = (now->tm_min * 60) + now->tm_sec;
    int second_angle = now->tm_sec * 60;

    if (s_ui.clock_hour_hand) {
        lv_obj_set_style_transform_rotation(s_ui.clock_hour_hand, hour_angle, 0);
    }
    if (s_ui.clock_minute_hand) {
        lv_obj_set_style_transform_rotation(s_ui.clock_minute_hand, minute_angle, 0);
    }
    if (s_ui.clock_second_hand) {
        lv_obj_set_style_transform_rotation(s_ui.clock_second_hand, second_angle, 0);
    }
}

static void format_weather_update_text(int64_t update_time, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }

    if (update_time <= 0) {
        strlcpy(out, "Updated --", out_size);
        return;
    }

    int64_t now = esp_timer_get_time() / 1000000;
    int64_t age = now - update_time;
    if (age < 0) {
        age = 0;
    }

    if (age < 60) {
        strlcpy(out, "Updated <1m", out_size);
    } else if (age < 3600) {
        snprintf(out, out_size, "Updated %lldm", (long long)(age / 60));
    } else {
        snprintf(out, out_size, "Updated %lldh", (long long)(age / 3600));
    }
}

static void format_elapsed_text_us(int64_t update_time_us, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }

    if (update_time_us <= 0) {
        strlcpy(out, "waiting", out_size);
        return;
    }

    int64_t age_s = (esp_timer_get_time() - update_time_us) / 1000000;
    if (age_s < 0) {
        age_s = 0;
    }

    if (age_s < 2) {
        strlcpy(out, "just now", out_size);
    } else if (age_s < 60) {
        snprintf(out, out_size, "%llds ago", (long long)age_s);
    } else {
        snprintf(out, out_size, "%lldm ago", (long long)(age_s / 60));
    }
}

static void set_wifi_signal_visual(bool connected, int8_t rssi)
{
    int active = 0;

    if (connected) {
        if (rssi >= -60) {
            active = 4;
        } else if (rssi >= -68) {
            active = 3;
        } else if (rssi >= -76) {
            active = 2;
        } else {
            active = 1;
        }
    }

    for (int i = 0; i < 4; i++) {
        if (!s_ui.wifi_bars[i]) {
            continue;
        }

        lv_obj_set_style_bg_color(s_ui.wifi_bars[i],
                                  i < active ? lv_color_hex(0x68C1FF) : lv_color_hex(0x31485E), 0);
        lv_obj_set_style_bg_opa(s_ui.wifi_bars[i],
                                i < active ? LV_OPA_COVER : LV_OPA_70, 0);
    }
}

static void set_battery_visual(int percent, bool valid)
{
    if (!s_ui.battery_fill) {
        return;
    }

    if (!valid) {
        lv_obj_set_width(s_ui.battery_fill, 0);
        return;
    }

    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    lv_obj_set_width(s_ui.battery_fill, (percent * 14) / 100);
    lv_obj_set_style_bg_color(s_ui.battery_fill,
                              percent > 20 ? lv_color_hex(0x7BE495) : lv_color_hex(0xFF8B6A), 0);
}

static void create_detail_card(lv_obj_t *parent, lv_obj_t **label, int x, int y, int width, int height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_set_pos(card, x, y);
    style_chip(card, lv_color_hex(0x111F2D));

    *label = lv_label_create(card);
    lv_obj_set_size(*label, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(*label, LV_ALIGN_CENTER, 0, 0);
    style_small_cjk_label(*label, lv_color_hex(0xD5DFEB));
    lv_obj_set_style_text_align(*label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(*label, "");
}

static void create_indoor_metric_card(lv_obj_t *parent,
                                      const char *title,
                                      lv_obj_t **value_label,
                                      int x, int y, int width, int height,
                                      lv_color_t bg_color)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_set_pos(card, x, y);
    style_chip(card, bg_color);
    lv_obj_set_style_pad_all(card, 12, 0);

    lv_obj_t *title_label = lv_label_create(card);
    lv_obj_set_pos(title_label, 0, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x91B7DC), 0);
    lv_label_set_text(title_label, title);

    *value_label = lv_label_create(card);
    lv_obj_set_width(*value_label, LV_PCT(100));
    lv_obj_align(*value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_font(*value_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(*value_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(*value_label, "--");
}

static void create_clock_visual(lv_obj_t *parent)
{
    const int face_size = 184;
    const int center = face_size / 2;
    const int orbit = 72;

    lv_obj_t *face = lv_obj_create(parent);
    lv_obj_set_size(face, face_size, face_size);
    lv_obj_set_pos(face, 96, 40);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(face, 2, 0);
    lv_obj_set_style_border_color(face, lv_color_hex(0x325A76), 0);
    lv_obj_set_style_bg_color(face, lv_color_hex(0x091320), 0);
    lv_obj_set_style_bg_grad_color(face, lv_color_hex(0x122537), 0);
    lv_obj_set_style_bg_grad_dir(face, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_width(face, 0, 0);
    lv_obj_set_style_shadow_opa(face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(face, 0, 0);

    for (int i = 0; i < 12; i++) {
        float angle = ((float)i * 30.0f) * (UI_PI / 180.0f);
        int dot_size = (i % 3 == 0) ? 8 : 4;
        int x = center + (int)(sinf(angle) * (float)orbit) - (dot_size / 2);
        int y = center - (int)(cosf(angle) * (float)orbit) - (dot_size / 2);

        lv_obj_t *dot = lv_obj_create(face);
        lv_obj_set_size(dot, dot_size, dot_size);
        lv_obj_set_pos(dot, x, y);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, i % 3 == 0 ? lv_color_hex(0x76D0FF) : lv_color_hex(0x40657E), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
    }

    s_ui.clock_hour_hand = lv_obj_create(face);
    lv_obj_set_size(s_ui.clock_hour_hand, 6, 46);
    lv_obj_set_pos(s_ui.clock_hour_hand, center - 3, center - 46);
    lv_obj_set_style_radius(s_ui.clock_hour_hand, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ui.clock_hour_hand, 0, 0);
    lv_obj_set_style_bg_color(s_ui.clock_hour_hand, lv_color_hex(0xF7F9FD), 0);
    lv_obj_set_style_bg_opa(s_ui.clock_hour_hand, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_pivot_x(s_ui.clock_hour_hand, 3, 0);
    lv_obj_set_style_transform_pivot_y(s_ui.clock_hour_hand, 46, 0);

    s_ui.clock_minute_hand = lv_obj_create(face);
    lv_obj_set_size(s_ui.clock_minute_hand, 4, 66);
    lv_obj_set_pos(s_ui.clock_minute_hand, center - 2, center - 66);
    lv_obj_set_style_radius(s_ui.clock_minute_hand, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ui.clock_minute_hand, 0, 0);
    lv_obj_set_style_bg_color(s_ui.clock_minute_hand, lv_color_hex(0x7CD6FF), 0);
    lv_obj_set_style_bg_opa(s_ui.clock_minute_hand, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_pivot_x(s_ui.clock_minute_hand, 2, 0);
    lv_obj_set_style_transform_pivot_y(s_ui.clock_minute_hand, 66, 0);

    s_ui.clock_second_hand = lv_obj_create(face);
    lv_obj_set_size(s_ui.clock_second_hand, 2, 76);
    lv_obj_set_pos(s_ui.clock_second_hand, center - 1, center - 76);
    lv_obj_set_style_radius(s_ui.clock_second_hand, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ui.clock_second_hand, 0, 0);
    lv_obj_set_style_bg_color(s_ui.clock_second_hand, lv_color_hex(0xFFB057), 0);
    lv_obj_set_style_bg_opa(s_ui.clock_second_hand, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_pivot_x(s_ui.clock_second_hand, 1, 0);
    lv_obj_set_style_transform_pivot_y(s_ui.clock_second_hand, 76, 0);

    lv_obj_t *center_dot = lv_obj_create(face);
    lv_obj_set_size(center_dot, 14, 14);
    lv_obj_set_pos(center_dot, center - 7, center - 7);
    lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(center_dot, 0, 0);
    lv_obj_set_style_bg_color(center_dot, lv_color_hex(0xFFB057), 0);
    lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, 0);
}

static void create_weather_visual(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_set_size(wrap, 112, 108);
    lv_obj_set_pos(wrap, x, y);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);

    s_ui.weather_sun = lv_obj_create(wrap);
    lv_obj_set_size(s_ui.weather_sun, 34, 34);
    lv_obj_set_pos(s_ui.weather_sun, 58, 8);
    lv_obj_set_style_radius(s_ui.weather_sun, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ui.weather_sun, 0, 0);
    lv_obj_set_style_bg_color(s_ui.weather_sun, lv_color_hex(0xFFBF52), 0);
    lv_obj_set_style_bg_opa(s_ui.weather_sun, LV_OPA_COVER, 0);

    const int ray_pos[4][4] = {
        {72, 0, 4, 12},
        {72, 46, 4, 12},
        {46, 23, 12, 4},
        {92, 23, 12, 4},
    };
    for (int i = 0; i < 4; i++) {
        s_ui.weather_rays[i] = lv_obj_create(wrap);
        lv_obj_set_size(s_ui.weather_rays[i], ray_pos[i][2], ray_pos[i][3]);
        lv_obj_set_pos(s_ui.weather_rays[i], ray_pos[i][0], ray_pos[i][1]);
        lv_obj_set_style_radius(s_ui.weather_rays[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_ui.weather_rays[i], 0, 0);
        lv_obj_set_style_bg_color(s_ui.weather_rays[i], lv_color_hex(0xFFD58C), 0);
        lv_obj_set_style_bg_opa(s_ui.weather_rays[i], LV_OPA_COVER, 0);
    }

    const int cloud_pos[4][4] = {
        {18, 40, 30, 30},
        {42, 30, 40, 40},
        {70, 36, 28, 28},
        {18, 52, 82, 24},
    };
    for (int i = 0; i < 4; i++) {
        s_ui.weather_cloud[i] = lv_obj_create(wrap);
        lv_obj_set_size(s_ui.weather_cloud[i], cloud_pos[i][2], cloud_pos[i][3]);
        lv_obj_set_pos(s_ui.weather_cloud[i], cloud_pos[i][0], cloud_pos[i][1]);
        lv_obj_set_style_radius(s_ui.weather_cloud[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_ui.weather_cloud[i], 0, 0);
        lv_obj_set_style_bg_color(s_ui.weather_cloud[i], lv_color_hex(0xDCE6F0), 0);
        lv_obj_set_style_bg_opa(s_ui.weather_cloud[i], LV_OPA_COVER, 0);
    }

    for (int i = 0; i < 3; i++) {
        s_ui.weather_rain[i] = lv_obj_create(wrap);
        lv_obj_set_size(s_ui.weather_rain[i], 4, 18);
        lv_obj_set_pos(s_ui.weather_rain[i], 34 + (i * 22), 78 + (i % 2) * 4);
        lv_obj_set_style_radius(s_ui.weather_rain[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_ui.weather_rain[i], 0, 0);
        lv_obj_set_style_bg_color(s_ui.weather_rain[i], lv_color_hex(0x5BC0FF), 0);
        lv_obj_set_style_bg_opa(s_ui.weather_rain[i], LV_OPA_COVER, 0);

        s_ui.weather_snow[i] = lv_obj_create(wrap);
        lv_obj_set_size(s_ui.weather_snow[i], 8, 8);
        lv_obj_set_pos(s_ui.weather_snow[i], 32 + (i * 22), 82 + (i % 2) * 4);
        lv_obj_set_style_radius(s_ui.weather_snow[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_ui.weather_snow[i], 0, 0);
        lv_obj_set_style_bg_color(s_ui.weather_snow[i], lv_color_hex(0xF7FBFF), 0);
        lv_obj_set_style_bg_opa(s_ui.weather_snow[i], LV_OPA_COVER, 0);

        s_ui.weather_haze[i] = lv_obj_create(wrap);
        lv_obj_set_size(s_ui.weather_haze[i], 50, 4);
        lv_obj_set_pos(s_ui.weather_haze[i], 30 - (i % 2) * 8, 80 + i * 10);
        lv_obj_set_style_radius(s_ui.weather_haze[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_ui.weather_haze[i], 0, 0);
        lv_obj_set_style_bg_color(s_ui.weather_haze[i], lv_color_hex(0xAAB9C8), 0);
        lv_obj_set_style_bg_opa(s_ui.weather_haze[i], LV_OPA_70, 0);
    }

    set_weather_icon_mode(WEATHER_ICON_PARTLY_CLOUDY);
}

static void create_ui(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07111C), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x10263D), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);

    lv_obj_t *glow_left = lv_obj_create(screen);
    lv_obj_set_size(glow_left, 220, 220);
    lv_obj_set_pos(glow_left, -40, -70);
    lv_obj_set_style_radius(glow_left, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(glow_left, 0, 0);
    lv_obj_set_style_bg_color(glow_left, lv_color_hex(0x184C77), 0);
    lv_obj_set_style_bg_opa(glow_left, LV_OPA_20, 0);

    lv_obj_t *glow_right = lv_obj_create(screen);
    lv_obj_set_size(glow_right, 180, 180);
    lv_obj_set_pos(glow_right, 650, 326);
    lv_obj_set_style_radius(glow_right, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(glow_right, 0, 0);
    lv_obj_set_style_bg_color(glow_right, lv_color_hex(0xFFB057), 0);
    lv_obj_set_style_bg_opa(glow_right, LV_OPA_20, 0);

    lv_obj_t *status_bar = lv_obj_create(screen);
    lv_obj_set_size(status_bar, LCD_H_RES - 32, 38);
    lv_obj_set_pos(status_bar, 16, 12);
    lv_obj_set_style_radius(status_bar, 12, 0);
    lv_obj_set_style_border_width(status_bar, 1, 0);
    lv_obj_set_style_border_color(status_bar, lv_color_hex(0x22394E), 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x0C1724), 0);
    lv_obj_set_style_pad_hor(status_bar, 14, 0);
    lv_obj_set_style_pad_ver(status_bar, 8, 0);

    lv_obj_t *wifi_wrap = lv_obj_create(status_bar);
    lv_obj_set_size(wifi_wrap, 96, 20);
    lv_obj_align(wifi_wrap, LV_ALIGN_LEFT_MID, 0, 0);
    style_transparent_obj(wifi_wrap);

    const int wifi_bar_x[4] = {0, 6, 12, 18};
    const int wifi_bar_y[4] = {12, 9, 6, 3};
    const int wifi_bar_h[4] = {4, 7, 10, 13};
    for (int i = 0; i < 4; i++) {
        s_ui.wifi_bars[i] = lv_obj_create(wifi_wrap);
        lv_obj_set_size(s_ui.wifi_bars[i], 4, wifi_bar_h[i]);
        lv_obj_set_pos(s_ui.wifi_bars[i], wifi_bar_x[i], wifi_bar_y[i]);
        lv_obj_set_style_radius(s_ui.wifi_bars[i], 2, 0);
        lv_obj_set_style_border_width(s_ui.wifi_bars[i], 0, 0);
        lv_obj_set_style_bg_color(s_ui.wifi_bars[i], lv_color_hex(0x31485E), 0);
        lv_obj_set_style_bg_opa(s_ui.wifi_bars[i], LV_OPA_70, 0);
        lv_obj_set_style_pad_all(s_ui.wifi_bars[i], 0, 0);
    }

    s_ui.status_label = lv_label_create(wifi_wrap);
    lv_obj_set_style_text_font(s_ui.status_label, &ui_font_noto_sc_16, 0);
    lv_obj_set_style_text_color(s_ui.status_label, lv_color_hex(0xB9C7D8), 0);
    lv_obj_set_pos(s_ui.status_label, 30, 0);
    lv_label_set_text(s_ui.status_label, "");

    s_ui.top_clock_label = lv_label_create(status_bar);
    lv_obj_set_style_text_font(s_ui.top_clock_label, &ui_font_noto_sc_16, 0);
    lv_obj_set_style_text_color(s_ui.top_clock_label, lv_color_hex(0xF4F7FB), 0);
    lv_obj_align(s_ui.top_clock_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(s_ui.top_clock_label, "");

    lv_obj_t *battery_wrap = lv_obj_create(status_bar);
    lv_obj_set_size(battery_wrap, 76, 20);
    lv_obj_align(battery_wrap, LV_ALIGN_RIGHT_MID, 0, 0);
    style_transparent_obj(battery_wrap);

    lv_obj_t *battery_body = lv_obj_create(battery_wrap);
    lv_obj_set_size(battery_body, 20, 12);
    lv_obj_set_pos(battery_body, 0, 4);
    lv_obj_set_style_radius(battery_body, 3, 0);
    lv_obj_set_style_border_width(battery_body, 1, 0);
    lv_obj_set_style_border_color(battery_body, lv_color_hex(0x7D92A9), 0);
    lv_obj_set_style_bg_opa(battery_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(battery_body, 0, 0);

    lv_obj_t *battery_tip = lv_obj_create(battery_wrap);
    lv_obj_set_size(battery_tip, 3, 6);
    lv_obj_set_pos(battery_tip, 21, 7);
    lv_obj_set_style_radius(battery_tip, 1, 0);
    lv_obj_set_style_border_width(battery_tip, 0, 0);
    lv_obj_set_style_bg_color(battery_tip, lv_color_hex(0x7D92A9), 0);
    lv_obj_set_style_bg_opa(battery_tip, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(battery_tip, 0, 0);

    s_ui.battery_fill = lv_obj_create(battery_body);
    lv_obj_set_size(s_ui.battery_fill, 0, 8);
    lv_obj_set_pos(s_ui.battery_fill, 2, 2);
    lv_obj_set_style_radius(s_ui.battery_fill, 2, 0);
    lv_obj_set_style_border_width(s_ui.battery_fill, 0, 0);
    lv_obj_set_style_bg_color(s_ui.battery_fill, lv_color_hex(0x7BE495), 0);
    lv_obj_set_style_bg_opa(s_ui.battery_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_ui.battery_fill, 0, 0);

    s_ui.battery_label = lv_label_create(battery_wrap);
    lv_obj_set_style_text_font(s_ui.battery_label, &ui_font_noto_sc_16, 0);
    lv_obj_set_style_text_color(s_ui.battery_label, lv_color_hex(0xB9C7D8), 0);
    lv_obj_set_pos(s_ui.battery_label, 30, 0);
    lv_label_set_text(s_ui.battery_label, "");

    lv_obj_t *left_panel = lv_obj_create(screen);
    lv_obj_set_size(left_panel, 376, 395);
    lv_obj_set_pos(left_panel, 16, 66);
    style_panel(left_panel);

    lv_obj_t *clock_title = lv_label_create(left_panel);
    lv_obj_set_pos(clock_title, 0, 0);
    lv_obj_set_style_text_font(clock_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(clock_title, lv_color_hex(0x68C1FF), 0);
    lv_label_set_text(clock_title, "TIME");

    s_ui.time_label = lv_label_create(left_panel);
    lv_obj_set_size(s_ui.time_label, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_ui.time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_ui.time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(s_ui.time_label, 0, 236);
    lv_obj_set_style_text_align(s_ui.time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_ui.time_label, "");

    s_ui.date_label = lv_label_create(left_panel);
    lv_obj_set_size(s_ui.date_label, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_pos(s_ui.date_label, 0, 290);
    style_small_cjk_label(s_ui.date_label, lv_color_hex(0xA7B5C8));
    lv_obj_set_style_text_align(s_ui.date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_ui.date_label, "");

    s_ui.clock_meta_label = lv_label_create(left_panel);
    lv_obj_set_size(s_ui.clock_meta_label, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_pos(s_ui.clock_meta_label, 0, 334);
    style_small_cjk_label(s_ui.clock_meta_label, lv_color_hex(0x7D92A9));
    lv_obj_set_style_text_align(s_ui.clock_meta_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_ui.clock_meta_label, "");

    create_clock_visual(left_panel);

    lv_obj_t *indoor_panel = lv_obj_create(screen);
    lv_obj_set_size(indoor_panel, 376, 124);
    lv_obj_set_pos(indoor_panel, 408, 66);
    style_panel(indoor_panel);

    lv_obj_t *indoor_head = lv_label_create(indoor_panel);
    lv_obj_set_pos(indoor_head, 0, 0);
    lv_obj_set_style_text_font(indoor_head, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(indoor_head, lv_color_hex(0x68C1FF), 0);
    lv_label_set_text(indoor_head, "INDOOR");

    lv_obj_t *indoor_pin = lv_label_create(indoor_panel);
    lv_obj_align(indoor_pin, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_text_font(indoor_pin, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(indoor_pin, lv_color_hex(0x91B7DC), 0);
    lv_label_set_text(indoor_pin, "DHT11 / GPIO45");

    create_indoor_metric_card(indoor_panel, "TEMP", &s_ui.indoor_temp,
                              0, 30, 160, 66, lv_color_hex(0x13273D));
    create_indoor_metric_card(indoor_panel, "RH", &s_ui.indoor_humidity,
                              160, 30, 160, 66, lv_color_hex(0x102A2F));

    s_ui.indoor_summary = lv_label_create(indoor_panel);
    lv_obj_set_width(s_ui.indoor_summary, LV_PCT(100));
    lv_obj_set_pos(s_ui.indoor_summary, 0, 98);
    style_small_cjk_label(s_ui.indoor_summary, lv_color_hex(0xA7B5C8));
    lv_obj_set_style_text_align(s_ui.indoor_summary, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_ui.indoor_summary, "");

    lv_obj_t *right_panel = lv_obj_create(screen);
    lv_obj_set_size(right_panel, 376, 259);
    lv_obj_set_pos(right_panel, 408, 202);
    style_panel(right_panel);

    lv_obj_t *weather_head = lv_label_create(right_panel);
    lv_obj_set_pos(weather_head, 0, 0);
    lv_obj_set_style_text_font(weather_head, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(weather_head, lv_color_hex(0x68C1FF), 0);
    lv_label_set_text(weather_head, "OUTDOOR");

    s_ui.weather_update = lv_label_create(right_panel);
    lv_obj_set_size(s_ui.weather_update, 120, LV_SIZE_CONTENT);
    lv_obj_set_pos(s_ui.weather_update, 184, 0);
    style_small_cjk_label(s_ui.weather_update, lv_color_hex(0x7D92A9));
    lv_obj_set_style_text_align(s_ui.weather_update, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_ui.weather_update, "");

    s_ui.weather_title = lv_label_create(right_panel);
    lv_obj_set_size(s_ui.weather_title, 180, LV_SIZE_CONTENT);
    lv_obj_set_pos(s_ui.weather_title, 0, 28);
    style_small_cjk_label(s_ui.weather_title, lv_color_hex(0xFFFFFF));
    lv_label_set_text(s_ui.weather_title, "");

    s_ui.weather_temp = lv_label_create(right_panel);
    lv_obj_set_pos(s_ui.weather_temp, 0, 52);
    lv_obj_set_style_text_font(s_ui.weather_temp, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_ui.weather_temp, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(s_ui.weather_temp, "");

    s_ui.weather_meta = lv_label_create(right_panel);
    lv_obj_set_size(s_ui.weather_meta, 180, LV_SIZE_CONTENT);
    lv_obj_set_pos(s_ui.weather_meta, 0, 94);
    lv_obj_set_style_text_font(s_ui.weather_meta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.weather_meta, lv_color_hex(0xA7B5C8), 0);
    lv_label_set_text(s_ui.weather_meta, "");

    s_ui.weather_cond = lv_label_create(right_panel);
    lv_obj_set_size(s_ui.weather_cond, 112, LV_SIZE_CONTENT);
    lv_obj_set_pos(s_ui.weather_cond, 194, 116);
    style_small_cjk_label(s_ui.weather_cond, lv_color_hex(0x68C1FF));
    lv_obj_set_style_text_align(s_ui.weather_cond, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_ui.weather_cond, "");

    create_weather_visual(right_panel, 192, 22);

    create_detail_card(right_panel, &s_ui.detail_humidity, 0, 154, 148, 34);
    create_detail_card(right_panel, &s_ui.detail_feels, 156, 154, 148, 34);
    create_detail_card(right_panel, &s_ui.detail_wind, 0, 196, 148, 34);
    create_detail_card(right_panel, &s_ui.detail_vis, 156, 196, 148, 34);

    set_label_text_if_changed(s_ui.status_label, s_ui.status_text, sizeof(s_ui.status_text), "WiFi");
    set_label_text_if_changed(s_ui.top_clock_label, s_ui.top_clock_text, sizeof(s_ui.top_clock_text), "Digital Clock");
    set_label_text_if_changed(s_ui.battery_label, s_ui.battery_text, sizeof(s_ui.battery_text), "100%");
    set_battery_visual(100, true);
    set_wifi_signal_visual(false, -127);
    set_label_text_if_changed(s_ui.clock_meta_label, s_ui.clock_meta_text, sizeof(s_ui.clock_meta_text), "NTP");
    set_label_text_if_changed(s_ui.indoor_temp, s_ui.indoor_temp_text, sizeof(s_ui.indoor_temp_text), "--");
    set_label_text_if_changed(s_ui.indoor_humidity, s_ui.indoor_humidity_text, sizeof(s_ui.indoor_humidity_text), "--");
    set_label_text_if_changed(s_ui.indoor_summary, s_ui.indoor_summary_text, sizeof(s_ui.indoor_summary_text), "Waiting for DHT11");
    set_label_text_if_changed(s_ui.weather_title, s_ui.weather_title_text, sizeof(s_ui.weather_title_text), "Weather");
    set_label_text_if_changed(s_ui.weather_cond, s_ui.weather_cond_text, sizeof(s_ui.weather_cond_text), "");
    set_label_text_if_changed(s_ui.weather_meta, s_ui.weather_meta_text, sizeof(s_ui.weather_meta_text), "-- ~ --");
    set_label_text_if_changed(s_ui.weather_update, s_ui.weather_update_text, sizeof(s_ui.weather_update_text), "Updated --");
}

static void update_status_text(void)
{
    bool connected = wifi_manager_is_connected();

    set_label_text_if_changed(s_ui.status_label, s_ui.status_text, sizeof(s_ui.status_text),
                              connected ? "WiFi" : "WiFi");
    set_label_text_if_changed(s_ui.battery_label, s_ui.battery_text, sizeof(s_ui.battery_text), "100%");
    set_wifi_signal_visual(connected, wifi_manager_get_rssi());
    set_battery_visual(100, true);
}

static void update_clock_text(void)
{
    static const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    struct tm now;
    char new_time[sizeof(s_ui.time_text)];
    char new_date[sizeof(s_ui.date_text)];
    char clock_meta_text[sizeof(s_ui.clock_meta_text)];

    ntp_client_get_local_time(&now);

    snprintf(new_time, sizeof(new_time), "%02d:%02d:%02d", now.tm_hour, now.tm_min, now.tm_sec);
    snprintf(new_date, sizeof(new_date), "%04d-%02d-%02d %s",
             now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
             (now.tm_wday >= 0 && now.tm_wday < 7) ? weekdays[now.tm_wday] : "--");
    strlcpy(clock_meta_text, ntp_client_is_synced() ? "NTP synced" : "NTP syncing", sizeof(clock_meta_text));

    set_label_text_if_changed(s_ui.time_label, s_ui.time_text, sizeof(s_ui.time_text), new_time);
    set_label_text_if_changed(s_ui.date_label, s_ui.date_text, sizeof(s_ui.date_text), new_date);
    set_label_text_if_changed(s_ui.clock_meta_label, s_ui.clock_meta_text, sizeof(s_ui.clock_meta_text), clock_meta_text);
    update_clock_visual(&now);
}

static void update_indoor_text(void)
{
    dht11_data_t indoor = {0};
    char indoor_temp_text[sizeof(s_ui.indoor_temp_text)];
    char indoor_humidity_text[sizeof(s_ui.indoor_humidity_text)];
    char indoor_summary_text[sizeof(s_ui.indoor_summary_text)];
    char age_text[20];

    dht11_sensor_get_data(&indoor);

    if (!indoor.valid) {
        strlcpy(indoor_temp_text, "--", sizeof(indoor_temp_text));
        strlcpy(indoor_humidity_text, "--", sizeof(indoor_humidity_text));
        if (indoor.error_count > 0) {
            snprintf(indoor_summary_text, sizeof(indoor_summary_text),
                     "DHT11 retry / err %lu", (unsigned long)indoor.error_count);
        } else {
            strlcpy(indoor_summary_text, "Waiting for DHT11", sizeof(indoor_summary_text));
        }
    } else {
        snprintf(indoor_temp_text, sizeof(indoor_temp_text), "%d%sC",
                 indoor.temperature_c, UI_DEGREE_SYMBOL);
        snprintf(indoor_humidity_text, sizeof(indoor_humidity_text), "%u%%", indoor.humidity);
        format_elapsed_text_us(indoor.update_time_us, age_text, sizeof(age_text));
        snprintf(indoor_summary_text, sizeof(indoor_summary_text), "GPIO45 / %s", age_text);
    }

    set_label_text_if_changed(s_ui.indoor_temp, s_ui.indoor_temp_text, sizeof(s_ui.indoor_temp_text), indoor_temp_text);
    set_label_text_if_changed(s_ui.indoor_humidity, s_ui.indoor_humidity_text, sizeof(s_ui.indoor_humidity_text), indoor_humidity_text);
    set_label_text_if_changed(s_ui.indoor_summary, s_ui.indoor_summary_text, sizeof(s_ui.indoor_summary_text), indoor_summary_text);
}

static void update_weather_text(void)
{
    weather_data_t weather = {0};
    char weather_title_text[sizeof(s_ui.weather_title_text)];
    char weather_temp_text[sizeof(s_ui.weather_temp_text)];
    char weather_cond_text[sizeof(s_ui.weather_cond_text)];
    char weather_meta_text[sizeof(s_ui.weather_meta_text)];
    char weather_update_text[sizeof(s_ui.weather_update_text)];
    char detail_humidity_text[sizeof(s_ui.detail_humidity_text)];
    char detail_feels_text[sizeof(s_ui.detail_feels_text)];
    char detail_wind_text[sizeof(s_ui.detail_wind_text)];
    char detail_vis_text[sizeof(s_ui.detail_vis_text)];

    weather_client_get_data(&weather);

    if (!weather.valid) {
        strlcpy(weather_title_text, "Weather", sizeof(weather_title_text));
        strlcpy(weather_temp_text, "", sizeof(weather_temp_text));
        strlcpy(weather_cond_text, "Updating...", sizeof(weather_cond_text));
        snprintf(weather_meta_text, sizeof(weather_meta_text), "-- ~ --%sC", UI_DEGREE_SYMBOL);
        strlcpy(weather_update_text, "Updated --", sizeof(weather_update_text));
        strlcpy(detail_humidity_text, "RH --", sizeof(detail_humidity_text));
        strlcpy(detail_feels_text, "Feels --", sizeof(detail_feels_text));
        strlcpy(detail_wind_text, "Wind --", sizeof(detail_wind_text));
        strlcpy(detail_vis_text, "Vis --", sizeof(detail_vis_text));
        set_weather_icon_mode(WEATHER_ICON_PARTLY_CLOUDY);
    } else {
        strlcpy(weather_title_text, "Outdoor Weather", sizeof(weather_title_text));
        snprintf(weather_temp_text, sizeof(weather_temp_text), "%d%sC",
                 weather.temp_now, UI_DEGREE_SYMBOL);
        snprintf(weather_cond_text, sizeof(weather_cond_text), "%s", weather_condition_text(weather.icon_code));
        snprintf(weather_meta_text, sizeof(weather_meta_text), "%d ~ %d%sC",
                 weather.temp_low, weather.temp_high, UI_DEGREE_SYMBOL);
        format_weather_update_text(weather.update_time, weather_update_text, sizeof(weather_update_text));
        snprintf(detail_humidity_text, sizeof(detail_humidity_text), "RH %u%%", weather.humidity);
        snprintf(detail_feels_text, sizeof(detail_feels_text), "Feels %dC", weather.temp_feels);
        snprintf(detail_wind_text, sizeof(detail_wind_text), "Wind %ukm/h", weather.wind_speed);
        snprintf(detail_vis_text, sizeof(detail_vis_text), "Vis %ukm", weather.visibility);
        set_weather_icon_mode(weather_icon_mode_from_code(weather.icon_code));
    }

    set_label_text_if_changed(s_ui.weather_title, s_ui.weather_title_text, sizeof(s_ui.weather_title_text), weather_title_text);
    set_label_text_if_changed(s_ui.weather_temp, s_ui.weather_temp_text, sizeof(s_ui.weather_temp_text), weather_temp_text);
    set_label_text_if_changed(s_ui.weather_cond, s_ui.weather_cond_text, sizeof(s_ui.weather_cond_text), weather_cond_text);
    set_label_text_if_changed(s_ui.weather_meta, s_ui.weather_meta_text, sizeof(s_ui.weather_meta_text), weather_meta_text);
    set_label_text_if_changed(s_ui.weather_update, s_ui.weather_update_text, sizeof(s_ui.weather_update_text), weather_update_text);
    set_label_text_if_changed(s_ui.detail_humidity, s_ui.detail_humidity_text, sizeof(s_ui.detail_humidity_text), detail_humidity_text);
    set_label_text_if_changed(s_ui.detail_feels, s_ui.detail_feels_text, sizeof(s_ui.detail_feels_text), detail_feels_text);
    set_label_text_if_changed(s_ui.detail_wind, s_ui.detail_wind_text, sizeof(s_ui.detail_wind_text), detail_wind_text);
    set_label_text_if_changed(s_ui.detail_vis, s_ui.detail_vis_text, sizeof(s_ui.detail_vis_text), detail_vis_text);
}

static void update_all_ui_text(void)
{
    update_status_text();
    update_clock_text();
    update_indoor_text();
    update_weather_text();
}

esp_err_t ui_renderer_init(void)
{
    esp_lcd_panel_handle_t panel = lcd_driver_get_panel();
    uint16_t *fb0 = NULL;
    uint16_t *fb1 = NULL;
    size_t fb_size = 0;
    lv_display_render_mode_t render_mode = LV_DISPLAY_RENDER_MODE_PARTIAL;

    if (!panel) {
        return ESP_ERR_INVALID_STATE;
    }

    lcd_driver_get_framebuf(&fb0, &fb1);
    fb_size = lcd_driver_get_framebuf_size();
    if (!fb0 || !fb1 || fb_size == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    if (lcd_driver_uses_direct_mode()) {
        render_mode = LV_DISPLAY_RENDER_MODE_DIRECT;
    }

    lv_init();

    s_display = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_user_data(s_display, panel);
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display, fb0, fb1, fb_size, render_mode);
    lv_display_set_flush_cb(s_display, lvgl_flush_cb);

    if (lcd_driver_uses_direct_mode()) {
        ESP_ERROR_CHECK(lcd_driver_register_flush_done_callback(lvgl_flush_ready_cb, s_display));
    } else {
        ESP_ERROR_CHECK(lcd_driver_register_flush_done_callback(lvgl_flush_notify_cb, NULL));
    }

    const esp_timer_create_args_t timer_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    create_ui();
    update_all_ui_text();
    lv_obj_invalidate(lv_screen_active());
    (void)lv_timer_handler();

    ESP_LOGI(TAG_UI, "LVGL UI initialized panel=%p fb0=%p fb1=%p size=%u mode=%s",
             panel, fb0, fb1, (unsigned)fb_size,
             render_mode == LV_DISPLAY_RENDER_MODE_DIRECT ? "direct" : "partial");
    return ESP_OK;
}

void ui_renderer_task(void *arg)
{
    int64_t last_status_update_us = 0;
    int64_t last_indoor_update_us = 0;
    int64_t last_clock_update_us = 0;
    int64_t last_weather_update_us = 0;
    LV_UNUSED(arg);

    while (1) {
        int64_t now_us = esp_timer_get_time();
        uint32_t delay_ms;

        if (last_status_update_us == 0 || now_us - last_status_update_us >= (int64_t)UI_STATUS_UPDATE_MS * 1000) {
            update_status_text();
            last_status_update_us = now_us;
        }

        if (last_indoor_update_us == 0 || now_us - last_indoor_update_us >= (int64_t)UI_INDOOR_UPDATE_MS * 1000) {
            update_indoor_text();
            last_indoor_update_us = now_us;
        }

        if (last_clock_update_us == 0 || now_us - last_clock_update_us >= (int64_t)UI_CLOCK_UPDATE_MS * 1000) {
            update_clock_text();
            last_clock_update_us = now_us;
        }

        if (last_weather_update_us == 0 || now_us - last_weather_update_us >= (int64_t)UI_WEATHER_UPDATE_MS * 1000) {
            update_weather_text();
            last_weather_update_us = now_us;
        }

        delay_ms = lv_timer_handler();
        if (delay_ms < UI_MIN_DELAY_MS) {
            delay_ms = UI_MIN_DELAY_MS;
        } else if (delay_ms > UI_MAX_DELAY_MS) {
            delay_ms = UI_MAX_DELAY_MS;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
