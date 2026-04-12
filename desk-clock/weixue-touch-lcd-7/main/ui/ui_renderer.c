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
#include "storage/media_library.h"
#include "touch_gt911.h"
#include "ui/ui_font_manager.h"
#include "ui/ui_music_player.h"

#define LVGL_TICK_PERIOD_MS      5
#define UI_MIN_DELAY_MS          10
#define UI_MAX_DELAY_MS          100
#define UI_STATUS_UPDATE_MS      5000
#define UI_CLOCK_UPDATE_MS       1000
#define UI_MUSIC_UPDATE_MS       1000
#define UI_PHOTO_ROTATE_MS       5000
#define UI_EVENT_QUEUE_LEN       16
#define UI_FLUSH_TIMEOUT_MS      1000
#define UI_SWIPE_MIN_X_PX        90
#define UI_SWIPE_MAX_Y_PX        80
#define UI_TAP_MAX_MOVE_PX       24
#define UI_DEGREE_SYMBOL         "\xC2\xB0"
#define UI_PAGE_COUNT            4
#define UI_CALENDAR_COLS         7
#define UI_CALENDAR_ROWS         6
#define UI_CALENDAR_CELL_COUNT   (UI_CALENDAR_COLS * UI_CALENDAR_ROWS)

typedef enum {
    WEATHER_ICON_SUNNY = 0,
    WEATHER_ICON_PARTLY_CLOUDY,
    WEATHER_ICON_CLOUDY,
    WEATHER_ICON_RAIN,
    WEATHER_ICON_SNOW,
    WEATHER_ICON_HAZE,
} weather_icon_mode_t;

typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_PHOTO,
    UI_PAGE_MUSIC,
    UI_PAGE_VIDEO,
} ui_page_t;

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

typedef struct {
    lv_obj_t *status_label;
    lv_obj_t *network_icon_label;
    lv_obj_t *network_value_label;
    lv_obj_t *battery_icon_label;
    lv_obj_t *battery_label;

    lv_obj_t *tileview;
    lv_obj_t *pages[UI_PAGE_COUNT];
    lv_obj_t *page_name_label;
    lv_obj_t *page_dots[UI_PAGE_COUNT];
    int active_page;

    lv_obj_t *calendar_title_label;
    lv_obj_t *calendar_weekday_labels[UI_CALENDAR_COLS];
    lv_obj_t *calendar_day_cells[UI_CALENDAR_CELL_COUNT];
    lv_obj_t *calendar_day_labels[UI_CALENDAR_CELL_COUNT];
    lv_obj_t *time_label;
    lv_obj_t *date_label;

    lv_obj_t *weather_title;
    lv_obj_t *weather_temp;
    lv_obj_t *weather_cond;
    lv_obj_t *detail_humidity;
    lv_obj_t *detail_feels;
    lv_obj_t *detail_wind;
    lv_obj_t *detail_vis;
    lv_obj_t *forecast_date[3];
    lv_obj_t *forecast_cond[3];
    lv_obj_t *forecast_temp[3];
    lv_obj_t *weather_sun;
    lv_obj_t *weather_rays[4];
    lv_obj_t *weather_cloud[4];
    lv_obj_t *weather_rain[3];
    lv_obj_t *weather_snow[3];
    lv_obj_t *weather_haze[3];

    lv_obj_t *photo_image;
    lv_obj_t *photo_placeholder_icon;
    lv_obj_t *photo_name_label;
    lv_obj_t *photo_meta_label;
    lv_obj_t *photo_status_label;

    lv_obj_t *music_list_wrap;
    lv_obj_t *music_empty_label;
    lv_obj_t *music_item_btn[AUDIO_MAX_PLAYLIST_SIZE];
    lv_obj_t *music_item_index_label[AUDIO_MAX_PLAYLIST_SIZE];
    lv_obj_t *music_item_title_label[AUDIO_MAX_PLAYLIST_SIZE];
    int music_item_count;

    lv_obj_t *video_preview_image;
    lv_obj_t *video_preview_placeholder_icon;
    lv_obj_t *video_name_label;
    lv_obj_t *video_status_label;
    lv_obj_t *video_list_wrap;
    lv_obj_t *video_empty_label;
    lv_obj_t *video_card_btn[MEDIA_LIBRARY_MAX_ITEMS];
    lv_obj_t *video_card_thumb_frame[MEDIA_LIBRARY_MAX_ITEMS];
    lv_obj_t *video_card_image[MEDIA_LIBRARY_MAX_ITEMS];
    lv_obj_t *video_card_placeholder[MEDIA_LIBRARY_MAX_ITEMS];
    lv_obj_t *video_card_title[MEDIA_LIBRARY_MAX_ITEMS];
    int video_card_count;

    int current_photo_index;
    int current_video_index;
    int rendered_photo_index;
    int rendered_photo_count;
    int rendered_video_index;
    int rendered_video_count;
    int rendered_music_track;
    int rendered_video_selection_index;
    int rendered_video_selection_count;
    int rendered_calendar_year;
    int rendered_calendar_month;
    int rendered_calendar_day;
    bool calendar_rendered_valid;
    bool status_colors_valid;
    lv_color_t rendered_network_color;
    lv_color_t rendered_battery_color;
    bool sd_media_loaded;

    char status_text[96];
    char network_icon_text[16];
    char network_value_text[12];
    char battery_icon_text[16];
    char battery_text[24];
    char page_title_text[24];
    char calendar_title_text[32];
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
    char photo_name_text[96];
    char photo_meta_text[48];
    char photo_status_text[96];
    char video_name_text[96];
    char video_status_text[96];
} ui_state_t;

static const char *s_page_names[UI_PAGE_COUNT] = {"月历", "照片", "音乐", "视频"};
static const char *s_calendar_headers[UI_CALENDAR_COLS] = {"一", "二", "三", "四", "五", "六", "日"};

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
static lv_point_t s_touch_start_point = {0, 0};
static lv_point_t s_touch_last_move_point = {0, 0};
static bool s_touch_pressed = false;
static bool s_touch_swipe_tracking = false;
static int s_pending_page_delta = 0;

static void update_photo_view(void);
static void update_video_preview(void);
static void update_video_card_selection(void);
static void update_page_indicator(void);
static bool point_in_obj(lv_obj_t *obj, const lv_point_t *point);
static void switch_to_page(int page, lv_anim_enable_t anim_en);
static void process_pending_swipe(void);

static bool set_label_text_if_changed(lv_obj_t *label, char *storage, size_t storage_size, const char *text)
{
    if (!label || !storage || !text) {
        return false;
    }

    if (strncmp(storage, text, storage_size) == 0) {
        return false;
    }

    strlcpy(storage, text, storage_size);
    lv_label_set_text(label, storage);
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

static int days_in_month(int year, int month)
{
    static const int days_per_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int days = days_per_month[month - 1];
    bool leap_year = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);

    if (month == 2 && leap_year) {
        days = 29;
    }
    return days;
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

static bool point_in_obj(lv_obj_t *obj, const lv_point_t *point)
{
    if (!obj || !point) {
        return false;
    }

    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    return point->x >= area.x1 && point->x <= area.x2 &&
           point->y >= area.y1 && point->y <= area.y2;
}

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x = 0;
    uint16_t y = 0;
    bool pressed = false;

    LV_UNUSED(indev);

    if (touch_gt911_read(&x, &y, &pressed) == ESP_OK && pressed) {
        if (s_ui.tileview) {
            lv_coord_t tile_y = lv_obj_get_y(s_ui.tileview);
            if (tile_y > 0 && y < (uint16_t)lv_obj_get_height(s_ui.tileview)) {
                uint32_t adjusted_y = (uint32_t)y + (uint32_t)tile_y;
                if (adjusted_y >= LCD_V_RES) {
                    adjusted_y = LCD_V_RES - 1;
                }
                y = (uint16_t)adjusted_y;
            }
        }

        s_last_touch_point.x = x;
        s_last_touch_point.y = y;
        data->point = s_last_touch_point;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->point = s_last_touch_point;
        data->state = LV_INDEV_STATE_RELEASED;
    }

    if (pressed) {
        if (!s_touch_pressed) {
            s_touch_pressed = true;
            s_touch_start_point = s_last_touch_point;
            s_touch_last_move_point = s_last_touch_point;
            s_touch_swipe_tracking = point_in_obj(s_ui.tileview, &s_last_touch_point);
        } else if (s_touch_swipe_tracking) {
            s_touch_last_move_point = s_last_touch_point;
        }
    } else if (s_touch_pressed) {
        int dx = s_touch_last_move_point.x - s_touch_start_point.x;
        int dy = s_touch_last_move_point.y - s_touch_start_point.y;
        int abs_dx = dx >= 0 ? dx : -dx;
        int abs_dy = dy >= 0 ? dy : -dy;

        if (s_touch_swipe_tracking && s_pending_page_delta == 0) {
            if (abs_dx >= UI_SWIPE_MIN_X_PX && abs_dx > abs_dy && abs_dy <= UI_SWIPE_MAX_Y_PX) {
                s_pending_page_delta = dx < 0 ? 1 : -1;
            }
        }

        if (s_pending_page_delta == 0 &&
            abs_dx <= UI_TAP_MAX_MOVE_PX &&
            abs_dy <= UI_TAP_MAX_MOVE_PX &&
            s_ui.active_page == UI_PAGE_MUSIC) {
            (void)ui_music_player_handle_touch_release(&s_last_touch_point);
        }

        s_touch_pressed = false;
        s_touch_swipe_tracking = false;
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
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 24, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x20384E), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x101A28), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x172A40), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 18, 0);
}

static void style_chip(lv_obj_t *obj, lv_color_t bg_color)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x25435C), 0);
    lv_obj_set_style_bg_color(obj, bg_color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

static void style_small_cjk_label(lv_obj_t *obj, lv_color_t color)
{
    lv_obj_set_style_text_font(obj, ui_font_manager_get_font_16(), 0);
    lv_obj_set_style_text_color(obj, color, 0);
}

static void style_section_title(lv_obj_t *obj)
{
    style_small_cjk_label(obj, lv_color_hex(0x68C1FF));
}

static void style_body_label(lv_obj_t *obj, lv_color_t color)
{
    style_small_cjk_label(obj, color);
}

static void style_thumb_frame(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x304E68), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x0C1724), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_clip_corner(obj, true, 0);
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

static void create_detail_card(lv_obj_t *parent, lv_obj_t **label, int x, int y)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 124, 34);
    lv_obj_set_pos(card, x, y);
    style_chip(card, lv_color_hex(0x111F2D));

    *label = lv_label_create(card);
    lv_obj_set_size(*label, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(*label, LV_ALIGN_CENTER, 0, 0);
    style_body_label(*label, lv_color_hex(0xD5DFEB));
    lv_obj_set_style_text_align(*label, LV_TEXT_ALIGN_CENTER, 0);
}

static void create_weather_visual(lv_obj_t *parent)
{
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_set_size(wrap, 92, 92);
    lv_obj_set_pos(wrap, 182, 6);
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

static void update_page_indicator(void)
{
    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        if (s_ui.page_dots[i]) {
            lv_obj_set_style_text_color(s_ui.page_dots[i],
                                        i == s_ui.active_page ? lv_color_hex(0x68C1FF) : lv_color_hex(0x38556D),
                                        0);
        }
    }

    set_label_text_if_changed(s_ui.page_name_label,
                              s_ui.page_title_text,
                              sizeof(s_ui.page_title_text),
                              s_page_names[s_ui.active_page]);
}

static void switch_to_page(int page, lv_anim_enable_t anim_en)
{
    LV_UNUSED(anim_en);

    if (!s_ui.tileview) {
        return;
    }

    if (page < 0) {
        page = 0;
    } else if (page >= UI_PAGE_COUNT) {
        page = UI_PAGE_COUNT - 1;
    }

    s_ui.active_page = page;
    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        set_obj_hidden(s_ui.pages[i], i != s_ui.active_page);
    }
    update_page_indicator();
}

static void process_pending_swipe(void)
{
    int delta = s_pending_page_delta;

    if (delta == 0) {
        return;
    }

    s_pending_page_delta = 0;
    switch_to_page(s_ui.active_page + delta, LV_ANIM_OFF);
}

static void show_image_or_placeholder(lv_obj_t *image,
                                      lv_obj_t *placeholder,
                                      const char *src,
                                      const char *fallback_symbol)
{
    if (src && src[0] != '\0') {
        lv_image_set_src(image, src);
        set_obj_hidden(image, false);
        set_obj_hidden(placeholder, true);
        return;
    }

    if (fallback_symbol) {
        lv_label_set_text(placeholder, fallback_symbol);
    }
    set_obj_hidden(image, true);
    set_obj_hidden(placeholder, false);
}

static void photo_nav_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    intptr_t delta = (intptr_t)lv_event_get_user_data(e);
    int count = media_library_get_photo_count();
    if (count <= 0) {
        return;
    }

    s_ui.current_photo_index = (s_ui.current_photo_index + (int)delta + count) % count;
    update_photo_view();
}

static void music_item_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (audio_player_play_index(index) == ESP_OK) {
        ui_music_player_update();
    }
}

static void video_card_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    s_ui.current_video_index = (int)(intptr_t)lv_event_get_user_data(e);
    update_video_preview();
    update_video_card_selection();
}

static void create_status_bar(lv_obj_t *screen)
{
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
    style_body_label(s_ui.status_label, lv_color_hex(0xB9C7D8));
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

    s_ui.network_value_label = lv_label_create(status_metrics);
    lv_obj_set_style_text_font(s_ui.network_value_label, LV_FONT_DEFAULT, 0);

    s_ui.battery_icon_label = lv_label_create(status_metrics);
    lv_obj_set_style_text_font(s_ui.battery_icon_label, LV_FONT_DEFAULT, 0);

    s_ui.battery_label = lv_label_create(status_metrics);
    lv_obj_set_style_text_font(s_ui.battery_label, LV_FONT_DEFAULT, 0);
}

static void create_page_indicator(lv_obj_t *screen)
{
    lv_obj_t *wrap = lv_obj_create(screen);
    lv_obj_set_size(wrap, 150, 26);
    lv_obj_set_pos(wrap, (LCD_H_RES - 150) / 2, 450);
    lv_obj_set_style_radius(wrap, 13, 0);
    lv_obj_set_style_border_width(wrap, 1, 0);
    lv_obj_set_style_border_color(wrap, lv_color_hex(0x213A51), 0);
    lv_obj_set_style_bg_color(wrap, lv_color_hex(0x0C1724), 0);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        s_ui.page_dots[i] = lv_label_create(wrap);
        lv_obj_set_pos(s_ui.page_dots[i], 10 + (i * 16), 5);
        lv_obj_set_style_text_font(s_ui.page_dots[i], LV_FONT_DEFAULT, 0);
        lv_label_set_text(s_ui.page_dots[i], LV_SYMBOL_BULLET);
    }

    s_ui.page_name_label = lv_label_create(wrap);
    lv_obj_set_pos(s_ui.page_name_label, 78, 4);
    style_body_label(s_ui.page_name_label, lv_color_hex(0xBFD3E6));
    update_page_indicator();
}

static void create_home_page(lv_obj_t *page)
{
    lv_obj_t *calendar_panel = lv_obj_create(page);
    lv_obj_set_size(calendar_panel, 436, 388);
    lv_obj_set_pos(calendar_panel, 0, 0);
    style_panel(calendar_panel);

    lv_obj_t *calendar_head = lv_label_create(calendar_panel);
    lv_obj_set_pos(calendar_head, 0, 0);
    style_section_title(calendar_head);
    lv_label_set_text(calendar_head, "本月日历");

    s_ui.calendar_title_label = lv_label_create(calendar_panel);
    lv_obj_set_pos(s_ui.calendar_title_label, 0, 24);
    lv_obj_set_width(s_ui.calendar_title_label, LV_PCT(100));
    style_body_label(s_ui.calendar_title_label, lv_color_hex(0xFFFFFF));
    lv_obj_set_style_text_align(s_ui.calendar_title_label, LV_TEXT_ALIGN_LEFT, 0);

    for (int i = 0; i < UI_CALENDAR_COLS; i++) {
        s_ui.calendar_weekday_labels[i] = lv_label_create(calendar_panel);
        lv_obj_set_size(s_ui.calendar_weekday_labels[i], 48, LV_SIZE_CONTENT);
        lv_obj_set_pos(s_ui.calendar_weekday_labels[i], 4 + (i * 56), 58);
        style_body_label(s_ui.calendar_weekday_labels[i],
                         i >= 5 ? lv_color_hex(0xFFB057) : lv_color_hex(0x8FB4D7));
        lv_obj_set_style_text_align(s_ui.calendar_weekday_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_ui.calendar_weekday_labels[i], s_calendar_headers[i]);
    }

    for (int i = 0; i < UI_CALENDAR_CELL_COUNT; i++) {
        int col = i % UI_CALENDAR_COLS;
        int row = i / UI_CALENDAR_COLS;
        s_ui.calendar_day_cells[i] = lv_obj_create(calendar_panel);
        lv_obj_set_size(s_ui.calendar_day_cells[i], 48, 42);
        lv_obj_set_pos(s_ui.calendar_day_cells[i], 4 + (col * 56), 90 + (row * 46));
        style_chip(s_ui.calendar_day_cells[i], lv_color_hex(0x111F2D));

        s_ui.calendar_day_labels[i] = lv_label_create(s_ui.calendar_day_cells[i]);
        lv_obj_set_size(s_ui.calendar_day_labels[i], LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_center(s_ui.calendar_day_labels[i]);
        style_body_label(s_ui.calendar_day_labels[i], lv_color_hex(0xD6E2EF));
        lv_obj_set_style_text_align(s_ui.calendar_day_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_ui.calendar_day_labels[i], "");
    }

    lv_obj_t *time_panel = lv_obj_create(page);
    lv_obj_set_size(time_panel, 320, 150);
    lv_obj_set_pos(time_panel, 448, 0);
    style_panel(time_panel);

    lv_obj_t *time_head = lv_label_create(time_panel);
    lv_obj_set_pos(time_head, 0, 0);
    style_section_title(time_head);
    lv_label_set_text(time_head, "时间");

    s_ui.time_label = lv_label_create(time_panel);
    lv_obj_set_width(s_ui.time_label, LV_PCT(100));
    lv_obj_set_pos(s_ui.time_label, 0, 34);
    lv_obj_set_style_text_font(s_ui.time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_ui.time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_ui.time_label, LV_TEXT_ALIGN_CENTER, 0);

    s_ui.date_label = lv_label_create(time_panel);
    lv_obj_set_width(s_ui.date_label, LV_PCT(100));
    lv_obj_set_pos(s_ui.date_label, 0, 96);
    style_body_label(s_ui.date_label, lv_color_hex(0xA7B5C8));
    lv_obj_set_style_text_align(s_ui.date_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *weather_panel = lv_obj_create(page);
    lv_obj_set_size(weather_panel, 320, 226);
    lv_obj_set_pos(weather_panel, 448, 162);
    style_panel(weather_panel);

    lv_obj_t *weather_head = lv_label_create(weather_panel);
    lv_obj_set_pos(weather_head, 0, 0);
    style_section_title(weather_head);
    lv_label_set_text(weather_head, "天气信息");

    s_ui.weather_title = lv_label_create(weather_panel);
    lv_obj_set_pos(s_ui.weather_title, 0, 26);
    lv_obj_set_width(s_ui.weather_title, 160);
    style_body_label(s_ui.weather_title, lv_color_hex(0xFFFFFF));

    s_ui.weather_temp = lv_label_create(weather_panel);
    lv_obj_set_pos(s_ui.weather_temp, 0, 52);
    lv_obj_set_style_text_font(s_ui.weather_temp, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_ui.weather_temp, lv_color_hex(0xFFFFFF), 0);

    s_ui.weather_cond = lv_label_create(weather_panel);
    lv_obj_set_pos(s_ui.weather_cond, 0, 96);
    lv_obj_set_width(s_ui.weather_cond, 160);
    style_body_label(s_ui.weather_cond, lv_color_hex(0x68C1FF));

    create_weather_visual(weather_panel);
    create_detail_card(weather_panel, &s_ui.detail_humidity, 0, 118);
    create_detail_card(weather_panel, &s_ui.detail_feels, 138, 118);
    create_detail_card(weather_panel, &s_ui.detail_wind, 0, 160);
    create_detail_card(weather_panel, &s_ui.detail_vis, 138, 160);
}

static void create_photo_page(lv_obj_t *page)
{
    lv_obj_t *preview_panel = lv_obj_create(page);
    lv_obj_set_size(preview_panel, 518, 388);
    lv_obj_set_pos(preview_panel, 0, 0);
    style_panel(preview_panel);

    lv_obj_t *photo_head = lv_label_create(preview_panel);
    lv_obj_set_pos(photo_head, 0, 0);
    style_section_title(photo_head);
    lv_label_set_text(photo_head, "Picture 轮播");

    lv_obj_t *photo_frame = lv_obj_create(preview_panel);
    lv_obj_set_size(photo_frame, 482, 300);
    lv_obj_set_pos(photo_frame, 0, 32);
    style_thumb_frame(photo_frame);

    s_ui.photo_image = lv_image_create(photo_frame);
    lv_obj_set_size(s_ui.photo_image, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(s_ui.photo_image, LV_IMAGE_ALIGN_STRETCH);

    s_ui.photo_placeholder_icon = lv_label_create(photo_frame);
    lv_obj_center(s_ui.photo_placeholder_icon);
    lv_obj_set_style_text_font(s_ui.photo_placeholder_icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_ui.photo_placeholder_icon, lv_color_hex(0x4C6B86), 0);
    lv_label_set_text(s_ui.photo_placeholder_icon, LV_SYMBOL_IMAGE);

    lv_obj_t *info_panel = lv_obj_create(page);
    lv_obj_set_size(info_panel, 238, 388);
    lv_obj_set_pos(info_panel, 530, 0);
    style_panel(info_panel);

    lv_obj_t *info_head = lv_label_create(info_panel);
    lv_obj_set_pos(info_head, 0, 0);
    style_section_title(info_head);
    lv_label_set_text(info_head, "当前照片");

    s_ui.photo_name_label = lv_label_create(info_panel);
    lv_obj_set_size(s_ui.photo_name_label, LV_PCT(100), 72);
    lv_obj_set_pos(s_ui.photo_name_label, 0, 36);
    style_body_label(s_ui.photo_name_label, lv_color_hex(0xFFFFFF));
    lv_label_set_long_mode(s_ui.photo_name_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_ui.photo_name_label, LV_TEXT_ALIGN_CENTER, 0);

    s_ui.photo_meta_label = lv_label_create(info_panel);
    lv_obj_set_size(s_ui.photo_meta_label, LV_PCT(100), 40);
    lv_obj_set_pos(s_ui.photo_meta_label, 0, 118);
    style_body_label(s_ui.photo_meta_label, lv_color_hex(0x8FB4D7));
    lv_obj_set_style_text_align(s_ui.photo_meta_label, LV_TEXT_ALIGN_CENTER, 0);

    s_ui.photo_status_label = lv_label_create(info_panel);
    lv_obj_set_size(s_ui.photo_status_label, LV_PCT(100), 80);
    lv_obj_set_pos(s_ui.photo_status_label, 0, 164);
    style_body_label(s_ui.photo_status_label, lv_color_hex(0xB8C8D7));
    lv_label_set_long_mode(s_ui.photo_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_ui.photo_status_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *btn_prev = lv_button_create(info_panel);
    lv_obj_set_size(btn_prev, 82, 46);
    lv_obj_set_pos(btn_prev, 10, 300);
    lv_obj_add_event_cb(btn_prev, photo_nav_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    style_chip(btn_prev, lv_color_hex(0x15273A));

    lv_obj_t *btn_prev_label = lv_label_create(btn_prev);
    lv_obj_center(btn_prev_label);
    lv_obj_set_style_text_font(btn_prev_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(btn_prev_label, lv_color_hex(0x68C1FF), 0);
    lv_label_set_text(btn_prev_label, LV_SYMBOL_PREV);

    lv_obj_t *btn_next = lv_button_create(info_panel);
    lv_obj_set_size(btn_next, 82, 46);
    lv_obj_set_pos(btn_next, 110, 300);
    lv_obj_add_event_cb(btn_next, photo_nav_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    style_chip(btn_next, lv_color_hex(0x15273A));

    lv_obj_t *btn_next_label = lv_label_create(btn_next);
    lv_obj_center(btn_next_label);
    lv_obj_set_style_text_font(btn_next_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(btn_next_label, lv_color_hex(0x68C1FF), 0);
    lv_label_set_text(btn_next_label, LV_SYMBOL_NEXT);
}

static void create_music_page(lv_obj_t *page)
{
    ui_music_player_create(page, 0, 0, 272, 388);

    lv_obj_t *list_panel = lv_obj_create(page);
    lv_obj_set_size(list_panel, 484, 388);
    lv_obj_set_pos(list_panel, 284, 0);
    style_panel(list_panel);

    lv_obj_t *list_head = lv_label_create(list_panel);
    lv_obj_set_pos(list_head, 0, 0);
    style_section_title(list_head);
    lv_label_set_text(list_head, "Music 列表");

    s_ui.music_list_wrap = lv_obj_create(list_panel);
    lv_obj_set_size(s_ui.music_list_wrap, LV_PCT(100), 330);
    lv_obj_set_pos(s_ui.music_list_wrap, 0, 36);
    lv_obj_set_style_bg_opa(s_ui.music_list_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.music_list_wrap, 0, 0);
    lv_obj_set_style_pad_all(s_ui.music_list_wrap, 0, 0);
    lv_obj_set_scroll_dir(s_ui.music_list_wrap, LV_DIR_VER);
    lv_obj_add_flag(s_ui.music_list_wrap, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_scrollbar_mode(s_ui.music_list_wrap, LV_SCROLLBAR_MODE_AUTO);

    s_ui.music_empty_label = lv_label_create(s_ui.music_list_wrap);
    lv_obj_set_width(s_ui.music_empty_label, LV_PCT(100));
    lv_obj_align(s_ui.music_empty_label, LV_ALIGN_CENTER, 0, 0);
    style_body_label(s_ui.music_empty_label, lv_color_hex(0x7D92A9));
    lv_obj_set_style_text_align(s_ui.music_empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_ui.music_empty_label, "等待 SD 卡中的 Music 目录");
}

static void create_video_page(lv_obj_t *page)
{
    lv_obj_t *preview_panel = lv_obj_create(page);
    lv_obj_set_size(preview_panel, 296, 388);
    lv_obj_set_pos(preview_panel, 0, 0);
    style_panel(preview_panel);

    lv_obj_t *preview_head = lv_label_create(preview_panel);
    lv_obj_set_pos(preview_head, 0, 0);
    style_section_title(preview_head);
    lv_label_set_text(preview_head, "视频预览");

    lv_obj_t *preview_frame = lv_obj_create(preview_panel);
    lv_obj_set_size(preview_frame, 260, 180);
    lv_obj_set_pos(preview_frame, 0, 36);
    style_thumb_frame(preview_frame);

    s_ui.video_preview_image = lv_image_create(preview_frame);
    lv_obj_set_size(s_ui.video_preview_image, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(s_ui.video_preview_image, LV_IMAGE_ALIGN_STRETCH);

    s_ui.video_preview_placeholder_icon = lv_label_create(preview_frame);
    lv_obj_center(s_ui.video_preview_placeholder_icon);
    lv_obj_set_style_text_font(s_ui.video_preview_placeholder_icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_ui.video_preview_placeholder_icon, lv_color_hex(0x4C6B86), 0);
    lv_label_set_text(s_ui.video_preview_placeholder_icon, LV_SYMBOL_VIDEO);

    s_ui.video_name_label = lv_label_create(preview_panel);
    lv_obj_set_size(s_ui.video_name_label, LV_PCT(100), 64);
    lv_obj_set_pos(s_ui.video_name_label, 0, 232);
    style_body_label(s_ui.video_name_label, lv_color_hex(0xFFFFFF));
    lv_label_set_long_mode(s_ui.video_name_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_ui.video_name_label, LV_TEXT_ALIGN_CENTER, 0);

    s_ui.video_status_label = lv_label_create(preview_panel);
    lv_obj_set_size(s_ui.video_status_label, LV_PCT(100), 70);
    lv_obj_set_pos(s_ui.video_status_label, 0, 304);
    style_body_label(s_ui.video_status_label, lv_color_hex(0xA7B5C8));
    lv_label_set_long_mode(s_ui.video_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_ui.video_status_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *list_panel = lv_obj_create(page);
    lv_obj_set_size(list_panel, 460, 388);
    lv_obj_set_pos(list_panel, 308, 0);
    style_panel(list_panel);

    lv_obj_t *list_head = lv_label_create(list_panel);
    lv_obj_set_pos(list_head, 0, 0);
    style_section_title(list_head);
    lv_label_set_text(list_head, "Video 缩略图");

    s_ui.video_list_wrap = lv_obj_create(list_panel);
    lv_obj_set_size(s_ui.video_list_wrap, LV_PCT(100), 330);
    lv_obj_set_pos(s_ui.video_list_wrap, 0, 36);
    lv_obj_set_style_bg_opa(s_ui.video_list_wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.video_list_wrap, 0, 0);
    lv_obj_set_style_pad_all(s_ui.video_list_wrap, 0, 0);
    lv_obj_set_scroll_dir(s_ui.video_list_wrap, LV_DIR_VER);
    lv_obj_add_flag(s_ui.video_list_wrap, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_scrollbar_mode(s_ui.video_list_wrap, LV_SCROLLBAR_MODE_AUTO);

    s_ui.video_empty_label = lv_label_create(s_ui.video_list_wrap);
    lv_obj_set_width(s_ui.video_empty_label, LV_PCT(100));
    lv_obj_align(s_ui.video_empty_label, LV_ALIGN_CENTER, 0, 0);
    style_body_label(s_ui.video_empty_label, lv_color_hex(0x7D92A9));
    lv_obj_set_style_text_align(s_ui.video_empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_ui.video_empty_label, "等待 Video 目录和封面图");
}

static void create_ui(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07111C), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x10263D), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);

    lv_obj_t *glow_left = lv_obj_create(screen);
    lv_obj_clear_flag(glow_left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(glow_left, 220, 220);
    lv_obj_set_pos(glow_left, -40, -70);
    lv_obj_set_style_radius(glow_left, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(glow_left, 0, 0);
    lv_obj_set_style_bg_color(glow_left, lv_color_hex(0x184C77), 0);
    lv_obj_set_style_bg_opa(glow_left, LV_OPA_20, 0);

    lv_obj_t *glow_right = lv_obj_create(screen);
    lv_obj_clear_flag(glow_right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(glow_right, 180, 180);
    lv_obj_set_pos(glow_right, 606, 278);
    lv_obj_set_style_radius(glow_right, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(glow_right, 0, 0);
    lv_obj_set_style_bg_color(glow_right, lv_color_hex(0xFFB057), 0);
    lv_obj_set_style_bg_opa(glow_right, LV_OPA_20, 0);

    create_status_bar(screen);

    s_ui.tileview = lv_obj_create(screen);
    lv_obj_clear_flag(s_ui.tileview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_ui.tileview, LCD_H_RES - 32, 388);
    lv_obj_set_pos(s_ui.tileview, 16, 66);
    lv_obj_set_style_bg_opa(s_ui.tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.tileview, 0, 0);
    lv_obj_set_style_pad_all(s_ui.tileview, 0, 0);
    lv_obj_set_scrollbar_mode(s_ui.tileview, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        s_ui.pages[i] = lv_obj_create(s_ui.tileview);
        lv_obj_set_size(s_ui.pages[i], LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(s_ui.pages[i], 0, 0);
    }

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        lv_obj_clear_flag(s_ui.pages[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(s_ui.pages[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_ui.pages[i], 0, 0);
        lv_obj_set_style_pad_all(s_ui.pages[i], 0, 0);
    }

    create_home_page(s_ui.pages[UI_PAGE_HOME]);
    create_photo_page(s_ui.pages[UI_PAGE_PHOTO]);
    create_music_page(s_ui.pages[UI_PAGE_MUSIC]);
    create_video_page(s_ui.pages[UI_PAGE_VIDEO]);
    switch_to_page(UI_PAGE_HOME, LV_ANIM_OFF);
    create_page_indicator(screen);
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

    if (!s_ui.status_colors_valid ||
        memcmp(&s_ui.rendered_network_color, &network_color, sizeof(network_color)) != 0) {
        lv_obj_set_style_text_color(s_ui.network_icon_label, network_color, 0);
        lv_obj_set_style_text_color(s_ui.network_value_label, network_color, 0);
        s_ui.rendered_network_color = network_color;
    }
    if (!s_ui.status_colors_valid ||
        memcmp(&s_ui.rendered_battery_color, &battery_color, sizeof(battery_color)) != 0) {
        lv_obj_set_style_text_color(s_ui.battery_icon_label, battery_color, 0);
        lv_obj_set_style_text_color(s_ui.battery_label, battery_color, 0);
        s_ui.rendered_battery_color = battery_color;
    }
    s_ui.status_colors_valid = true;
}

static void update_calendar_view(const struct tm *now)
{
    if (!now) {
        if (!s_ui.calendar_rendered_valid &&
            strncmp(s_ui.calendar_title_text, "等待授时", sizeof(s_ui.calendar_title_text)) == 0) {
            return;
        }
        set_label_text_if_changed(s_ui.calendar_title_label,
                                  s_ui.calendar_title_text,
                                  sizeof(s_ui.calendar_title_text),
                                  "等待授时");
        for (int i = 0; i < UI_CALENDAR_CELL_COUNT; i++) {
            lv_label_set_text(s_ui.calendar_day_labels[i], "");
            lv_obj_set_style_bg_color(s_ui.calendar_day_cells[i], lv_color_hex(0x111F2D), 0);
            lv_obj_set_style_border_color(s_ui.calendar_day_cells[i], lv_color_hex(0x25435C), 0);
        }
        s_ui.calendar_rendered_valid = false;
        return;
    }

    if (s_ui.calendar_rendered_valid &&
        s_ui.rendered_calendar_year == now->tm_year &&
        s_ui.rendered_calendar_month == now->tm_mon &&
        s_ui.rendered_calendar_day == now->tm_mday) {
        return;
    }

    snprintf(s_ui.calendar_title_text, sizeof(s_ui.calendar_title_text),
             "%04d年%02d月", now->tm_year + 1900, now->tm_mon + 1);
    lv_label_set_text(s_ui.calendar_title_label, s_ui.calendar_title_text);

    struct tm first_day = {
        .tm_year = now->tm_year,
        .tm_mon = now->tm_mon,
        .tm_mday = 1,
        .tm_isdst = -1,
    };
    mktime(&first_day);

    int start_col = (first_day.tm_wday + 6) % 7;
    int total_days = days_in_month(now->tm_year + 1900, now->tm_mon + 1);

    for (int i = 0; i < UI_CALENDAR_CELL_COUNT; i++) {
        int day = i - start_col + 1;
        bool in_month = day >= 1 && day <= total_days;
        bool is_today = in_month && day == now->tm_mday;
        bool weekend = (i % UI_CALENDAR_COLS) >= 5;

        if (in_month) {
            char day_text[8];
            snprintf(day_text, sizeof(day_text), "%d", day);
            lv_label_set_text(s_ui.calendar_day_labels[i], day_text);
            lv_obj_set_style_text_color(s_ui.calendar_day_labels[i],
                                        is_today ? lv_color_hex(0xFFFFFF) :
                                        (weekend ? lv_color_hex(0xFFB057) : lv_color_hex(0xD6E2EF)),
                                        0);
            lv_obj_set_style_bg_color(s_ui.calendar_day_cells[i],
                                      is_today ? lv_color_hex(0xFF8D3A) : lv_color_hex(0x111F2D),
                                      0);
            lv_obj_set_style_border_color(s_ui.calendar_day_cells[i],
                                          is_today ? lv_color_hex(0xFFD0A5) : lv_color_hex(0x25435C),
                                          0);
        } else {
            lv_label_set_text(s_ui.calendar_day_labels[i], "");
            lv_obj_set_style_bg_color(s_ui.calendar_day_cells[i], lv_color_hex(0x101721), 0);
            lv_obj_set_style_border_color(s_ui.calendar_day_cells[i], lv_color_hex(0x1C3145), 0);
        }
    }

    s_ui.rendered_calendar_year = now->tm_year;
    s_ui.rendered_calendar_month = now->tm_mon;
    s_ui.rendered_calendar_day = now->tm_mday;
    s_ui.calendar_rendered_valid = true;
}

static void update_clock_text(void)
{
    struct tm now = {0};
    char new_time[sizeof(s_ui.time_text)];
    char new_date[sizeof(s_ui.date_text)];

    if (!(s_time_ready || ntp_client_has_valid_time())) {
        strlcpy(new_time, "--:--", sizeof(new_time));
        strlcpy(new_date, "等待 NTP 同步", sizeof(new_date));
        set_label_text_if_changed(s_ui.time_label, s_ui.time_text, sizeof(s_ui.time_text), new_time);
        set_label_text_if_changed(s_ui.date_label, s_ui.date_text, sizeof(s_ui.date_text), new_date);
        update_calendar_view(NULL);
        return;
    }

    ntp_client_get_local_time(&now);

    snprintf(new_time, sizeof(new_time), "%02d:%02d", now.tm_hour, now.tm_min);
    snprintf(new_date, sizeof(new_date), "%04d年%02d月%02d日 %s",
             now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, weekday_cn(now.tm_wday));

    set_label_text_if_changed(s_ui.time_label, s_ui.time_text, sizeof(s_ui.time_text), new_time);
    set_label_text_if_changed(s_ui.date_label, s_ui.date_text, sizeof(s_ui.date_text), new_date);
    update_calendar_view(&now);
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

static void update_photo_view(void)
{
    int count = media_library_get_photo_count();
    if (count <= 0) {
        s_ui.current_photo_index = 0;
        s_ui.rendered_photo_index = -1;
        s_ui.rendered_photo_count = 0;
        show_image_or_placeholder(s_ui.photo_image, s_ui.photo_placeholder_icon, NULL, LV_SYMBOL_IMAGE);
        set_label_text_if_changed(s_ui.photo_name_label,
                                  s_ui.photo_name_text,
                                  sizeof(s_ui.photo_name_text),
                                  "Picture 目录为空");
        set_label_text_if_changed(s_ui.photo_meta_label,
                                  s_ui.photo_meta_text,
                                  sizeof(s_ui.photo_meta_text),
                                  "0 / 0");
        set_label_text_if_changed(s_ui.photo_status_label,
                                  s_ui.photo_status_text,
                                  sizeof(s_ui.photo_status_text),
                                  "请把照片放到 SD 卡的 Picture 目录下，支持 JPG / PNG / BMP。");
        return;
    }

    if (s_ui.current_photo_index < 0 || s_ui.current_photo_index >= count) {
        s_ui.current_photo_index = 0;
    }

    if (s_ui.rendered_photo_index == s_ui.current_photo_index &&
        s_ui.rendered_photo_count == count &&
        s_ui.photo_name_text[0] != '\0') {
        return;
    }

    const media_library_entry_t *entry = media_library_get_photo(s_ui.current_photo_index);
    if (!entry) {
        return;
    }

    show_image_or_placeholder(s_ui.photo_image,
                              s_ui.photo_placeholder_icon,
                              entry->preview_lvgl_path,
                              LV_SYMBOL_IMAGE);

    set_label_text_if_changed(s_ui.photo_name_label,
                              s_ui.photo_name_text,
                              sizeof(s_ui.photo_name_text),
                              entry->name);

    snprintf(s_ui.photo_meta_text, sizeof(s_ui.photo_meta_text),
             "%d / %d", s_ui.current_photo_index + 1, count);
    lv_label_set_text(s_ui.photo_meta_label, s_ui.photo_meta_text);

    set_label_text_if_changed(s_ui.photo_status_label,
                              s_ui.photo_status_text,
                              sizeof(s_ui.photo_status_text),
                              count > 1 ? "轮播中，可在本屏停留自动切换，也可点左右键手动切换。" :
                                          "当前仅有 1 张照片。");
    s_ui.rendered_photo_index = s_ui.current_photo_index;
    s_ui.rendered_photo_count = count;
}

static void rebuild_music_list(void)
{
    int count = audio_player_get_track_count();

    s_ui.music_item_count = 0;
    s_ui.rendered_music_track = -2;
    memset(s_ui.music_item_btn, 0, sizeof(s_ui.music_item_btn));
    memset(s_ui.music_item_index_label, 0, sizeof(s_ui.music_item_index_label));
    memset(s_ui.music_item_title_label, 0, sizeof(s_ui.music_item_title_label));
    lv_obj_clean(s_ui.music_list_wrap);

    if (count <= 0) {
        s_ui.music_empty_label = lv_label_create(s_ui.music_list_wrap);
        lv_obj_set_width(s_ui.music_empty_label, LV_PCT(100));
        lv_obj_align(s_ui.music_empty_label, LV_ALIGN_CENTER, 0, 0);
        style_body_label(s_ui.music_empty_label, lv_color_hex(0x7D92A9));
        lv_obj_set_style_text_align(s_ui.music_empty_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_ui.music_empty_label, "Music 目录暂无可播放文件");
        return;
    }

    for (int i = 0; i < count && i < AUDIO_MAX_PLAYLIST_SIZE; i++) {
        audio_track_info_t info = {0};
        if (audio_player_get_track_info(i, &info) != ESP_OK) {
            continue;
        }

        s_ui.music_item_btn[s_ui.music_item_count] = lv_button_create(s_ui.music_list_wrap);
        lv_obj_set_size(s_ui.music_item_btn[s_ui.music_item_count], LV_PCT(100), 52);
        lv_obj_set_pos(s_ui.music_item_btn[s_ui.music_item_count], 0, s_ui.music_item_count * 58);
        lv_obj_add_event_cb(s_ui.music_item_btn[s_ui.music_item_count],
                            music_item_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        style_chip(s_ui.music_item_btn[s_ui.music_item_count], lv_color_hex(0x132334));
        lv_obj_set_style_pad_left(s_ui.music_item_btn[s_ui.music_item_count], 14, 0);
        lv_obj_set_style_pad_right(s_ui.music_item_btn[s_ui.music_item_count], 14, 0);

        s_ui.music_item_index_label[s_ui.music_item_count] = lv_label_create(s_ui.music_item_btn[s_ui.music_item_count]);
        lv_obj_set_pos(s_ui.music_item_index_label[s_ui.music_item_count], 0, 14);
        style_body_label(s_ui.music_item_index_label[s_ui.music_item_count], lv_color_hex(0x68C1FF));
        lv_label_set_text_fmt(s_ui.music_item_index_label[s_ui.music_item_count], "%02d", i + 1);

        s_ui.music_item_title_label[s_ui.music_item_count] = lv_label_create(s_ui.music_item_btn[s_ui.music_item_count]);
        lv_obj_set_size(s_ui.music_item_title_label[s_ui.music_item_count], 360, LV_SIZE_CONTENT);
        lv_obj_set_pos(s_ui.music_item_title_label[s_ui.music_item_count], 48, 14);
        style_body_label(s_ui.music_item_title_label[s_ui.music_item_count], lv_color_hex(0xFFFFFF));
        lv_label_set_long_mode(s_ui.music_item_title_label[s_ui.music_item_count], LV_LABEL_LONG_DOT);
        lv_label_set_text(s_ui.music_item_title_label[s_ui.music_item_count], info.title);

        s_ui.music_item_count++;
    }
}

static void update_music_list_selection(void)
{
    audio_player_status_t status = {0};
    if (audio_player_get_status(&status) != ESP_OK) {
        return;
    }

    if (status.current_track == s_ui.rendered_music_track) {
        return;
    }

    for (int i = 0; i < s_ui.music_item_count; i++) {
        bool selected = status.current_track == i;
        lv_obj_set_style_bg_color(s_ui.music_item_btn[i],
                                  selected ? lv_color_hex(0x21415F) : lv_color_hex(0x132334),
                                  0);
        lv_obj_set_style_border_color(s_ui.music_item_btn[i],
                                      selected ? lv_color_hex(0x68C1FF) : lv_color_hex(0x25435C),
                                      0);
        lv_obj_set_style_text_color(s_ui.music_item_index_label[i],
                                    selected ? lv_color_hex(0xFFB057) : lv_color_hex(0x68C1FF),
                                    0);
    }

    s_ui.rendered_music_track = status.current_track;
}

static void rebuild_video_list(void)
{
    int count = media_library_get_video_count();

    s_ui.video_card_count = 0;
    s_ui.rendered_video_selection_index = -2;
    s_ui.rendered_video_selection_count = -1;
    memset(s_ui.video_card_btn, 0, sizeof(s_ui.video_card_btn));
    memset(s_ui.video_card_thumb_frame, 0, sizeof(s_ui.video_card_thumb_frame));
    memset(s_ui.video_card_image, 0, sizeof(s_ui.video_card_image));
    memset(s_ui.video_card_placeholder, 0, sizeof(s_ui.video_card_placeholder));
    memset(s_ui.video_card_title, 0, sizeof(s_ui.video_card_title));
    lv_obj_clean(s_ui.video_list_wrap);

    if (count <= 0) {
        s_ui.video_empty_label = lv_label_create(s_ui.video_list_wrap);
        lv_obj_set_width(s_ui.video_empty_label, LV_PCT(100));
        lv_obj_align(s_ui.video_empty_label, LV_ALIGN_CENTER, 0, 0);
        style_body_label(s_ui.video_empty_label, lv_color_hex(0x7D92A9));
        lv_obj_set_style_text_align(s_ui.video_empty_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(s_ui.video_empty_label, "Video 目录为空");
        s_ui.current_video_index = -1;
        return;
    }

    for (int i = 0; i < count && i < MEDIA_LIBRARY_MAX_ITEMS; i++) {
        const media_library_entry_t *entry = media_library_get_video(i);
        if (!entry) {
            continue;
        }

        s_ui.video_card_btn[s_ui.video_card_count] = lv_button_create(s_ui.video_list_wrap);
        lv_obj_set_size(s_ui.video_card_btn[s_ui.video_card_count], LV_PCT(100), 108);
        lv_obj_set_pos(s_ui.video_card_btn[s_ui.video_card_count], 0, s_ui.video_card_count * 116);
        lv_obj_add_event_cb(s_ui.video_card_btn[s_ui.video_card_count],
                            video_card_event_cb,
                            LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        style_chip(s_ui.video_card_btn[s_ui.video_card_count], lv_color_hex(0x132334));
        lv_obj_set_style_pad_all(s_ui.video_card_btn[s_ui.video_card_count], 10, 0);

        s_ui.video_card_thumb_frame[s_ui.video_card_count] = lv_obj_create(s_ui.video_card_btn[s_ui.video_card_count]);
        lv_obj_set_size(s_ui.video_card_thumb_frame[s_ui.video_card_count], 152, 86);
        lv_obj_set_pos(s_ui.video_card_thumb_frame[s_ui.video_card_count], 0, 0);
        style_thumb_frame(s_ui.video_card_thumb_frame[s_ui.video_card_count]);

        s_ui.video_card_image[s_ui.video_card_count] = lv_image_create(s_ui.video_card_thumb_frame[s_ui.video_card_count]);
        lv_obj_set_size(s_ui.video_card_image[s_ui.video_card_count], LV_PCT(100), LV_PCT(100));
        lv_image_set_inner_align(s_ui.video_card_image[s_ui.video_card_count], LV_IMAGE_ALIGN_STRETCH);

        s_ui.video_card_placeholder[s_ui.video_card_count] = lv_label_create(s_ui.video_card_thumb_frame[s_ui.video_card_count]);
        lv_obj_center(s_ui.video_card_placeholder[s_ui.video_card_count]);
        lv_obj_set_style_text_font(s_ui.video_card_placeholder[s_ui.video_card_count], &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(s_ui.video_card_placeholder[s_ui.video_card_count], lv_color_hex(0x4C6B86), 0);
        lv_label_set_text(s_ui.video_card_placeholder[s_ui.video_card_count], LV_SYMBOL_VIDEO);

        show_image_or_placeholder(s_ui.video_card_image[s_ui.video_card_count],
                                  s_ui.video_card_placeholder[s_ui.video_card_count],
                                  entry->has_preview ? entry->preview_lvgl_path : NULL,
                                  LV_SYMBOL_VIDEO);

        s_ui.video_card_title[s_ui.video_card_count] = lv_label_create(s_ui.video_card_btn[s_ui.video_card_count]);
        lv_obj_set_size(s_ui.video_card_title[s_ui.video_card_count], 240, 60);
        lv_obj_set_pos(s_ui.video_card_title[s_ui.video_card_count], 170, 14);
        style_body_label(s_ui.video_card_title[s_ui.video_card_count], lv_color_hex(0xFFFFFF));
        lv_label_set_long_mode(s_ui.video_card_title[s_ui.video_card_count], LV_LABEL_LONG_WRAP);
        lv_label_set_text(s_ui.video_card_title[s_ui.video_card_count], entry->name);

        lv_obj_t *hint = lv_label_create(s_ui.video_card_btn[s_ui.video_card_count]);
        lv_obj_set_pos(hint, 170, 74);
        style_body_label(hint, lv_color_hex(0x7D92A9));
        lv_label_set_text(hint, entry->has_preview ? "已匹配封面图" : "缺少同名封面图");

        s_ui.video_card_count++;
    }

    if (s_ui.current_video_index < 0 || s_ui.current_video_index >= count) {
        s_ui.current_video_index = 0;
    }
}

static void update_video_preview(void)
{
    int count = media_library_get_video_count();
    if (count <= 0 || s_ui.current_video_index < 0 || s_ui.current_video_index >= count) {
        s_ui.rendered_video_index = -1;
        s_ui.rendered_video_count = 0;
        show_image_or_placeholder(s_ui.video_preview_image,
                                  s_ui.video_preview_placeholder_icon,
                                  NULL,
                                  LV_SYMBOL_VIDEO);
        set_label_text_if_changed(s_ui.video_name_label,
                                  s_ui.video_name_text,
                                  sizeof(s_ui.video_name_text),
                                  "暂无视频");
        set_label_text_if_changed(s_ui.video_status_label,
                                  s_ui.video_status_text,
                                  sizeof(s_ui.video_status_text),
                                  "请把视频文件放到 SD 卡的 Video 目录，并放入同名封面图。");
        return;
    }

    if (s_ui.rendered_video_index == s_ui.current_video_index &&
        s_ui.rendered_video_count == count &&
        s_ui.video_name_text[0] != '\0') {
        return;
    }

    const media_library_entry_t *entry = media_library_get_video(s_ui.current_video_index);
    if (!entry) {
        return;
    }

    show_image_or_placeholder(s_ui.video_preview_image,
                              s_ui.video_preview_placeholder_icon,
                              entry->has_preview ? entry->preview_lvgl_path : NULL,
                              LV_SYMBOL_VIDEO);

    set_label_text_if_changed(s_ui.video_name_label,
                              s_ui.video_name_text,
                              sizeof(s_ui.video_name_text),
                              entry->name);

    set_label_text_if_changed(s_ui.video_status_label,
                              s_ui.video_status_text,
                              sizeof(s_ui.video_status_text),
                              entry->has_preview ?
                              "已切换到该视频的预览图，点击右侧其他缩略图可快速切换。" :
                              "已选中该视频，但当前未找到同名封面图。");
    s_ui.rendered_video_index = s_ui.current_video_index;
    s_ui.rendered_video_count = count;
}

static void update_video_card_selection(void)
{
    if (s_ui.rendered_video_selection_index == s_ui.current_video_index &&
        s_ui.rendered_video_selection_count == s_ui.video_card_count) {
        return;
    }

    for (int i = 0; i < s_ui.video_card_count; i++) {
        bool selected = s_ui.current_video_index == i;
        lv_obj_set_style_bg_color(s_ui.video_card_btn[i],
                                  selected ? lv_color_hex(0x21415F) : lv_color_hex(0x132334),
                                  0);
        lv_obj_set_style_border_color(s_ui.video_card_btn[i],
                                      selected ? lv_color_hex(0x68C1FF) : lv_color_hex(0x25435C),
                                      0);
    }

    s_ui.rendered_video_selection_index = s_ui.current_video_index;
    s_ui.rendered_video_selection_count = s_ui.video_card_count;
}

static void update_all_ui_text(void)
{
    update_status_text();
    update_clock_text();
    update_weather_text();
    update_photo_view();
    update_music_list_selection();
    update_video_preview();
    update_video_card_selection();
    update_page_indicator();
}

static void refresh_runtime_fonts(void)
{
    const lv_font_t *font = ui_font_manager_get_font_16();

    if (s_ui.status_label) lv_obj_set_style_text_font(s_ui.status_label, font, 0);
    if (s_ui.date_label) lv_obj_set_style_text_font(s_ui.date_label, font, 0);
    if (s_ui.calendar_title_label) lv_obj_set_style_text_font(s_ui.calendar_title_label, font, 0);
    if (s_ui.page_name_label) lv_obj_set_style_text_font(s_ui.page_name_label, font, 0);
    if (s_ui.weather_title) lv_obj_set_style_text_font(s_ui.weather_title, font, 0);
    if (s_ui.weather_cond) lv_obj_set_style_text_font(s_ui.weather_cond, font, 0);
    if (s_ui.detail_humidity) lv_obj_set_style_text_font(s_ui.detail_humidity, font, 0);
    if (s_ui.detail_feels) lv_obj_set_style_text_font(s_ui.detail_feels, font, 0);
    if (s_ui.detail_wind) lv_obj_set_style_text_font(s_ui.detail_wind, font, 0);
    if (s_ui.detail_vis) lv_obj_set_style_text_font(s_ui.detail_vis, font, 0);
    if (s_ui.photo_name_label) lv_obj_set_style_text_font(s_ui.photo_name_label, font, 0);
    if (s_ui.photo_meta_label) lv_obj_set_style_text_font(s_ui.photo_meta_label, font, 0);
    if (s_ui.photo_status_label) lv_obj_set_style_text_font(s_ui.photo_status_label, font, 0);
    if (s_ui.video_name_label) lv_obj_set_style_text_font(s_ui.video_name_label, font, 0);
    if (s_ui.video_status_label) lv_obj_set_style_text_font(s_ui.video_status_label, font, 0);
    for (int i = 0; i < UI_CALENDAR_COLS; i++) {
        if (s_ui.calendar_weekday_labels[i]) {
            lv_obj_set_style_text_font(s_ui.calendar_weekday_labels[i], font, 0);
        }
    }
    for (int i = 0; i < UI_CALENDAR_CELL_COUNT; i++) {
        if (s_ui.calendar_day_labels[i]) {
            lv_obj_set_style_text_font(s_ui.calendar_day_labels[i], font, 0);
        }
    }
    for (int i = 0; i < 3; i++) {
        if (s_ui.forecast_date[i]) lv_obj_set_style_text_font(s_ui.forecast_date[i], font, 0);
        if (s_ui.forecast_cond[i]) lv_obj_set_style_text_font(s_ui.forecast_cond[i], font, 0);
    }
    for (int i = 0; i < s_ui.music_item_count; i++) {
        if (s_ui.music_item_index_label[i]) lv_obj_set_style_text_font(s_ui.music_item_index_label[i], font, 0);
        if (s_ui.music_item_title_label[i]) lv_obj_set_style_text_font(s_ui.music_item_title_label[i], font, 0);
    }
    for (int i = 0; i < s_ui.video_card_count; i++) {
        if (s_ui.video_card_title[i]) lv_obj_set_style_text_font(s_ui.video_card_title[i], font, 0);
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

static void initialize_sd_media_ui(void)
{
    if (s_ui.sd_media_loaded || !audio_player_is_sd_mounted()) {
        return;
    }

    media_library_refresh();
    rebuild_music_list();
    rebuild_video_list();
    update_photo_view();
    update_video_preview();
    update_video_card_selection();
    s_ui.sd_media_loaded = true;

    ESP_LOGI(TAG_UI, "SD card media UI ready");
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

    ui_init_touch_input();
    create_ui();

    if (battery_monitor_init() != ESP_OK) {
        ESP_LOGW(TAG_UI, "Battery monitor init skipped, status bar will show '--'");
    }

    capture_current_wifi_state(&s_latest_wifi);
    weather_client_get_data(&s_latest_weather);
    s_latest_weather_state = s_latest_weather.valid ? APP_WEATHER_STATE_UPDATED : APP_WEATHER_STATE_WAITING;
    s_time_ready = ntp_client_has_valid_time() || ntp_client_is_synced();
    s_ui.rendered_music_track = -2;
    s_ui.rendered_video_selection_index = -2;
    s_ui.rendered_video_selection_count = -1;
    s_ui.rendered_calendar_year = -1;
    s_ui.rendered_calendar_month = -1;
    s_ui.rendered_calendar_day = -1;
    s_ui.calendar_rendered_valid = false;
    s_ui.status_colors_valid = false;

    update_all_ui_text();
    lv_obj_invalidate(lv_screen_active());
    (void)lv_timer_handler();

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
    int64_t last_photo_rotate_us = 0;
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

        initialize_sd_media_ui();

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
            update_music_list_selection();
            last_music_update_us = now_us;
        }

        if (s_ui.active_page == UI_PAGE_PHOTO &&
            media_library_get_photo_count() > 1 &&
            (last_photo_rotate_us == 0 || now_us - last_photo_rotate_us >= (int64_t)UI_PHOTO_ROTATE_MS * 1000)) {
            s_ui.current_photo_index = (s_ui.current_photo_index + 1) % media_library_get_photo_count();
            update_photo_view();
            last_photo_rotate_us = now_us;
        }

        delay_ms = lv_timer_handler();
        process_pending_swipe();
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
