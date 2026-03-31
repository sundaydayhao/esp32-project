#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "lvgl_display.h"
#include "gif/lvgl_gif.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <font_emoji.h>

#include <atomic>
#include <array>
#include <memory>
#include <string>

#define PREVIEW_IMAGE_DURATION_MS 5000

class LvglTheme;

class LcdDisplay : public LvglDisplay {
protected:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    
    lv_draw_buf_t draw_buf_;
    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* bottom_bar_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* emoji_label_ = nullptr;
    lv_obj_t* emoji_image_ = nullptr;
    std::unique_ptr<LvglGif> gif_controller_ = nullptr;
    lv_obj_t* emoji_box_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    lv_obj_t* home_panel_ = nullptr;
    lv_obj_t* home_clock_card_ = nullptr;
    lv_obj_t* home_clock_face_ = nullptr;
    lv_obj_t* home_clock_digital_label_ = nullptr;
    lv_obj_t* home_date_label_ = nullptr;
    lv_obj_t* home_weather_card_ = nullptr;
    lv_obj_t* home_weather_location_label_ = nullptr;
    lv_obj_t* home_weather_temperature_label_ = nullptr;
    lv_obj_t* home_weather_condition_label_ = nullptr;
    lv_obj_t* home_clock_tick_lines_[12] = {};
    lv_obj_t* home_clock_hour_hand_ = nullptr;
    lv_obj_t* home_clock_minute_hand_ = nullptr;
    lv_obj_t* home_clock_second_hand_ = nullptr;
    lv_obj_t* home_clock_center_dot_ = nullptr;
    std::array<std::array<lv_point_precise_t, 2>, 12> home_clock_tick_points_{};
    std::array<lv_point_precise_t, 2> home_clock_hour_points_{};
    std::array<lv_point_precise_t, 2> home_clock_minute_points_{};
    std::array<lv_point_precise_t, 2> home_clock_second_points_{};
    esp_timer_handle_t preview_timer_ = nullptr;
    std::unique_ptr<LvglImage> preview_image_cached_ = nullptr;
    bool hide_subtitle_ = false;  // Control whether to hide chat messages/subtitles
    bool chat_active_ = false;
    time_t last_home_clock_update_ = 0;
    std::string weather_condition_ = "天气获取中";
    std::string weather_temperature_ = "--";
    std::string weather_location_ = "本地";

    void InitializeLcdThemes();
    void CreateHomePanel(LvglTheme* theme);
    void CreateChatPanel(LvglTheme* theme);
    void UpdateActivePanelVisibility();
    void UpdateHomeClock();
    void RefreshWeatherDisplay();
    void AppendChatBubble(const char* role, const char* content, LvglTheme* theme);
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

protected:
    // Add protected constructor
    LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height);
    
public:
    ~LcdDisplay();
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetChatActive(bool active) override;
    virtual void SetWeather(const char* condition, const char* temperature, const char* location = nullptr) override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void SetupUI() override;
    // Add theme switching function
    virtual void SetTheme(Theme* theme) override;
    virtual void UpdateStatusBar(bool update_all = false) override;
    
    // Set whether to hide chat messages/subtitles
    void SetHideSubtitle(bool hide);
};

// SPI LCD display
class SpiLcdDisplay : public LcdDisplay {
public:
    SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy);
};

// RGB LCD display
class RgbLcdDisplay : public LcdDisplay {
public:
    RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                  int width, int height, int offset_x, int offset_y,
                  bool mirror_x, bool mirror_y, bool swap_xy);
};

// MIPI LCD display
class MipiLcdDisplay : public LcdDisplay {
public:
    MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy);
};

#endif // LCD_DISPLAY_H
