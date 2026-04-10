/**
 * @file ui_music_player.c
 * @brief Music player UI implementation
 */
#include "ui_music_player.h"

#include <stdio.h>
#include <string.h>

#include "audio/audio_player.h"
#include "config.h"
#include "esp_log.h"
#include "lvgl.h"

LV_FONT_DECLARE(ui_font_noto_sc_16);

typedef struct {
    lv_obj_t *panel;
    lv_obj_t *title_label;
    lv_obj_t *track_label;
    lv_obj_t *time_label;
    lv_obj_t *progress_bar;
    lv_obj_t *btn_prev;
    lv_obj_t *btn_play;
    lv_obj_t *btn_next;
    lv_obj_t *volume_slider;
    lv_obj_t *status_label;
    lv_obj_t *album_art;

    char title_text[64];
    char track_text[96];
    char time_text[32];
    char status_text[64];
} music_ui_t;

static music_ui_t s_music_ui = {0};

static void style_music_panel(lv_obj_t *obj)
{
    lv_obj_set_style_radius(obj, 24, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x20384E), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x0D1A28), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x1A2A3F), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 20, 0);
}

static void style_music_button(lv_obj_t *obj, lv_color_t color)
{
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x15273A), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

static void style_small_label(lv_obj_t *obj, lv_color_t color)
{
    lv_obj_set_style_text_font(obj, &ui_font_noto_sc_16, 0);
    lv_obj_set_style_text_color(obj, color, 0);
}

static void btn_prev_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG_AUDIO, "Previous button clicked");
        audio_player_prev();
        ui_music_player_update();
    }
}

static void btn_play_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        audio_player_status_t status;
        if (audio_player_get_status(&status) == ESP_OK) {
            if (status.state == AUDIO_STATE_PLAYING) {
                ESP_LOGI(TAG_AUDIO, "Pause button clicked");
                audio_player_pause();
            } else {
                ESP_LOGI(TAG_AUDIO, "Play button clicked");
                audio_player_play();
            }
            ui_music_player_update();
        }
    }
}

static void btn_next_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG_AUDIO, "Next button clicked");
        audio_player_next();
        ui_music_player_update();
    }
}

static void volume_slider_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *slider = lv_event_get_target(e);
        int32_t value = lv_slider_get_value(slider);
        audio_player_set_volume((uint8_t)value);
        ESP_LOGI(TAG_AUDIO, "Volume changed to %d%%", (int)value);
    }
}

esp_err_t ui_music_player_create(lv_obj_t *parent)
{
    if (!parent) {
        return ESP_ERR_INVALID_ARG;
    }

    // Create main panel
    s_music_ui.panel = lv_obj_create(parent);
    lv_obj_set_size(s_music_ui.panel, LCD_H_RES - 32, 140);
    lv_obj_set_pos(s_music_ui.panel, 16, LCD_V_RES - 156);
    style_music_panel(s_music_ui.panel);

    // Title
    s_music_ui.title_label = lv_label_create(s_music_ui.panel);
    lv_obj_set_pos(s_music_ui.title_label, 0, 0);
    style_small_label(s_music_ui.title_label, lv_color_hex(0x68C1FF));
    lv_label_set_text(s_music_ui.title_label, "音乐播放器");

    // Album art placeholder
    s_music_ui.album_art = lv_obj_create(s_music_ui.panel);
    lv_obj_set_size(s_music_ui.album_art, 80, 80);
    lv_obj_set_pos(s_music_ui.album_art, 0, 30);
    lv_obj_set_style_radius(s_music_ui.album_art, 12, 0);
    lv_obj_set_style_border_width(s_music_ui.album_art, 2, 0);
    lv_obj_set_style_border_color(s_music_ui.album_art, lv_color_hex(0x3A5A7A), 0);
    lv_obj_set_style_bg_color(s_music_ui.album_art, lv_color_hex(0x1A3A5A), 0);
    lv_obj_set_style_bg_opa(s_music_ui.album_art, LV_OPA_COVER, 0);

    // Music note icon (simple representation)
    lv_obj_t *note_icon = lv_label_create(s_music_ui.album_art);
    lv_obj_center(note_icon);
    lv_obj_set_style_text_font(note_icon, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(note_icon, lv_color_hex(0x68C1FF), 0);
    lv_label_set_text(note_icon, LV_SYMBOL_AUDIO);

    // Track info
    s_music_ui.track_label = lv_label_create(s_music_ui.panel);
    lv_obj_set_size(s_music_ui.track_label, 400, LV_SIZE_CONTENT);
    lv_obj_set_pos(s_music_ui.track_label, 95, 30);
    lv_obj_set_style_text_font(s_music_ui.track_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_music_ui.track_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(s_music_ui.track_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // Time display
    s_music_ui.time_label = lv_label_create(s_music_ui.panel);
    lv_obj_set_pos(s_music_ui.time_label, 95, 58);
    style_small_label(s_music_ui.time_label, lv_color_hex(0xA7B5C8));

    // Progress bar
    s_music_ui.progress_bar = lv_bar_create(s_music_ui.panel);
    lv_obj_set_size(s_music_ui.progress_bar, 400, 8);
    lv_obj_set_pos(s_music_ui.progress_bar, 95, 82);
    lv_obj_set_style_bg_color(s_music_ui.progress_bar, lv_color_hex(0x1A3A5A), 0);
    lv_obj_set_style_bg_opa(s_music_ui.progress_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_music_ui.progress_bar, 4, 0);
    lv_obj_set_style_bg_color(s_music_ui.progress_bar, lv_color_hex(0x68C1FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_music_ui.progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(s_music_ui.progress_bar, 0, 100);
    lv_bar_set_value(s_music_ui.progress_bar, 0, LV_ANIM_OFF);

    // Control buttons
    int btn_y = 30;
    int btn_start_x = 520;
    int btn_spacing = 70;

    // Previous button
    s_music_ui.btn_prev = lv_btn_create(s_music_ui.panel);
    lv_obj_set_size(s_music_ui.btn_prev, 50, 50);
    lv_obj_set_pos(s_music_ui.btn_prev, btn_start_x, btn_y);
    style_music_button(s_music_ui.btn_prev, lv_color_hex(0x68C1FF));
    lv_obj_add_event_cb(s_music_ui.btn_prev, btn_prev_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *prev_label = lv_label_create(s_music_ui.btn_prev);
    lv_obj_center(prev_label);
    lv_obj_set_style_text_font(prev_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(prev_label, lv_color_hex(0x68C1FF), 0);
    lv_label_set_text(prev_label, LV_SYMBOL_PREV);

    // Play/Pause button
    s_music_ui.btn_play = lv_btn_create(s_music_ui.panel);
    lv_obj_set_size(s_music_ui.btn_play, 60, 60);
    lv_obj_set_pos(s_music_ui.btn_play, btn_start_x + btn_spacing, btn_y - 5);
    style_music_button(s_music_ui.btn_play, lv_color_hex(0xFFB057));
    lv_obj_add_event_cb(s_music_ui.btn_play, btn_play_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *play_label = lv_label_create(s_music_ui.btn_play);
    lv_obj_center(play_label);
    lv_obj_set_style_text_font(play_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(play_label, lv_color_hex(0xFFB057), 0);
    lv_label_set_text(play_label, LV_SYMBOL_PLAY);

    // Next button
    s_music_ui.btn_next = lv_btn_create(s_music_ui.panel);
    lv_obj_set_size(s_music_ui.btn_next, 50, 50);
    lv_obj_set_pos(s_music_ui.btn_next, btn_start_x + btn_spacing * 2, btn_y);
    style_music_button(s_music_ui.btn_next, lv_color_hex(0x68C1FF));
    lv_obj_add_event_cb(s_music_ui.btn_next, btn_next_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *next_label = lv_label_create(s_music_ui.btn_next);
    lv_obj_center(next_label);
    lv_obj_set_style_text_font(next_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(next_label, lv_color_hex(0x68C1FF), 0);
    lv_label_set_text(next_label, LV_SYMBOL_NEXT);

    // Volume slider
    s_music_ui.volume_slider = lv_slider_create(s_music_ui.panel);
    lv_obj_set_size(s_music_ui.volume_slider, 120, 10);
    lv_obj_set_pos(s_music_ui.volume_slider, btn_start_x + 10, btn_y + 65);
    lv_obj_set_style_bg_color(s_music_ui.volume_slider, lv_color_hex(0x1A3A5A), 0);
    lv_obj_set_style_bg_opa(s_music_ui.volume_slider, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_music_ui.volume_slider, lv_color_hex(0x68C1FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_music_ui.volume_slider, lv_color_hex(0xFFB057), LV_PART_KNOB);
    lv_slider_set_range(s_music_ui.volume_slider, 0, 100);
    lv_slider_set_value(s_music_ui.volume_slider, 50, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_music_ui.volume_slider, volume_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *vol_icon = lv_label_create(s_music_ui.panel);
    lv_obj_set_pos(vol_icon, btn_start_x - 15, btn_y + 60);
    lv_obj_set_style_text_font(vol_icon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(vol_icon, lv_color_hex(0x68C1FF), 0);
    lv_label_set_text(vol_icon, LV_SYMBOL_VOLUME_MAX);

    // Status label
    s_music_ui.status_label = lv_label_create(s_music_ui.panel);
    lv_obj_set_pos(s_music_ui.status_label, 95, 95);
    style_small_label(s_music_ui.status_label, lv_color_hex(0x7D92A9));

    // Initial update
    ui_music_player_update();

    ESP_LOGI(TAG_AUDIO, "Music player UI created");
    return ESP_OK;
}

void ui_music_player_update(void)
{
    audio_player_status_t status;

    if (audio_player_get_status(&status) != ESP_OK) {
        return;
    }

    // Update track name
    if (status.total_tracks > 0) {
        snprintf(s_music_ui.track_text, sizeof(s_music_ui.track_text),
                 "%s", status.current_info.title);
    } else {
        snprintf(s_music_ui.track_text, sizeof(s_music_ui.track_text),
                 "无音乐文件");
    }

    if (s_music_ui.track_label && strcmp(lv_label_get_text(s_music_ui.track_label), s_music_ui.track_text) != 0) {
        lv_label_set_text(s_music_ui.track_label, s_music_ui.track_text);
    }

    // Update time
    uint32_t pos_min = status.current_info.position_sec / 60;
    uint32_t pos_sec = status.current_info.position_sec % 60;
    uint32_t dur_min = status.current_info.duration_sec / 60;
    uint32_t dur_sec = status.current_info.duration_sec % 60;

    snprintf(s_music_ui.time_text, sizeof(s_music_ui.time_text),
             "%02u:%02u / %02u:%02u",
             (unsigned)pos_min, (unsigned)pos_sec,
             (unsigned)dur_min, (unsigned)dur_sec);

    if (s_music_ui.time_label && strcmp(lv_label_get_text(s_music_ui.time_label), s_music_ui.time_text) != 0) {
        lv_label_set_text(s_music_ui.time_label, s_music_ui.time_text);
    }

    // Update progress bar
    if (s_music_ui.progress_bar && status.current_info.duration_sec > 0) {
        int32_t progress = (status.current_info.position_sec * 100) / status.current_info.duration_sec;
        lv_bar_set_value(s_music_ui.progress_bar, progress, LV_ANIM_OFF);
    }

    // Update play/pause button
    if (s_music_ui.btn_play) {
        lv_obj_t *play_label = lv_obj_get_child(s_music_ui.btn_play, 0);
        if (play_label) {
            const char *symbol = (status.state == AUDIO_STATE_PLAYING) ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
            lv_label_set_text(play_label, symbol);
        }
    }

    // Update status text
    if (audio_player_is_sd_mounted()) {
        snprintf(s_music_ui.status_text, sizeof(s_music_ui.status_text),
                 "曲目 %d/%d  音量 %d%%",
                 status.current_track + 1, status.total_tracks, status.volume);
    } else {
        snprintf(s_music_ui.status_text, sizeof(s_music_ui.status_text),
                 "SD卡未插入");
    }

    if (s_music_ui.status_label && strcmp(lv_label_get_text(s_music_ui.status_label), s_music_ui.status_text) != 0) {
        lv_label_set_text(s_music_ui.status_label, s_music_ui.status_text);
    }
}

lv_obj_t *ui_music_player_get_panel(void)
{
    return s_music_ui.panel;
}