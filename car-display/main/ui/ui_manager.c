/**
 * @file ui_manager.c
 * @brief UI管理器实现 (LVGL)
 */

#include "ui_manager.h"
#include "display.h"
#include "config.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char *TAG = "ui";

/* ============================================================
 * 控件句柄
 * ============================================================ */
static lv_obj_t *s_screen_main       = NULL;
static ui_screen_t s_current_screen  = UI_SCREEN_MAIN;
static lv_obj_t *s_lbl_speed         = NULL;
static lv_obj_t *s_lbl_speed_unit    = NULL;
static lv_obj_t *s_lbl_emoji         = NULL;
static lv_obj_t *s_lbl_mood          = NULL;
static lv_obj_t *s_lbl_message       = NULL;
static lv_obj_t *s_lbl_voice         = NULL;
static lv_obj_t *s_lbl_stats         = NULL;
static lv_obj_t *s_arc_speed         = NULL;
static lv_obj_t *s_notification_box  = NULL;
static lv_timer_t *s_notif_timer     = NULL;

/* ============================================================
 * 颜色定义
 * ============================================================ */
#define CLR_BG          lv_color_hex(0x0A0A1A)
#define CLR_PANEL       lv_color_hex(0x141428)
#define CLR_SPEED_SLOW  lv_color_hex(0x00E5FF)
#define CLR_SPEED_MID   lv_color_hex(0x00FF88)
#define CLR_SPEED_FAST  lv_color_hex(0xFFD700)
#define CLR_SPEED_OVER  lv_color_hex(0xFF4444)
#define CLR_TEXT        lv_color_hex(0xE0E0E0)
#define CLR_TEXT_DIM    lv_color_hex(0x606060)
#define CLR_ACCENT      lv_color_hex(0x4488FF)
#define CLR_HAPPY       lv_color_hex(0xFFD700)
#define CLR_EXCITED     lv_color_hex(0xFF6B35)
#define CLR_WORRIED     lv_color_hex(0xFF4444)
#define CLR_RELAXED     lv_color_hex(0x44DDAA)
#define CLR_CHEERING    lv_color_hex(0xAA88FF)
#define CLR_VOICE_ON    lv_color_hex(0x00FF88)
#define CLR_VOICE_SPEAK lv_color_hex(0xFF6B35)

/* ============================================================
 * 辅助函数
 * ============================================================ */
static lv_color_t get_speed_color(float speed_kmh)
{
    if (speed_kmh >= SPEED_VERY_FAST) return CLR_SPEED_OVER;
    if (speed_kmh >= SPEED_FAST)      return CLR_SPEED_FAST;
    if (speed_kmh >= SPEED_NORMAL)    return CLR_SPEED_MID;
    return CLR_SPEED_SLOW;
}

static lv_color_t get_mood_color(empathy_mood_t mood)
{
    switch (mood) {
        case MOOD_HAPPY:    return CLR_HAPPY;
        case MOOD_EXCITED:  return CLR_EXCITED;
        case MOOD_WORRIED:  return CLR_WORRIED;
        case MOOD_SCARED:   return CLR_WORRIED;
        case MOOD_RELAXED:  return CLR_RELAXED;
        case MOOD_CHEERING: return CLR_CHEERING;
        default:            return CLR_TEXT;
    }
}

/* ============================================================
 * 主界面创建
 * ============================================================ */
static void create_main_screen(void)
{
    s_screen_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen_main, CLR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen_main, LV_OPA_COVER, 0);

    /* 顶部状态栏 */
    lv_obj_t *top_bar = lv_obj_create(s_screen_main);
    lv_obj_set_size(top_bar, LCD_H_RES, 22);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, CLR_PANEL, 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);

    lv_obj_t *lbl_title = lv_label_create(top_bar);
    lv_label_set_text(lbl_title, "CarPal");
    lv_obj_set_style_text_color(lbl_title, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    /* 语音状态：右侧显示 MIC / SPK* */
    s_lbl_voice = lv_label_create(top_bar);
    lv_label_set_text(s_lbl_voice, "MIC");
    lv_obj_set_style_text_color(s_lbl_voice, CLR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_lbl_voice, &lv_font_montserrat_10, 0);
    lv_obj_align(s_lbl_voice, LV_ALIGN_RIGHT_MID, -4, 0);

    /* 速度仪表盘弧形 */
    s_arc_speed = lv_arc_create(s_screen_main);
    lv_obj_set_size(s_arc_speed, 160, 160);
    lv_obj_align(s_arc_speed, LV_ALIGN_TOP_MID, 0, 28);
    lv_arc_set_rotation(s_arc_speed, 135);
    lv_arc_set_bg_angles(s_arc_speed, 0, 270);
    lv_arc_set_range(s_arc_speed, 0, 140);
    lv_arc_set_value(s_arc_speed, 0);
    lv_obj_set_style_arc_color(s_arc_speed, lv_color_hex(0x1A1A3A), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc_speed, CLR_SPEED_SLOW, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc_speed, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc_speed, 8, LV_PART_INDICATOR);
    lv_obj_remove_style(s_arc_speed, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_arc_speed, LV_OBJ_FLAG_CLICKABLE);

    /* 速度数字 */
    s_lbl_speed = lv_label_create(s_screen_main);
    lv_label_set_text(s_lbl_speed, "0");
    lv_obj_set_style_text_color(s_lbl_speed, CLR_SPEED_SLOW, 0);
    lv_obj_set_style_text_font(s_lbl_speed, &lv_font_montserrat_48, 0);
    lv_obj_align(s_lbl_speed, LV_ALIGN_TOP_MID, 0, 60);

    s_lbl_speed_unit = lv_label_create(s_screen_main);
    lv_label_set_text(s_lbl_speed_unit, "km/h");
    lv_obj_set_style_text_color(s_lbl_speed_unit, CLR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_lbl_speed_unit, &lv_font_montserrat_12, 0);
    lv_obj_align(s_lbl_speed_unit, LV_ALIGN_TOP_MID, 0, 118);

    /* 分隔线 */
    lv_obj_t *line1 = lv_obj_create(s_screen_main);
    lv_obj_set_size(line1, LCD_H_RES - 20, 1);
    lv_obj_align(line1, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_set_style_bg_color(line1, lv_color_hex(0x2A2A4A), 0);
    lv_obj_set_style_border_width(line1, 0, 0);

    /* 情感区域 */
    lv_obj_t *mood_panel = lv_obj_create(s_screen_main);
    lv_obj_set_size(mood_panel, LCD_H_RES, 44);
    lv_obj_set_pos(mood_panel, 0, 143);
    lv_obj_set_style_bg_color(mood_panel, CLR_PANEL, 0);
    lv_obj_set_style_bg_opa(mood_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mood_panel, 0, 0);
    lv_obj_set_style_radius(mood_panel, 0, 0);
    lv_obj_set_style_pad_all(mood_panel, 4, 0);

    s_lbl_emoji = lv_label_create(mood_panel);
    lv_label_set_text(s_lbl_emoji, "(^_^)");
    lv_obj_set_style_text_color(s_lbl_emoji, CLR_HAPPY, 0);
    lv_obj_set_style_text_font(s_lbl_emoji, &lv_font_montserrat_16, 0);
    lv_obj_align(s_lbl_emoji, LV_ALIGN_LEFT_MID, 8, 0);

    s_lbl_mood = lv_label_create(mood_panel);
    lv_label_set_text(s_lbl_mood, "Happy");
    lv_obj_set_style_text_color(s_lbl_mood, CLR_HAPPY, 0);
    lv_obj_set_style_text_font(s_lbl_mood, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_mood, LV_ALIGN_LEFT_MID, 80, 0);

    /* 共情消息区域 */
    lv_obj_t *msg_panel = lv_obj_create(s_screen_main);
    lv_obj_set_size(msg_panel, LCD_H_RES, 52);
    lv_obj_set_pos(msg_panel, 0, 187);
    lv_obj_set_style_bg_color(msg_panel, CLR_BG, 0);
    lv_obj_set_style_bg_opa(msg_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(msg_panel, 0, 0);
    lv_obj_set_style_radius(msg_panel, 0, 0);
    lv_obj_set_style_pad_all(msg_panel, 4, 0);

    s_lbl_message = lv_label_create(msg_panel);
    lv_label_set_text(s_lbl_message, "Say: Xiao Che Ban");
    lv_obj_set_style_text_color(s_lbl_message, CLR_TEXT, 0);
    lv_obj_set_style_text_font(s_lbl_message, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(s_lbl_message, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_lbl_message, LCD_H_RES - 16);
    lv_obj_align(s_lbl_message, LV_ALIGN_TOP_MID, 0, 4);

    /* 行程统计栏 */
    s_lbl_stats = lv_label_create(s_screen_main);
    lv_label_set_text(s_lbl_stats, "Max:0  Avg:0  Dist:0km");
    lv_obj_set_style_text_color(s_lbl_stats, CLR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_lbl_stats, &lv_font_montserrat_10, 0);
    lv_obj_align(s_lbl_stats, LV_ALIGN_BOTTOM_MID, 0, -2);

    ESP_LOGI(TAG, "主界面创建完成");
}

/* ============================================================
 * 通知弹窗定时器回调
 * ============================================================ */
static void notif_timer_cb(lv_timer_t *timer)
{
    if (s_notification_box != NULL) {
        lv_obj_del(s_notification_box);
        s_notification_box = NULL;
    }
    lv_timer_del(timer);
    s_notif_timer = NULL;
}

/* ============================================================
 * 公共接口实现
 * ============================================================ */

esp_err_t ui_manager_init(void)
{
    ESP_LOGI(TAG, "初始化UI管理器");

    if (!display_lock(5000)) {
        ESP_LOGE(TAG, "获取LVGL锁失败");
        return ESP_FAIL;
    }

    create_main_screen();
    lv_disp_load_scr(s_screen_main);
    s_current_screen = UI_SCREEN_MAIN;

    display_unlock();
    ESP_LOGI(TAG, "UI管理器初始化完成");
    return ESP_OK;
}

void ui_manager_switch_screen(ui_screen_t screen)
{
    if (screen == s_current_screen) return;
    if (!display_lock(1000)) return;
    /* 当前只有主界面，预留扩展 */
    display_unlock();
}

void ui_manager_update_speed(const speed_data_t *data)
{
    if (data == NULL || s_lbl_speed == NULL) return;
    if (!display_lock(100)) return;

    char speed_str[8];
    snprintf(speed_str, sizeof(speed_str), "%d", (int)data->speed_kmh);
    lv_label_set_text(s_lbl_speed, speed_str);

    lv_color_t color = get_speed_color(data->speed_kmh);
    lv_obj_set_style_text_color(s_lbl_speed, color, 0);

    int arc_val = (int)data->speed_kmh;
    if (arc_val > 140) arc_val = 140;
    lv_arc_set_value(s_arc_speed, arc_val);
    lv_obj_set_style_arc_color(s_arc_speed, color, LV_PART_INDICATOR);

    char stats_str[64];
    snprintf(stats_str, sizeof(stats_str),
             "Max:%.0f  Avg:%.0f  Dist:%.1fkm",
             data->max_speed_kmh,
             data->avg_speed_kmh,
             data->distance_m / 1000.0f);
    lv_label_set_text(s_lbl_stats, stats_str);

    display_unlock();
}

void ui_manager_update_empathy(const empathy_response_t *response)
{
    if (response == NULL || s_lbl_emoji == NULL) return;
    if (!display_lock(100)) return;

    lv_label_set_text(s_lbl_emoji, response->emoji);
    lv_label_set_text(s_lbl_mood, empathy_mood_to_name(response->mood));
    lv_label_set_text(s_lbl_message, response->message);

    lv_color_t mood_color = get_mood_color(response->mood);
    lv_obj_set_style_text_color(s_lbl_emoji, mood_color, 0);
    lv_obj_set_style_text_color(s_lbl_mood, mood_color, 0);

    display_unlock();
}

void ui_manager_update_voice_status(bool is_awake, bool is_speaking)
{
    if (s_lbl_voice == NULL) return;
    if (!display_lock(100)) return;

    if (is_speaking) {
        lv_label_set_text(s_lbl_voice, "SPK*");
        lv_obj_set_style_text_color(s_lbl_voice, CLR_VOICE_SPEAK, 0);
    } else if (is_awake) {
        lv_label_set_text(s_lbl_voice, "MIC*");
        lv_obj_set_style_text_color(s_lbl_voice, CLR_VOICE_ON, 0);
    } else {
        lv_label_set_text(s_lbl_voice, "MIC");
        lv_obj_set_style_text_color(s_lbl_voice, CLR_TEXT_DIM, 0);
    }

    display_unlock();
}

void ui_manager_show_notification(const char *title, const char *message,
                                   uint32_t duration_ms)
{
    if (!display_lock(200)) return;

    if (s_notification_box != NULL) {
        lv_obj_del(s_notification_box);
        s_notification_box = NULL;
    }
    if (s_notif_timer != NULL) {
        lv_timer_del(s_notif_timer);
        s_notif_timer = NULL;
    }

    s_notification_box = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_notification_box, 210, 80);
    lv_obj_align(s_notification_box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_notification_box, lv_color_hex(0x1A2A4A), 0);
    lv_obj_set_style_border_color(s_notification_box, CLR_ACCENT, 0);
    lv_obj_set_style_border_width(s_notification_box, 2, 0);
    lv_obj_set_style_radius(s_notification_box, 8, 0);

    lv_obj_t *lbl_t = lv_label_create(s_notification_box);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_color(lbl_t, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *lbl_m = lv_label_create(s_notification_box);
    lv_label_set_text(lbl_m, message);
    lv_obj_set_style_text_color(lbl_m, CLR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_m, &lv_font_montserrat_10, 0);
    lv_label_set_long_mode(lbl_m, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_m, 190);
    lv_obj_align(lbl_m, LV_ALIGN_BOTTOM_MID, 0, -4);

    if (duration_ms > 0) {
        s_notif_timer = lv_timer_create(notif_timer_cb, duration_ms, NULL);
        lv_timer_set_repeat_count(s_notif_timer, 1);
    }

    display_unlock();
}

void ui_manager_task(void *arg)
{
    ESP_LOGI(TAG, "UI任务启动");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(UI_UPDATE_INTERVAL_MS));
    }
}