#include "ui_renderer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_events.h"
#include "audio/audio_player.h"
#include "config.h"
#include "esp_event.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lcd/lcd_driver.h"
#include "lvgl.h"
#include "net/ntp_client.h"
#include "net/weather_client.h"
#include "net/wifi_manager.h"
#include "power/battery_monitor.h"
#include "touch_gt911.h"
#include "ui/ui_font_manager.h"
#include "ui/ui_music_player.h"

#define LVGL_TICK_PERIOD_MS 5
#define UI_MIN_DELAY_MS 10
#define UI_MAX_DELAY_MS 100
#define UI_STATUS_UPDATE_MS 5000
#define UI_CLOCK_UPDATE_MS 1000
#define UI_MUSIC_UPDATE_MS 1000
#define UI_EVENT_QUEUE_LEN 16
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
    lv_obj_t *network_icon_label;
    lv_obj_t *network_value_label;
    lv_obj_t *battery_icon_label;
    lv_obj_t *battery_label;
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_obj_t *clock_hour_hand;
    lv_obj_t *clock_minute_hand;
    lv_obj_t *clock_second_hand;
    lv_obj_t *weather_title;
    lv_obj_t *weather_temp;
    lv_obj_t *weather_cond;
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
    char network_icon_text[16];
    char network_value_text[12];
    char battery_icon_text[16];
    char battery_text[24];
    char time_text[16];
    char date_text[40];
    char weather_title_text[48];
    char weather_temp_text[16];
    char weather_cond_text[32];
    char detail_humidity_text[24];
    char detail_feels_text[24];
    char detail_wind_text[24];
    char detail_vis_text[24];
    char forecast_date_text[3][16];
    char forecast_cond_text[3][24];
    char forecast_temp_text[3][16];
} ui_state_t;

typedef enum {
    UI_MSG_WIFI = 1,
    UI_MSG_FONT,
    UI_MSG_NTP,
    UI_MSG_WEATHER,
} ui_msg_type_t;

typedef struct {
    ui_msg_type_t type;
    union {
        app_wifi_event_t wifi;
        app_font_event_t font;
        app_ntp_event_t ntp;
        app_weather_event_t weather;
    } data;
} ui_msg_t;

static lv_display_t *s_display = NULL;
static esp_timer_handle_t s_lvgl_tick_timer = NULL;
static QueueHandle_t s_ui_event_queue = NULL;
static ui_state_t s_ui = {0};
static app_wifi_event_t s_latest_wifi = {0};
static weather_data_t s_latest_weather = {0};
static app_weather_state_t s_latest_weather_state = APP_WEATHER_STATE_WAITING;
static uint32_t s_flush_count = 0;
static volatile TaskHandle_t s_flush_wait_task = NULL;
static bool s_time_ready = false;
static bool s_ui_ready = false;
static lv_indev_t *s_touch_indev = NULL;
static lv_point_t s_last_touch_point = {0, 0};

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

static int wifi_signal_percent(int8_t rssi)
{
    if (rssi <= -100) {
        return 0;
    }

    if (rssi >= -50) {
        return 100;
    }

    return (int)((rssi + 100) * 2);
}

static int wifi_signal_level(int8_t rssi)
{
    if (rssi >= -60) {
        return 3;
    }
    if (rssi >= -70) {
        return 2;
    }
    if (rssi >= -80) {
        return 1;
    }
    return 0;
}

static lv_color_t wifi_indicator_color(const app_wifi_event_t *wifi)
{
    if (!wifi) {
        return lv_color_hex(0x6C7A88);
    }

    if (wifi->connected) {
        switch (wifi_signal_level(wifi->rssi)) {
        case 3:
            return lv_color_hex(0x68C1FF);
        case 2:
            return lv_color_hex(0x8FDDB9);
        case 1:
            return lv_color_hex(0xF8C05A);
        default:
            return lv_color_hex(0x9FB0C3);
        }
    }

    if (wifi->configuring || wifi->state == WIFI_STATE_CONNECTING) {
        return lv_color_hex(0xF8C05A);
    }

    return lv_color_hex(0x6C7A88);
}

static const char *battery_icon_for_level(uint8_t level_percent, bool charging)
{
    if (charging) {
        return LV_SYMBOL_CHARGE;
    }
    if (level_percent >= 90U) {
        return LV_SYMBOL_BATTERY_FULL;
    }
    if (level_percent >= 65U) {
        return LV_SYMBOL_BATTERY_3;
    }
    if (level_percent >= 40U) {
        return LV_SYMBOL_BATTERY_2;
    }
    if (level_percent >= 15U) {
        return LV_SYMBOL_BATTERY_1;
    }
    return LV_SYMBOL_BATTERY_EMPTY;
}

static lv_color_t battery_indicator_color(const battery_status_t *battery)
{
    if (!battery || !battery->available) {
        return lv_color_hex(0x6C7A88);
    }

    if (battery->charging) {
        return lv_color_hex(0x68C1FF);
    }
    if (battery->level_percent <= 10U) {
        return lv_color_hex(0xFF7D7D);
    }
    if (battery->level_percent <= 25U) {
        return lv_color_hex(0xF8C05A);
    }
    return lv_color_hex(0xB9C7D8);
}

static const char *weekday_cn(int wday)
{
    static const char *weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

    if (wday < 0 || wday > 6) {
        return "--";
    }
    return weekdays[wday];
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

static void capture_current_wifi_state(app_wifi_event_t *wifi)
{
    if (!wifi) {
        return;
    }

    memset(wifi, 0, sizeof(*wifi));
    wifi->state = wifi_manager_get_state();
    wifi->connected = wifi->state == WIFI_STATE_CONNECTED;
    wifi->configuring = wifi->state == WIFI_STATE_CONFIGURING;
    wifi->has_credentials = wifi_manager_has_credentials();
    wifi->rssi = wifi->connected ? wifi_manager_get_rssi() : 0;
    wifi_manager_get_ip(wifi->ip, sizeof(wifi->ip));
    wifi_manager_get_saved_ssid(wifi->ssid, sizeof(wifi->ssid));
    wifi_manager_get_ap_ssid(wifi->ap_ssid, sizeof(wifi->ap_ssid));
    if (wifi->configuring && wifi->ip[0] == '\0') {
        strlcpy(wifi->ip, WIFI_PROV_AP_IP, sizeof(wifi->ip));
    }
}

static void ui_enqueue_msg(const ui_msg_t *msg)
{
    if (!s_ui_event_queue || !msg) {
        return;
    }

    if (xQueueSend(s_ui_event_queue, msg, 0) != pdTRUE) {
        ESP_LOGW(TAG_UI, "UI event queue full, drop message type=%d", (int)msg->type);
    }
}

static void ui_app_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    ui_msg_t msg = {0};

    (void)arg;

    if (base != APP_EVENTS || !s_ui_event_queue) {
        return;
    }

    switch (id) {
    case APP_EVENT_WIFI_STATE_CHANGED:
        msg.type = UI_MSG_WIFI;
        if (event_data) {
            msg.data.wifi = *(const app_wifi_event_t *)event_data;
        }
        ui_enqueue_msg(&msg);
        break;
    case APP_EVENT_FONT_STATE_CHANGED:
        msg.type = UI_MSG_FONT;
        if (event_data) {
            msg.data.font = *(const app_font_event_t *)event_data;
        }
        ui_enqueue_msg(&msg);
        break;
    case APP_EVENT_NTP_STATE_CHANGED:
        msg.type = UI_MSG_NTP;
        if (event_data) {
            msg.data.ntp = *(const app_ntp_event_t *)event_data;
        }
        ui_enqueue_msg(&msg);
        break;
    case APP_EVENT_WEATHER_STATE_CHANGED:
        msg.type = UI_MSG_WEATHER;
        if (event_data) {
            msg.data.weather = *(const app_weather_event_t *)event_data;
        }
        ui_enqueue_msg(&msg);
        break;
    default:
        break;
    }
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

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x = 0;
    uint16_t y = 0;
    bool pressed = false;

    LV_UNUSED(indev);

    if (touch_gt911_read(&x, &y, &pressed) == ESP_OK && pressed) {
        s_last_touch_point.x = x;
        s_last_touch_point.y = y;
        data->point = s_last_touch_point;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->point = s_last_touch_point;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void ui_init_touch_input(void)
{
    esp_err_t ret = touch_gt911_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_UI, "Touch init failed, continue without touch: %s", esp_err_to_name(ret));
        return;
    }

    s_touch_indev = lv_indev_create();
    lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch_indev, lvgl_touch_read_cb);
    lv_indev_set_display(s_touch_indev, s_display);
    ESP_LOGI(TAG_UI, "GT911 touch input registered");
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
    lv_obj_set_style_text_font(obj, ui_font_manager_get_font_16(), 0);
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

static void format_forecast_date(const char *date_text, char *out, size_t out_size)
{
    int year = 0;
    int month = 0;
    int day = 0;

    if (!date_text || sscanf(date_text, "%d-%d-%d", &year, &month, &day) != 3) {
        strlcpy(out, "--", out_size);
        return;
    }

    struct tm tm_date = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_isdst = -1,
    };

    if (mktime(&tm_date) == (time_t)-1) {
        snprintf(out, out_size, "%02d-%02d", month, day);
        return;
    }

    strlcpy(out, weekday_cn(tm_date.tm_wday), out_size);
}

static void create_forecast_card(lv_obj_t *parent, int index, int x)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 60, 92);
    lv_obj_set_pos(card, x, 248);
    style_chip(card, lv_color_hex(0x15273A));

    s_ui.forecast_date[index] = lv_label_create(card);
    lv_obj_set_size(s_ui.forecast_date[index], LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_pos(s_ui.forecast_date[index], 0, 10);
    style_small_cjk_label(s_ui.forecast_date[index], lv_color_hex(0x91B7DC));
    lv_obj_set_style_text_align(s_ui.forecast_date[index], LV_TEXT_ALIGN_CENTER, 0);

    s_ui.forecast_cond[index] = lv_label_create(card);
    lv_obj_set_size(s_ui.forecast_cond[index], LV_PCT(100), 20);
    lv_obj_set_pos(s_ui.forecast_cond[index], 0, 36);
    style_small_cjk_label(s_ui.forecast_cond[index], lv_color_hex(0xF4F7FB));
    lv_obj_set_style_text_align(s_ui.forecast_cond[index], LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_ui.forecast_cond[index], LV_LABEL_LONG_CLIP);

    s_ui.forecast_temp[index] = lv_label_create(card);
    lv_obj_set_size(s_ui.forecast_temp[index], LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_pos(s_ui.forecast_temp[index], 0, 62);
    lv_obj_set_style_text_font(s_ui.forecast_temp[index], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.forecast_temp[index], lv_color_hex(0x7CD6FF), 0);
    lv_obj_set_style_text_align(s_ui.forecast_temp[index], LV_TEXT_ALIGN_CENTER, 0);
}

static void create_detail_card(lv_obj_t *parent, lv_obj_t **label, int x, int y)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 96, 32);
    lv_obj_set_pos(card, x, y);
    style_chip(card, lv_color_hex(0x111F2D));

    *label = lv_label_create(card);
    lv_obj_set_size(*label, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(*label, LV_ALIGN_CENTER, 0, 0);
    style_small_cjk_label(*label, lv_color_hex(0xD5DFEB));
    lv_obj_set_style_text_align(*label, LV_TEXT_ALIGN_CENTER, 0);
}

static void create_clock_visual(lv_obj_t *parent)
{
    const int face_size = 170;
    const int center = face_size / 2;
    const int orbit = 66;

    lv_obj_t *face = lv_obj_create(parent);
    lv_obj_set_size(face, face_size, face_size);
    lv_obj_set_pos(face, 38, 30);
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
    lv_obj_set_size(s_ui.clock_hour_hand, 6, 40);
    lv_obj_set_pos(s_ui.clock_hour_hand, center - 3, center - 40);
    lv_obj_set_style_radius(s_ui.clock_hour_hand, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ui.clock_hour_hand, 0, 0);
    lv_obj_set_style_bg_color(s_ui.clock_hour_hand, lv_color_hex(0xF7F9FD), 0);
    lv_obj_set_style_bg_opa(s_ui.clock_hour_hand, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_pivot_x(s_ui.clock_hour_hand, 3, 0);
    lv_obj_set_style_transform_pivot_y(s_ui.clock_hour_hand, 40, 0);

    s_ui.clock_minute_hand = lv_obj_create(face);
    lv_obj_set_size(s_ui.clock_minute_hand, 4, 58);
    lv_obj_set_pos(s_ui.clock_minute_hand, center - 2, center - 58);
    lv_obj_set_style_radius(s_ui.clock_minute_hand, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ui.clock_minute_hand, 0, 0);
    lv_obj_set_style_bg_color(s_ui.clock_minute_hand, lv_color_hex(0x7CD6FF), 0);
    lv_obj_set_style_bg_opa(s_ui.clock_minute_hand, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_pivot_x(s_ui.clock_minute_hand, 2, 0);
    lv_obj_set_style_transform_pivot_y(s_ui.clock_minute_hand, 58, 0);

    s_ui.clock_second_hand = lv_obj_create(face);
    lv_obj_set_size(s_ui.clock_second_hand, 2, 66);
    lv_obj_set_pos(s_ui.clock_second_hand, center - 1, center - 66);
    lv_obj_set_style_radius(s_ui.clock_second_hand, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ui.clock_second_hand, 0, 0);
    lv_obj_set_style_bg_color(s_ui.clock_second_hand, lv_color_hex(0xFFB057), 0);
    lv_obj_set_style_bg_opa(s_ui.clock_second_hand, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_pivot_x(s_ui.clock_second_hand, 1, 0);
    lv_obj_set_style_transform_pivot_y(s_ui.clock_second_hand, 66, 0);

    lv_obj_t *center_dot = lv_obj_create(face);
    lv_obj_set_size(center_dot, 14, 14);
    lv_obj_set_pos(center_dot, center - 7, center - 7);
    lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(center_dot, 0, 0);
    lv_obj_set_style_bg_color(center_dot, lv_color_hex(0xFFB057), 0);
    lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, 0);
}

static void create_weather_visual(lv_obj_t *parent)
{
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_set_size(wrap, 92, 92);
    lv_obj_set_pos(wrap, 112, 10);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);

    s_ui.weather_sun = lv_obj_create(wrap);
    lv_obj_set_size(s_ui.weather_sun, 28, 28);
    lv_obj_set_pos(s_ui.weather_sun, 50, 8);
    lv_obj_set_style_radius(s_ui.weather_sun, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ui.weather_sun, 0, 0);
    lv_obj_set_style_bg_color(s_ui.weather_sun, lv_color_hex(0xFFBF52), 0);
    lv_obj_set_style_bg_opa(s_ui.weather_sun, LV_OPA_COVER, 0);

    const int ray_pos[4][4] = {
        {62, 0, 4, 10},
        {62, 38, 4, 10},
        {40, 20, 10, 4},
        {74, 20, 10, 4},
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
        {10, 34, 24, 24},
        {28, 26, 32, 32},
        {52, 32, 22, 22},
        {10, 48, 66, 20},
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
        lv_obj_set_size(s_ui.weather_rain[i], 4, 14);
        lv_obj_set_pos(s_ui.weather_rain[i], 22 + (i * 16), 68 + (i % 2) * 4);
        lv_obj_set_style_radius(s_ui.weather_rain[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_ui.weather_rain[i], 0, 0);
        lv_obj_set_style_bg_color(s_ui.weather_rain[i], lv_color_hex(0x5BC0FF), 0);
        lv_obj_set_style_bg_opa(s_ui.weather_rain[i], LV_OPA_COVER, 0);

        s_ui.weather_snow[i] = lv_obj_create(wrap);
        lv_obj_set_size(s_ui.weather_snow[i], 6, 6);
        lv_obj_set_pos(s_ui.weather_snow[i], 20 + (i * 16), 72 + (i % 2) * 4);
        lv_obj_set_style_radius(s_ui.weather_snow[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_ui.weather_snow[i], 0, 0);
        lv_obj_set_style_bg_color(s_ui.weather_snow[i], lv_color_hex(0xF7FBFF), 0);
        lv_obj_set_style_bg_opa(s_ui.weather_snow[i], LV_OPA_COVER, 0);

        s_ui.weather_haze[i] = lv_obj_create(wrap);
        lv_obj_set_size(s_ui.weather_haze[i], 38, 4);
        lv_obj_set_pos(s_ui.weather_haze[i], 22 - (i % 2) * 6, 68 + i * 10);
        lv_obj_set_style_radius(s_ui.weather_haze[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_ui.weather_haze[i], 0, 0);
        lv_obj_set_style_bg_color(s_ui.weather_haze[i], lv_color_hex(0xAAB9C8), 0);
        lv_obj_set_style_bg_opa(s_ui.weather_haze[i], LV_OPA_70, 0);
    }

    set_weather_icon_mode(WEATHER_ICON_PARTLY_CLOUDY);
}

static void create_ui(void)
{
    const lv_coord_t content_x = 16;
    const lv_coord_t content_y = 66;
    const lv_coord_t panel_h = 398;
    const lv_coord_t panel_gap = 12;
    const lv_coord_t clock_panel_w = 280;
    const lv_coord_t weather_panel_w = 240;
    const lv_coord_t music_panel_w = 240;

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
    lv_obj_set_pos(glow_right, 606, 278);
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

    s_ui.status_label = lv_label_create(status_bar);
    lv_obj_set_width(s_ui.status_label, 560);
    lv_obj_set_style_text_font(s_ui.status_label, ui_font_manager_get_font_16(), 0);
    lv_obj_set_style_text_color(s_ui.status_label, lv_color_hex(0xB9C7D8), 0);
    lv_label_set_long_mode(s_ui.status_label, LV_LABEL_LONG_CLIP);
    lv_obj_align(s_ui.status_label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *status_metrics = lv_obj_create(status_bar);
    lv_obj_remove_style_all(status_metrics);
    lv_obj_clear_flag(status_metrics, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(status_metrics, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(status_metrics, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_metrics, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(status_metrics, 0, 0);
    lv_obj_set_style_pad_column(status_metrics, 6, 0);
    lv_obj_set_size(status_metrics, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(status_metrics, LV_ALIGN_RIGHT_MID, 0, 0);

    s_ui.network_icon_label = lv_label_create(status_metrics);
    lv_obj_set_style_text_font(s_ui.network_icon_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(s_ui.network_icon_label, lv_color_hex(0x6C7A88), 0);

    s_ui.network_value_label = lv_label_create(status_metrics);
    lv_obj_set_style_text_font(s_ui.network_value_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(s_ui.network_value_label, lv_color_hex(0x6C7A88), 0);

    s_ui.battery_icon_label = lv_label_create(status_metrics);
    lv_obj_set_style_text_font(s_ui.battery_icon_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(s_ui.battery_icon_label, lv_color_hex(0x6C7A88), 0);

    s_ui.battery_label = lv_label_create(status_metrics);
    lv_obj_set_style_text_font(s_ui.battery_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(s_ui.battery_label, lv_color_hex(0x6C7A88), 0);

    lv_obj_t *left_panel = lv_obj_create(screen);
    lv_obj_set_size(left_panel, clock_panel_w, panel_h);
    lv_obj_set_pos(left_panel, content_x, content_y);
    style_panel(left_panel);

    lv_obj_t *clock_title = lv_label_create(left_panel);
    lv_obj_set_pos(clock_title, 0, 0);
    style_small_cjk_label(clock_title, lv_color_hex(0x68C1FF));
    lv_label_set_text(clock_title, "时间");

    s_ui.time_label = lv_label_create(left_panel);
    lv_obj_set_width(s_ui.time_label, LV_PCT(100));
    lv_obj_set_style_text_font(s_ui.time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_ui.time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_ui.time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_ui.time_label, 0, 224);

    s_ui.date_label = lv_label_create(left_panel);
    lv_obj_set_pos(s_ui.date_label, 0, 284);
    lv_obj_set_width(s_ui.date_label, LV_PCT(100));
    style_small_cjk_label(s_ui.date_label, lv_color_hex(0xA7B5C8));
    lv_obj_set_style_text_align(s_ui.date_label, LV_TEXT_ALIGN_CENTER, 0);

    create_clock_visual(left_panel);

    lv_obj_t *weather_panel = lv_obj_create(screen);
    lv_obj_set_size(weather_panel, weather_panel_w, panel_h);
    lv_obj_set_pos(weather_panel, content_x + clock_panel_w + panel_gap, content_y);
    style_panel(weather_panel);

    lv_obj_t *weather_head = lv_label_create(weather_panel);
    lv_obj_set_pos(weather_head, 0, 0);
    style_small_cjk_label(weather_head, lv_color_hex(0x68C1FF));
    lv_label_set_text(weather_head, "天气");

    s_ui.weather_title = lv_label_create(weather_panel);
    lv_obj_set_pos(s_ui.weather_title, 0, 26);
    lv_obj_set_width(s_ui.weather_title, 104);
    style_small_cjk_label(s_ui.weather_title, lv_color_hex(0xFFFFFF));

    s_ui.weather_temp = lv_label_create(weather_panel);
    lv_obj_set_pos(s_ui.weather_temp, 0, 56);
    lv_obj_set_style_text_font(s_ui.weather_temp, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_ui.weather_temp, lv_color_hex(0xFFFFFF), 0);

    s_ui.weather_cond = lv_label_create(weather_panel);
    lv_obj_set_pos(s_ui.weather_cond, 0, 102);
    lv_obj_set_width(s_ui.weather_cond, 104);
    style_small_cjk_label(s_ui.weather_cond, lv_color_hex(0x68C1FF));

    create_weather_visual(weather_panel);

    lv_obj_t *forecast_title = lv_label_create(weather_panel);
    lv_obj_set_pos(forecast_title, 0, 220);
    style_small_cjk_label(forecast_title, lv_color_hex(0x68C1FF));
    lv_label_set_text(forecast_title, "3日天气");

    create_detail_card(weather_panel, &s_ui.detail_humidity, 0, 132);
    create_detail_card(weather_panel, &s_ui.detail_feels, 108, 132);
    create_detail_card(weather_panel, &s_ui.detail_wind, 0, 172);
    create_detail_card(weather_panel, &s_ui.detail_vis, 108, 172);

    create_forecast_card(weather_panel, 0, 0);
    create_forecast_card(weather_panel, 1, 72);
    create_forecast_card(weather_panel, 2, 144);

    set_label_text_if_changed(s_ui.weather_title, s_ui.weather_title_text, sizeof(s_ui.weather_title_text), "天气");

    ui_music_player_create(screen,
                           content_x + clock_panel_w + weather_panel_w + (panel_gap * 2),
                           content_y,
                           music_panel_w,
                           panel_h);
}

static void update_status_text(void)
{
    char new_status[sizeof(s_ui.status_text)];
    char new_network_icon[sizeof(s_ui.network_icon_text)];
    char new_network_value[sizeof(s_ui.network_value_text)];
    char new_battery_icon[sizeof(s_ui.battery_icon_text)];
    char new_battery[sizeof(s_ui.battery_text)];
    battery_status_t battery = {0};
    bool battery_ready = battery_monitor_get_status(&battery);
    lv_color_t network_color = wifi_indicator_color(&s_latest_wifi);
    lv_color_t battery_color = lv_color_hex(0x6C7A88);

    if (s_latest_wifi.connected) {
        if (s_latest_wifi.ip[0]) {
            snprintf(new_status, sizeof(new_status), "WiFi 已连接  %s", s_latest_wifi.ip);
        } else {
            snprintf(new_status, sizeof(new_status), "WiFi 已连接");
        }
    } else if (s_latest_wifi.configuring) {
        snprintf(new_status, sizeof(new_status), "WiFi AP  %s",
                 s_latest_wifi.ip[0] ? s_latest_wifi.ip : WIFI_PROV_AP_IP);
    } else if (s_latest_wifi.state == WIFI_STATE_CONNECTING) {
        snprintf(new_status, sizeof(new_status), "WiFi 连接中...");
    } else if (s_latest_wifi.has_credentials) {
        snprintf(new_status, sizeof(new_status), "WiFi 已断开");
    } else {
        snprintf(new_status, sizeof(new_status), "WiFi 待配置");
    }

    strlcpy(new_network_icon, LV_SYMBOL_WIFI, sizeof(new_network_icon));
    if (s_latest_wifi.connected && s_latest_wifi.rssi != 0) {
        snprintf(new_network_value, sizeof(new_network_value), "%d%%",
                 wifi_signal_percent(s_latest_wifi.rssi));
    } else if (s_latest_wifi.configuring) {
        strlcpy(new_network_value, "AP", sizeof(new_network_value));
    } else if (s_latest_wifi.state == WIFI_STATE_CONNECTING) {
        strlcpy(new_network_value, "...", sizeof(new_network_value));
    } else {
        strlcpy(new_network_value, "--", sizeof(new_network_value));
    }

    if (battery_ready && battery.available) {
        strlcpy(new_battery_icon,
                battery_icon_for_level(battery.level_percent, battery.charging),
                sizeof(new_battery_icon));
        snprintf(new_battery, sizeof(new_battery), "%u%%", battery.level_percent);
        battery_color = battery_indicator_color(&battery);
    } else {
        strlcpy(new_battery_icon, LV_SYMBOL_BATTERY_EMPTY, sizeof(new_battery_icon));
        strlcpy(new_battery, "--", sizeof(new_battery));
    }

    set_label_text_if_changed(s_ui.status_label, s_ui.status_text, sizeof(s_ui.status_text), new_status);
    set_label_text_if_changed(s_ui.network_icon_label, s_ui.network_icon_text, sizeof(s_ui.network_icon_text), new_network_icon);
    set_label_text_if_changed(s_ui.network_value_label, s_ui.network_value_text, sizeof(s_ui.network_value_text), new_network_value);
    set_label_text_if_changed(s_ui.battery_icon_label, s_ui.battery_icon_text, sizeof(s_ui.battery_icon_text), new_battery_icon);
    set_label_text_if_changed(s_ui.battery_label, s_ui.battery_text, sizeof(s_ui.battery_text), new_battery);

    if (s_ui.network_icon_label) {
        lv_obj_set_style_text_color(s_ui.network_icon_label, network_color, 0);
    }
    if (s_ui.network_value_label) {
        lv_obj_set_style_text_color(s_ui.network_value_label, network_color, 0);
    }
    if (s_ui.battery_icon_label) {
        lv_obj_set_style_text_color(s_ui.battery_icon_label, battery_color, 0);
    }
    if (s_ui.battery_label) {
        lv_obj_set_style_text_color(s_ui.battery_label, battery_color, 0);
    }
}

static void update_clock_text(void)
{
    struct tm now;
    char new_time[sizeof(s_ui.time_text)];
    char new_date[sizeof(s_ui.date_text)];

    if (!(s_time_ready || ntp_client_has_valid_time())) {
        strlcpy(new_time, "--:--", sizeof(new_time));
        strlcpy(new_date, "等待 NTP 同步", sizeof(new_date));
        set_label_text_if_changed(s_ui.time_label, s_ui.time_text, sizeof(s_ui.time_text), new_time);
        set_label_text_if_changed(s_ui.date_label, s_ui.date_text, sizeof(s_ui.date_text), new_date);
        return;
    }

    ntp_client_get_local_time(&now);

    snprintf(new_time, sizeof(new_time), "%02d:%02d", now.tm_hour, now.tm_min);
    snprintf(new_date, sizeof(new_date), "%04d年%02d月%02d日 %s",
             now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, weekday_cn(now.tm_wday));

    set_label_text_if_changed(s_ui.time_label, s_ui.time_text, sizeof(s_ui.time_text), new_time);
    set_label_text_if_changed(s_ui.date_label, s_ui.date_text, sizeof(s_ui.date_text), new_date);
    update_clock_visual(&now);
}

static void update_weather_text(void)
{
    char weather_title_text[sizeof(s_ui.weather_title_text)];
    char weather_temp_text[sizeof(s_ui.weather_temp_text)];
    char weather_cond_text[sizeof(s_ui.weather_cond_text)];
    char detail_humidity_text[sizeof(s_ui.detail_humidity_text)];
    char detail_feels_text[sizeof(s_ui.detail_feels_text)];
    char detail_wind_text[sizeof(s_ui.detail_wind_text)];
    char detail_vis_text[sizeof(s_ui.detail_vis_text)];
    char forecast_date_text[3][16] = {{0}};
    char forecast_cond_text[3][24] = {{0}};
    char forecast_temp_text[3][16] = {{0}};
    const weather_data_t *weather = &s_latest_weather;

    if (!weather->valid) {
        strlcpy(weather_title_text, "天气", sizeof(weather_title_text));
        strlcpy(weather_temp_text, "--", sizeof(weather_temp_text));
        if (!s_latest_wifi.connected) {
            strlcpy(weather_cond_text,
                    s_latest_wifi.configuring ? "等待联网..." : "网络未连接",
                    sizeof(weather_cond_text));
        } else if (!(s_time_ready || ntp_client_has_valid_time())) {
            strlcpy(weather_cond_text, "等待授时...", sizeof(weather_cond_text));
        } else if (s_latest_weather_state == APP_WEATHER_STATE_FETCHING) {
            strlcpy(weather_cond_text, "更新中...", sizeof(weather_cond_text));
        } else if (s_latest_weather_state == APP_WEATHER_STATE_FAILED) {
            strlcpy(weather_cond_text, "获取失败", sizeof(weather_cond_text));
        } else {
            strlcpy(weather_cond_text, "暂无数据", sizeof(weather_cond_text));
        }
        strlcpy(detail_humidity_text, "湿度 --", sizeof(detail_humidity_text));
        strlcpy(detail_feels_text, "体感 --", sizeof(detail_feels_text));
        strlcpy(detail_wind_text, "风速 --", sizeof(detail_wind_text));
        strlcpy(detail_vis_text, "能见 --", sizeof(detail_vis_text));
        for (int i = 0; i < 3; i++) {
            snprintf(forecast_date_text[i], sizeof(forecast_date_text[i]), "第%d天", i + 1);
            strlcpy(forecast_cond_text[i], "--", sizeof(forecast_cond_text[i]));
            strlcpy(forecast_temp_text[i], "--", sizeof(forecast_temp_text[i]));
        }
        set_weather_icon_mode(WEATHER_ICON_PARTLY_CLOUDY);
    } else {
        strlcpy(weather_title_text, "当前天气", sizeof(weather_title_text));

        snprintf(weather_temp_text, sizeof(weather_temp_text), "%d%sC", weather->temp_now, UI_DEGREE_SYMBOL);
        snprintf(weather_cond_text, sizeof(weather_cond_text), "%s", weather_condition_text(weather->icon_code));
        snprintf(detail_humidity_text, sizeof(detail_humidity_text), "湿度 %u%%", weather->humidity);
        snprintf(detail_feels_text, sizeof(detail_feels_text), "体感 %d度", weather->temp_feels);
        snprintf(detail_wind_text, sizeof(detail_wind_text), "风速 %ukm/h", weather->wind_speed);
        snprintf(detail_vis_text, sizeof(detail_vis_text), "能见 %ukm", weather->visibility);

        for (int i = 0; i < 3; i++) {
            format_forecast_date(weather->forecast[i].date, forecast_date_text[i], sizeof(forecast_date_text[i]));
            snprintf(forecast_cond_text[i], sizeof(forecast_cond_text[i]), "%s",
                     weather_condition_text(weather->forecast[i].icon_day));
            snprintf(forecast_temp_text[i], sizeof(forecast_temp_text[i]), "%d/%d%sC",
                     weather->forecast[i].high, weather->forecast[i].low, UI_DEGREE_SYMBOL);
        }
        set_weather_icon_mode(weather_icon_mode_from_code(weather->icon_code));
    }

    set_label_text_if_changed(s_ui.weather_title, s_ui.weather_title_text, sizeof(s_ui.weather_title_text), weather_title_text);
    set_label_text_if_changed(s_ui.weather_temp, s_ui.weather_temp_text, sizeof(s_ui.weather_temp_text), weather_temp_text);
    set_label_text_if_changed(s_ui.weather_cond, s_ui.weather_cond_text, sizeof(s_ui.weather_cond_text), weather_cond_text);
    set_label_text_if_changed(s_ui.detail_humidity, s_ui.detail_humidity_text, sizeof(s_ui.detail_humidity_text), detail_humidity_text);
    set_label_text_if_changed(s_ui.detail_feels, s_ui.detail_feels_text, sizeof(s_ui.detail_feels_text), detail_feels_text);
    set_label_text_if_changed(s_ui.detail_wind, s_ui.detail_wind_text, sizeof(s_ui.detail_wind_text), detail_wind_text);
    set_label_text_if_changed(s_ui.detail_vis, s_ui.detail_vis_text, sizeof(s_ui.detail_vis_text), detail_vis_text);

    for (int i = 0; i < 3; i++) {
        set_label_text_if_changed(s_ui.forecast_date[i], s_ui.forecast_date_text[i], sizeof(s_ui.forecast_date_text[i]), forecast_date_text[i]);
        set_label_text_if_changed(s_ui.forecast_cond[i], s_ui.forecast_cond_text[i], sizeof(s_ui.forecast_cond_text[i]), forecast_cond_text[i]);
        set_label_text_if_changed(s_ui.forecast_temp[i], s_ui.forecast_temp_text[i], sizeof(s_ui.forecast_temp_text[i]), forecast_temp_text[i]);
    }
}

static void update_all_ui_text(void)
{
    update_status_text();
    update_clock_text();
    update_weather_text();
}

static void refresh_runtime_fonts(void)
{
    const lv_font_t *font = ui_font_manager_get_font_16();

    if (s_ui.status_label) {
        lv_obj_set_style_text_font(s_ui.status_label, font, 0);
    }
    if (s_ui.date_label) {
        lv_obj_set_style_text_font(s_ui.date_label, font, 0);
    }
    if (s_ui.weather_title) {
        lv_obj_set_style_text_font(s_ui.weather_title, font, 0);
    }
    if (s_ui.weather_cond) {
        lv_obj_set_style_text_font(s_ui.weather_cond, font, 0);
    }
    if (s_ui.detail_humidity) {
        lv_obj_set_style_text_font(s_ui.detail_humidity, font, 0);
    }
    if (s_ui.detail_feels) {
        lv_obj_set_style_text_font(s_ui.detail_feels, font, 0);
    }
    if (s_ui.detail_wind) {
        lv_obj_set_style_text_font(s_ui.detail_wind, font, 0);
    }
    if (s_ui.detail_vis) {
        lv_obj_set_style_text_font(s_ui.detail_vis, font, 0);
    }
    for (int i = 0; i < 3; i++) {
        if (s_ui.forecast_date[i]) {
            lv_obj_set_style_text_font(s_ui.forecast_date[i], font, 0);
        }
        if (s_ui.forecast_cond[i]) {
            lv_obj_set_style_text_font(s_ui.forecast_cond[i], font, 0);
        }
    }

    ui_music_player_refresh_fonts();
}

static void process_ui_msg(const ui_msg_t *msg)
{
    if (!msg) {
        return;
    }

    switch (msg->type) {
    case UI_MSG_WIFI:
        s_latest_wifi = msg->data.wifi;
        update_status_text();
        break;
    case UI_MSG_FONT:
        if (msg->data.font.state == APP_FONT_STATE_READY && msg->data.font.external_loaded) {
            refresh_runtime_fonts();
            update_all_ui_text();
            ui_music_player_update();
            lv_obj_invalidate(lv_screen_active());
            ESP_LOGI(TAG_UI, "External font applied to runtime UI");
        } else if (msg->data.font.state == APP_FONT_STATE_FAILED) {
            ESP_LOGW(TAG_UI, "External font load failed: %s", esp_err_to_name(msg->data.font.err));
        }
        break;
    case UI_MSG_NTP:
        s_time_ready = msg->data.ntp.synced;
        update_clock_text();
        break;
    case UI_MSG_WEATHER:
        s_latest_weather_state = msg->data.weather.state;
        if (msg->data.weather.state == APP_WEATHER_STATE_UPDATED) {
            s_latest_weather = msg->data.weather.data;
        }
        update_weather_text();
        break;
    default:
        break;
    }
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

    ESP_LOGI(TAG_UI, "ui_renderer_init: lv_init");
    lv_init();

    ESP_LOGI(TAG_UI, "ui_renderer_init: display create");
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

    ui_init_touch_input();

    ESP_LOGI(TAG_UI, "ui_renderer_init: create ui");
    create_ui();
    if (battery_monitor_init() != ESP_OK) {
        ESP_LOGW(TAG_UI, "Battery monitor init skipped, status bar will show '--'");
    }
    capture_current_wifi_state(&s_latest_wifi);
    weather_client_get_data(&s_latest_weather);
    s_latest_weather_state = s_latest_weather.valid ? APP_WEATHER_STATE_UPDATED : APP_WEATHER_STATE_WAITING;
    s_time_ready = ntp_client_has_valid_time() || ntp_client_is_synced();
    ESP_LOGI(TAG_UI, "ui_renderer_init: populate text");
    update_all_ui_text();
    lv_obj_invalidate(lv_screen_active());
    ESP_LOGI(TAG_UI, "ui_renderer_init: first lv_timer_handler");
    (void)lv_timer_handler();

    ESP_LOGI(TAG_UI, "LVGL UI initialized panel=%p fb0=%p fb1=%p size=%u mode=%s",
             panel, fb0, fb1, (unsigned)fb_size,
             render_mode == LV_DISPLAY_RENDER_MODE_DIRECT ? "direct" : "partial");

    s_ui_ready = true;
    app_ui_event_t event = {
        .state = APP_UI_STATE_READY,
        .err = ESP_OK,
    };
    esp_err_t post_ret = app_events_post(APP_EVENT_UI_STATE_CHANGED, &event, sizeof(event));
    if (post_ret != ESP_OK && post_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG_UI, "Post UI ready event failed: %s", esp_err_to_name(post_ret));
    }
    return ESP_OK;
}

void ui_renderer_task(void *arg)
{
    int64_t last_status_update_us = 0;
    int64_t last_clock_update_us = 0;
    int64_t last_music_update_us = 0;
    ui_msg_t msg = {0};

    LV_UNUSED(arg);

    s_ui_ready = false;
    s_ui_event_queue = xQueueCreate(UI_EVENT_QUEUE_LEN, sizeof(ui_msg_t));
    if (!s_ui_event_queue) {
        ESP_LOGE(TAG_UI, "Create UI event queue failed");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_events_loop(), APP_EVENTS,
                                                    ESP_EVENT_ANY_ID,
                                                    ui_app_event_handler, NULL));

    esp_err_t ret = ui_renderer_init();
    if (ret != ESP_OK) {
        app_ui_event_t event = {
            .state = APP_UI_STATE_FAILED,
            .err = ret,
        };
        (void)app_events_post(APP_EVENT_UI_STATE_CHANGED, &event, sizeof(event));
        ESP_LOGE(TAG_UI, "UI init failed: %s", esp_err_to_name(ret));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    while (1) {
        int64_t now_us = esp_timer_get_time();
        uint32_t delay_ms;

        while (xQueueReceive(s_ui_event_queue, &msg, 0) == pdTRUE) {
            process_ui_msg(&msg);
        }

        if (last_status_update_us == 0 || now_us - last_status_update_us >= (int64_t)UI_STATUS_UPDATE_MS * 1000) {
            capture_current_wifi_state(&s_latest_wifi);
            update_status_text();
            last_status_update_us = now_us;
        }

        if (last_clock_update_us == 0 || now_us - last_clock_update_us >= (int64_t)UI_CLOCK_UPDATE_MS * 1000) {
            update_clock_text();
            last_clock_update_us = now_us;
        }

        if (last_music_update_us == 0 || now_us - last_music_update_us >= (int64_t)UI_MUSIC_UPDATE_MS * 1000) {
            ui_music_player_update();
            last_music_update_us = now_us;
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

bool ui_renderer_is_ready(void)
{
    return s_ui_ready;
}
