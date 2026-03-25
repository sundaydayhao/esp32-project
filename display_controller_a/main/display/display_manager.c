#include "display_manager.h"
#include "lcd_driver.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "display_mgr";
static const TickType_t DISPLAY_LOCK_TIMEOUT = pdMS_TO_TICKS(500);

static menu_content_t s_content;
static SemaphoreHandle_t s_content_mutex;
static TaskHandle_t s_display_task_handle = NULL;
static volatile bool s_content_dirty = false;
static volatile bool s_screen_dirty = false;

typedef enum {
    DISPLAY_SCREEN_IDLE = 0,
    DISPLAY_SCREEN_MENU,
    DISPLAY_SCREEN_WIFI_SETUP,
    DISPLAY_SCREEN_CONNECTING,
    DISPLAY_SCREEN_SPLASH,
} display_screen_t;

static display_screen_t s_screen = DISPLAY_SCREEN_IDLE;
static char s_wifi_setup_ssid[MAX_WIFI_SSID_LEN];
static char s_wifi_setup_ip[32];
static char s_connecting_ssid[MAX_WIFI_SSID_LEN];

typedef struct {
    uint16_t header_bg;
    uint16_t header_fg;
    uint16_t menu_bg;
    uint16_t menu_fg;
    uint16_t price_fg;
    uint16_t footer_bg;
    uint16_t footer_fg;
    uint16_t divider;
} theme_colors_t;

static const theme_colors_t THEMES[4] = {
    {THEME0_HEADER_BG, THEME0_HEADER_FG, THEME0_MENU_BG, THEME0_MENU_FG,
     THEME0_PRICE_FG, THEME0_FOOTER_BG, THEME0_FOOTER_FG, 0xC000},
    {THEME1_HEADER_BG, THEME1_HEADER_FG, THEME1_MENU_BG, THEME1_MENU_FG,
     THEME1_PRICE_FG, THEME1_FOOTER_BG, THEME1_FOOTER_FG, 0x0010},
    {THEME2_HEADER_BG, THEME2_HEADER_FG, THEME2_MENU_BG, THEME2_MENU_FG,
     THEME2_PRICE_FG, THEME2_FOOTER_BG, THEME2_FOOTER_FG, 0x0300},
    {THEME3_HEADER_BG, THEME3_HEADER_FG, THEME3_MENU_BG, THEME3_MENU_FG,
     THEME3_PRICE_FG, THEME3_FOOTER_BG, THEME3_FOOTER_FG, 0xC900},
};

static void copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void notify_display_task(void) {
    if (s_display_task_handle != NULL) {
        xTaskNotifyGive(s_display_task_handle);
    }
}

static void draw_header(const menu_content_t *content, const theme_colors_t *theme) {
    lcd_fill_rect(0, 0, LCD_WIDTH, HEADER_HEIGHT, theme->header_bg);

    int name_y = (HEADER_HEIGHT - 32) / 2;
    lcd_draw_utf8(20, name_y, content->restaurant_name, theme->header_fg, theme->header_bg, 2);

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    int time_x = LCD_WIDTH - (int)(strlen(time_str) * 16) - 20;
    lcd_draw_string(time_x, name_y + 4, time_str, theme->header_fg, theme->header_bg, 2);

    lcd_draw_hline(0, HEADER_HEIGHT - 2, LCD_WIDTH, theme->divider);
    lcd_draw_hline(0, HEADER_HEIGHT - 1, LCD_WIDTH, theme->divider);
}

static void draw_menu_area(const menu_content_t *content, const theme_colors_t *theme) {
    int menu_y = HEADER_HEIGHT;
    int menu_h = MENU_AREA_HEIGHT;
    int col_w = LCD_WIDTH / MENU_COLS;
    int row_h = menu_h / MENU_ROWS;

    lcd_fill_rect(0, menu_y, LCD_WIDTH, menu_h, theme->menu_bg);

    for (int i = 0; i < MENU_COLS * MENU_ROWS; i++) {
        int col = i % MENU_COLS;
        int row = i / MENU_COLS;
        int cell_x = col * col_w;
        int cell_y = menu_y + row * row_h;
        uint16_t cell_bg = ((row + col) % 2 == 0) ? theme->menu_bg : (uint16_t)(theme->menu_bg - 0x0821);

        lcd_fill_rect(cell_x, cell_y, col_w, row_h, cell_bg);

        if (i < content->item_count) {
            int text_y = cell_y + (row_h - 32) / 2;

            lcd_draw_utf8(cell_x + 12, text_y, content->items[i].name, theme->menu_fg, cell_bg, 2);

            char price_str[16];
            snprintf(price_str, sizeof(price_str), "Y%s", content->items[i].price);
            int price_w = (int)strlen(price_str) * 16;
            int price_x = cell_x + col_w - price_w - 12;
            if (price_x > cell_x + 12) {
                lcd_draw_string(price_x, text_y + 4, price_str, theme->price_fg, cell_bg, 2);
            }
        }

        lcd_draw_rect(cell_x, cell_y, col_w, row_h, theme->divider);
    }
}

static void draw_footer(const menu_content_t *content, const theme_colors_t *theme) {
    int footer_y = LCD_HEIGHT - FOOTER_HEIGHT;

    lcd_fill_rect(0, footer_y, LCD_WIDTH, FOOTER_HEIGHT, theme->footer_bg);
    lcd_draw_hline(0, footer_y, LCD_WIDTH, theme->divider);
    lcd_draw_hline(0, footer_y + 1, LCD_WIDTH, theme->divider);

    int text_y = footer_y + (FOOTER_HEIGHT - 32) / 2;
    lcd_draw_utf8(20, text_y, content->promo_text, theme->footer_fg, theme->footer_bg, 2);
}

static void render_menu(const menu_content_t *content) {
    uint8_t theme_idx = content->theme < 4 ? content->theme : 0;
    const theme_colors_t *theme = &THEMES[theme_idx];

    draw_header(content, theme);
    draw_menu_area(content, theme);
    draw_footer(content, theme);
}

static void refresh_clock(const menu_content_t *content) {
    uint8_t theme_idx = content->theme < 4 ? content->theme : 0;
    const theme_colors_t *theme = &THEMES[theme_idx];

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

    int name_y = (HEADER_HEIGHT - 32) / 2;
    int time_x = LCD_WIDTH - (int)(strlen(time_str) * 16) - 20;
    lcd_fill_rect(time_x - 4, name_y, 120, 32, theme->header_bg);
    lcd_draw_string(time_x, name_y + 4, time_str, theme->header_fg, theme->header_bg, 2);
}

static void render_wifi_setup(const char *ap_ssid, const char *ap_ip) {
    lcd_clear(COLOR_BLACK);
    lcd_draw_string(LCD_WIDTH / 2 - 120, 60, "WiFi Setup", COLOR_WHITE, COLOR_BLACK, 2);
    lcd_draw_string(40, 140, "1. Connect phone to WiFi:", COLOR_LGRAY, COLOR_BLACK, 1);
    lcd_draw_string(60, 165, ap_ssid, COLOR_YELLOW, COLOR_BLACK, 2);
    lcd_draw_string(40, 220, "2. Password: 12345678", COLOR_LGRAY, COLOR_BLACK, 1);
    lcd_draw_string(40, 270, "3. Open browser, visit:", COLOR_LGRAY, COLOR_BLACK, 1);

    char url[48];
    snprintf(url, sizeof(url), "http://%s", ap_ip);
    lcd_draw_string(60, 295, url, COLOR_GREEN, COLOR_BLACK, 2);

    lcd_draw_string(40, 370, "Enter your restaurant WiFi", COLOR_LGRAY, COLOR_BLACK, 1);
    lcd_draw_string(40, 395, "SSID and password to connect.", COLOR_LGRAY, COLOR_BLACK, 1);
}

static void render_connecting(const char *ssid) {
    lcd_clear(COLOR_BLACK);
    lcd_draw_string(LCD_WIDTH / 2 - 80, 200, "Connecting to:", COLOR_WHITE, COLOR_BLACK, 1);
    lcd_draw_string(LCD_WIDTH / 2 - (int)(strlen(ssid) * 8), 230, ssid, COLOR_YELLOW, COLOR_BLACK, 2);
    lcd_draw_string(LCD_WIDTH / 2 - 60, 290, "Please wait...", COLOR_LGRAY, COLOR_BLACK, 1);
}

static void render_splash(void) {
    lcd_clear(COLOR_BLACK);
    lcd_fill_rect(0, 0, LCD_WIDTH, 8, COLOR_RED);
    lcd_fill_rect(0, LCD_HEIGHT - 8, LCD_WIDTH, 8, COLOR_RED);
    lcd_draw_string(LCD_WIDTH / 2 - 120, 160, "Menu Display", COLOR_WHITE, COLOR_BLACK, 3);
    lcd_draw_string(LCD_WIDTH / 2 - 80, 220, "Smart Restaurant", COLOR_LGRAY, COLOR_BLACK, 1);
    lcd_draw_string(LCD_WIDTH / 2 - 40, 250, FIRMWARE_VERSION, COLOR_GRAY, COLOR_BLACK, 1);
}

static void display_task(void *arg) {
    ESP_LOGI(TAG, "Display task started");

    while (1) {
        bool timed_out = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(DISPLAY_REFRESH_MS)) == 0;
        bool render_full_screen = false;
        bool refresh_menu_clock_now = false;
        display_screen_t screen = DISPLAY_SCREEN_IDLE;
        menu_content_t content_snapshot = {0};
        char wifi_setup_ssid[MAX_WIFI_SSID_LEN] = {0};
        char wifi_setup_ip[sizeof(s_wifi_setup_ip)] = {0};
        char connecting_ssid[MAX_WIFI_SSID_LEN] = {0};

        if (xSemaphoreTake(s_content_mutex, DISPLAY_LOCK_TIMEOUT) != pdTRUE) {
            continue;
        }

        screen = s_screen;
        if (screen == DISPLAY_SCREEN_MENU) {
            memcpy(&content_snapshot, &s_content, sizeof(content_snapshot));
            if (s_content_dirty || s_screen_dirty) {
                render_full_screen = true;
                s_content_dirty = false;
                s_screen_dirty = false;
            } else if (timed_out) {
                refresh_menu_clock_now = true;
            }
        } else {
            copy_string(wifi_setup_ssid, sizeof(wifi_setup_ssid), s_wifi_setup_ssid);
            copy_string(wifi_setup_ip, sizeof(wifi_setup_ip), s_wifi_setup_ip);
            copy_string(connecting_ssid, sizeof(connecting_ssid), s_connecting_ssid);

            if (s_screen_dirty) {
                render_full_screen = true;
                s_screen_dirty = false;
            }
        }

        xSemaphoreGive(s_content_mutex);

        if (!render_full_screen && !refresh_menu_clock_now) {
            continue;
        }

        switch (screen) {
        case DISPLAY_SCREEN_MENU:
            if (render_full_screen) {
                render_menu(&content_snapshot);
            } else {
                refresh_clock(&content_snapshot);
            }
            break;
        case DISPLAY_SCREEN_WIFI_SETUP:
            render_wifi_setup(wifi_setup_ssid, wifi_setup_ip);
            break;
        case DISPLAY_SCREEN_CONNECTING:
            render_connecting(connecting_ssid);
            break;
        case DISPLAY_SCREEN_SPLASH:
            render_splash();
            break;
        case DISPLAY_SCREEN_IDLE:
        default:
            break;
        }
    }
}

void display_manager_init(void) {
    s_content_mutex = xSemaphoreCreateMutex();
    s_screen = DISPLAY_SCREEN_IDLE;
    s_content_dirty = false;
    s_screen_dirty = false;

    lcd_init();
    ESP_LOGI(TAG, "LCD initialized");

    xTaskCreatePinnedToCore(
        display_task,
        "display_task",
        DISPLAY_TASK_STACK_SIZE,
        NULL,
        DISPLAY_TASK_PRIORITY,
        &s_display_task_handle,
        DISPLAY_TASK_CORE
    );
}

void display_manager_update(const menu_content_t *content) {
    if (xSemaphoreTake(s_content_mutex, DISPLAY_LOCK_TIMEOUT) != pdTRUE) {
        return;
    }

    memcpy(&s_content, content, sizeof(menu_content_t));
    s_content_dirty = true;
    s_screen = DISPLAY_SCREEN_MENU;
    s_screen_dirty = true;
    xSemaphoreGive(s_content_mutex);

    notify_display_task();
    ESP_LOGI(TAG, "Content updated: %s (%d items)", content->restaurant_name, content->item_count);
}

void display_manager_show_wifi_setup(const char *ap_ssid, const char *ap_ip) {
    if (xSemaphoreTake(s_content_mutex, DISPLAY_LOCK_TIMEOUT) != pdTRUE) {
        return;
    }

    s_screen = DISPLAY_SCREEN_WIFI_SETUP;
    copy_string(s_wifi_setup_ssid, sizeof(s_wifi_setup_ssid), ap_ssid);
    copy_string(s_wifi_setup_ip, sizeof(s_wifi_setup_ip), ap_ip);
    s_screen_dirty = true;
    xSemaphoreGive(s_content_mutex);

    notify_display_task();
}

void display_manager_show_connecting(const char *ssid) {
    if (xSemaphoreTake(s_content_mutex, DISPLAY_LOCK_TIMEOUT) != pdTRUE) {
        return;
    }

    s_screen = DISPLAY_SCREEN_CONNECTING;
    copy_string(s_connecting_ssid, sizeof(s_connecting_ssid), ssid);
    s_screen_dirty = true;
    xSemaphoreGive(s_content_mutex);

    notify_display_task();
}

void display_manager_show_splash(void) {
    if (xSemaphoreTake(s_content_mutex, DISPLAY_LOCK_TIMEOUT) != pdTRUE) {
        return;
    }

    s_screen = DISPLAY_SCREEN_SPLASH;
    s_screen_dirty = true;
    xSemaphoreGive(s_content_mutex);

    notify_display_task();
    vTaskDelay(pdMS_TO_TICKS(1500));
}
