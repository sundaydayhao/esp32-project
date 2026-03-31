/**
 * @file blind_spot_ui.c
 * @brief 车辆盲区视觉显示 UI 模块
 *
 * 负责将摄像头画面渲染到 LCD 屏幕，支持以下显示模式：
 *
 * 1. 分屏模式 (SPLIT)：
 *    ┌─────────┬─────────┐
 *    │  LEFT   │  RIGHT  │
 *    │  左摄像头 │  右摄像头 │
 *    │  (镜像)  │  (正常)  │
 *    └─────────┴─────────┘
 *
 * 2. 左全屏模式 (LEFT_FULL)：左转向灯激活时
 *    ┌─────────────────────┐
 *    │                     │
 *    │    LEFT CAMERA      │
 *    │    (左侧盲区全屏)    │
 *    │                     │
 *    └─────────────────────┘
 *
 * 3. 右全屏模式 (RIGHT_FULL)：右转向灯激活时
 *    ┌─────────────────────┐
 *    │                     │
 *    │    RIGHT CAMERA     │
 *    │    (右侧盲区全屏)    │
 *    │                     │
 *    └─────────────────────┘
 *
 * 警告叠加：
 *   - 绿色边框：安全
 *   - 黄色边框：注意 (有物体)
 *   - 红色边框 + 闪烁：危险 (物体很近)
 */

#include "blind_spot_ui.h"
#include "lcd_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "blind_ui";

/* 状态栏高度 (像素) */
#define STATUSBAR_HEIGHT   20
/* 分屏分割线宽度 */
#define SPLIT_LINE_WIDTH   2
/* 警告边框厚度 */
#define WARN_BORDER_THICK  4
/* 危险警告闪烁周期 (us) */
#define BLINK_PERIOD_US    500000ULL

/* ============================================================
 * 内部状态
 * ============================================================ */
static struct {
    blind_spot_ui_config_t config;
    display_mode_t         mode;
    bool                   initialized;
    uint64_t               last_blink_us;
    bool                   blink_state;
} s_ui = {
    .mode        = DISPLAY_MODE_IDLE,
    .initialized = false,
    .blink_state = true,
};

/* ============================================================
 * 内部辅助函数
 * ============================================================ */

/**
 * @brief 计算分屏时单个摄像头画面的目标区域
 */
static void get_split_rect(camera_id_t cam, int *x, int *y, int *w, int *h)
{
    int sw     = s_ui.config.screen_width;
    int sh     = s_ui.config.screen_height - STATUSBAR_HEIGHT;
    int half_w = (sw - SPLIT_LINE_WIDTH) / 2;

    *y = 0;
    *h = sh;
    *w = half_w;
    *x = (cam == CAMERA_LEFT) ? 0 : (half_w + SPLIT_LINE_WIDTH);
}

/**
 * @brief 获取警告颜色
 */
static uint16_t get_warn_color(warn_level_t level)
{
    switch (level) {
        case WARN_LEVEL_CAUTION: return LCD_COLOR_YELLOW;
        case WARN_LEVEL_DANGER:  return LCD_COLOR_RED;
        default:                 return LCD_COLOR_GREEN;
    }
}

/**
 * @brief 更新闪烁状态
 * @return true 当前应显示 (亮), false 当前应隐藏 (灭)
 */
static bool update_blink(void)
{
    uint64_t now = esp_timer_get_time();
    if (now - s_ui.last_blink_us >= BLINK_PERIOD_US) {
        s_ui.blink_state    = !s_ui.blink_state;
        s_ui.last_blink_us  = now;
    }
    return s_ui.blink_state;
}

/**
 * @brief 绘制摄像头标签 (左上角小标签)
 */
static void draw_camera_label(int x, int y, camera_id_t cam_id, warn_level_t warn_level)
{
    const char *label = (cam_id == CAMERA_LEFT) ? "L" : "R";
    uint16_t bg = get_warn_color(warn_level);
    uint16_t fg = LCD_COLOR_BLACK;
    /* 绘制背景色块 */
    lcd_fill_rect(x + 2, y + 2, 16, 18, bg);
    /* 绘制字母 */
    lcd_draw_char(x + 4, y + 3, label[0], fg, bg, 1);
}

/* ============================================================
 * 公共接口实现
 * ============================================================ */

esp_err_t blind_spot_ui_init(const blind_spot_ui_config_t *config)
{
    if (s_ui.initialized) {
        ESP_LOGW(TAG, "盲区UI已初始化");
        return ESP_OK;
    }
    if (!config) return ESP_ERR_INVALID_ARG;

    memcpy(&s_ui.config, config, sizeof(blind_spot_ui_config_t));
    s_ui.mode           = DISPLAY_MODE_IDLE;
    s_ui.last_blink_us  = esp_timer_get_time();
    s_ui.blink_state    = true;
    s_ui.initialized    = true;

    ESP_LOGI(TAG, "盲区UI初始化完成 (屏幕 %dx%d, 摄像头帧 %dx%d)",
             config->screen_width, config->screen_height,
             config->cam_frame_width, config->cam_frame_height);
    return ESP_OK;
}

void blind_spot_ui_deinit(void)
{
    s_ui.initialized = false;
}

void blind_spot_ui_set_mode(display_mode_t mode)
{
    if (s_ui.mode != mode) {
        ESP_LOGI(TAG, "显示模式切换: %d -> %d", s_ui.mode, mode);
        s_ui.mode = mode;
        /* 切换模式时清屏 */
        lcd_clear(LCD_COLOR_BLACK);
    }
}

display_mode_t blind_spot_ui_get_mode(void)
{
    return s_ui.mode;
}

void blind_spot_ui_update_by_turn_signal(turn_signal_state_t turn_state)
{
    switch (turn_state) {
        case TURN_SIGNAL_LEFT:
            blind_spot_ui_set_mode(DISPLAY_MODE_LEFT_FULL);
            break;
        case TURN_SIGNAL_RIGHT:
            blind_spot_ui_set_mode(DISPLAY_MODE_RIGHT_FULL);
            break;
        case TURN_SIGNAL_BOTH:
            /* 双闪时显示分屏 */
            blind_spot_ui_set_mode(DISPLAY_MODE_SPLIT);
            break;
        case TURN_SIGNAL_NONE:
        default:
            /* 无转向时恢复分屏 */
            blind_spot_ui_set_mode(DISPLAY_MODE_SPLIT);
            break;
    }
}

void blind_spot_ui_render_split(const camera_frame_t *left_frame,
                                 const camera_frame_t *right_frame)
{
    if (!s_ui.initialized) return;

    int lx, ly, lw, lh;
    int rx, ry, rw, rh;
    get_split_rect(CAMERA_LEFT,  &lx, &ly, &lw, &lh);
    get_split_rect(CAMERA_RIGHT, &rx, &ry, &rw, &rh);

    /* ---- 渲染左摄像头 ---- */
    if (left_frame && left_frame->buf) {
        lcd_draw_bitmap_scaled(lx, ly, lw, lh,
                               (const uint16_t *)left_frame->buf,
                               left_frame->width, left_frame->height);
    } else {
        /* 无信号：显示灰色背景 + 提示 */
        lcd_fill_rect(lx, ly, lw, lh, LCD_COLOR_DARKGRAY);
        lcd_draw_string(lx + lw/2 - 16, ly + lh/2 - 8,
                        "NO SIG", LCD_COLOR_WHITE, LCD_COLOR_DARKGRAY, 1);
    }

    /* ---- 渲染右摄像头 ---- */
    if (right_frame && right_frame->buf) {
        lcd_draw_bitmap_scaled(rx, ry, rw, rh,
                               (const uint16_t *)right_frame->buf,
                               right_frame->width, right_frame->height);
    } else {
        lcd_fill_rect(rx, ry, rw, rh, LCD_COLOR_DARKGRAY);
        lcd_draw_string(rx + rw/2 - 16, ry + rh/2 - 8,
                        "NO SIG", LCD_COLOR_WHITE, LCD_COLOR_DARKGRAY, 1);
    }

    /* ---- 绘制分割线 ---- */
    lcd_fill_rect(lw, 0, SPLIT_LINE_WIDTH, lh, LCD_COLOR_GRAY);

    /* ---- 警告分析与叠加 ---- */
    if (s_ui.config.warning_enabled) {
        warn_level_t lwarn = left_frame  ? blind_spot_ui_analyze_frame(left_frame)  : WARN_LEVEL_NONE;
        warn_level_t rwarn = right_frame ? blind_spot_ui_analyze_frame(right_frame) : WARN_LEVEL_NONE;

        blind_spot_ui_draw_warning(lx, ly, lw, lh, lwarn);
        blind_spot_ui_draw_warning(rx, ry, rw, rh, rwarn);

        /* 绘制摄像头标签 */
        draw_camera_label(lx, ly, CAMERA_LEFT,  lwarn);
        draw_camera_label(rx, ry, CAMERA_RIGHT, rwarn);
    }
}

void blind_spot_ui_render_fullscreen(const camera_frame_t *frame,
                                      camera_id_t camera_id)
{
    if (!s_ui.initialized) return;

    int sw = s_ui.config.screen_width;
    int sh = s_ui.config.screen_height - STATUSBAR_HEIGHT;

    if (frame && frame->buf) {
        lcd_draw_bitmap_scaled(0, 0, sw, sh,
                               (const uint16_t *)frame->buf,
                               frame->width, frame->height);
    } else {
        lcd_fill_rect(0, 0, sw, sh, LCD_COLOR_DARKGRAY);
        lcd_draw_string(sw/2 - 24, sh/2 - 8,
                        "NO SIGNAL", LCD_COLOR_WHITE, LCD_COLOR_DARKGRAY, 1);
    }

    /* ---- 警告叠加 ---- */
    if (s_ui.config.warning_enabled && frame) {
        warn_level_t warn = blind_spot_ui_analyze_frame(frame);
        blind_spot_ui_draw_warning(0, 0, sw, sh, warn);
        draw_camera_label(0, 0, camera_id, warn);

        /* 全屏模式下显示方向提示 */
        const char *dir_str = (camera_id == CAMERA_LEFT) ? "< LEFT" : "RIGHT >";
        uint16_t dir_color  = (camera_id == CAMERA_LEFT) ? LCD_COLOR_CYAN : LCD_COLOR_ORANGE;
        lcd_draw_string(sw/2 - 24, sh - 24, dir_str, dir_color, LCD_COLOR_BLACK, 1);
    }
}

void blind_spot_ui_render_idle(void)
{
    if (!s_ui.initialized) return;

    int sw = s_ui.config.screen_width;
    int sh = s_ui.config.screen_height;

    lcd_fill_rect(0, 0, sw, sh, LCD_COLOR_BLACK);

    /* 标题 */
    lcd_draw_string(sw/2 - 48, sh/2 - 30,
                    "BLIND SPOT", LCD_COLOR_WHITE, LCD_COLOR_BLACK, 2);
    lcd_draw_string(sw/2 - 40, sh/2 + 10,
                    "MONITOR", LCD_COLOR_CYAN, LCD_COLOR_BLACK, 2);

    /* 提示 */
    lcd_draw_string(sw/2 - 56, sh/2 + 40,
                    "INITIALIZING...", LCD_COLOR_GRAY, LCD_COLOR_BLACK, 1);

    /* 边框装饰 */
    lcd_draw_rect(4, 4, sw - 8, sh - 8, 2, LCD_COLOR_DARKGRAY);
}

warn_level_t blind_spot_ui_analyze_frame(const camera_frame_t *frame)
{
    if (!frame || !frame->buf || frame->width == 0 || frame->height == 0) {
        return WARN_LEVEL_NONE;
    }

    /*
     * 简单的盲区物体检测算法：
     * 分析画面中心区域的像素亮度变化（边缘密度）。
     * 高边缘密度 = 有物体存在。
     *
     * 检测区域：画面中央 60% 宽度、80% 高度的区域
     * 采样步长：每 4 像素采样一次（降低计算量）
     */

    const uint16_t *pixels = (const uint16_t *)frame->buf;
    uint32_t w = frame->width;
    uint32_t h = frame->height;

    /* 检测区域 */
    uint32_t x_start = w * 2 / 10;
    uint32_t x_end   = w * 8 / 10;
    uint32_t y_start = h * 1 / 10;
    uint32_t y_end   = h * 9 / 10;

    uint32_t edge_count  = 0;
    uint32_t total_count = 0;
    int      step        = 4;

    for (uint32_t y = y_start; y < y_end - step; y += step) {
        for (uint32_t x = x_start; x < x_end - step; x += step) {
            uint16_t p00 = pixels[y * w + x];
            uint16_t p10 = pixels[y * w + x + step];
            uint16_t p01 = pixels[(y + step) * w + x];

            /* 提取绿色通道 (RGB565 中精度最高) */
            int g00 = (p00 >> 5) & 0x3F;
            int g10 = (p10 >> 5) & 0x3F;
            int g01 = (p01 >> 5) & 0x3F;

            /* 计算梯度 */
            int dx = g10 - g00;
            int dy = g01 - g00;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            int grad = dx + dy;

            if (grad > 8) { /* 边缘阈值 */
                edge_count++;
            }
            total_count++;
        }
    }

    if (total_count == 0) return WARN_LEVEL_NONE;

    /* 计算边缘密度百分比 */
    uint32_t edge_pct = (edge_count * 100) / total_count;

    if (edge_pct >= (uint32_t)s_ui.config.danger_threshold_pct) {
        return WARN_LEVEL_DANGER;
    } else if (edge_pct >= (uint32_t)s_ui.config.warn_threshold_pct) {
        return WARN_LEVEL_CAUTION;
    }
    return WARN_LEVEL_NONE;
}

void blind_spot_ui_draw_warning(int x, int y, int w, int h,
                                 warn_level_t warn_level)
{
    if (warn_level == WARN_LEVEL_NONE) {
        /* 安全：绘制细绿色边框 */
        lcd_draw_rect(x, y, w, h, 2, LCD_COLOR_GREEN);
        return;
    }

    uint16_t color = get_warn_color(warn_level);

    if (warn_level == WARN_LEVEL_DANGER) {
        /* 危险：闪烁红色粗边框 */
        bool show = update_blink();
        if (show) {
            lcd_draw_rect(x, y, w, h, WARN_BORDER_THICK, color);
            /* 四角加强显示 */
            int corner = 20;
            /* 左上角 */
            lcd_fill_rect(x, y, corner, WARN_BORDER_THICK, color);
            lcd_fill_rect(x, y, WARN_BORDER_THICK, corner, color);
            /* 右上角 */
            lcd_fill_rect(x + w - corner, y, corner, WARN_BORDER_THICK, color);
            lcd_fill_rect(x + w - WARN_BORDER_THICK, y, WARN_BORDER_THICK, corner, color);
            /* 左下角 */
            lcd_fill_rect(x, y + h - WARN_BORDER_THICK, corner, WARN_BORDER_THICK, color);
            lcd_fill_rect(x, y + h - corner, WARN_BORDER_THICK, corner, color);
            /* 右下角 */
            lcd_fill_rect(x + w - corner, y + h - WARN_BORDER_THICK, corner, WARN_BORDER_THICK, color);
            lcd_fill_rect(x + w - WARN_BORDER_THICK, y + h - corner, WARN_BORDER_THICK, corner, color);

            /* 危险文字提示 */
            lcd_draw_string(x + w/2 - 24, y + h - 20,
                            "DANGER!", color, LCD_COLOR_BLACK, 1);
        } else {
            /* 闪烁灭时绘制黑色边框 */
            lcd_draw_rect(x, y, w, h, WARN_BORDER_THICK, LCD_COLOR_BLACK);
        }
    } else {
        /* 注意：黄色边框 */
        lcd_draw_rect(x, y, w, h, WARN_BORDER_THICK, color);
        lcd_draw_string(x + w/2 - 24, y + h - 20,
                        "CAUTION", color, LCD_COLOR_BLACK, 1);
    }
}

void blind_spot_ui_draw_statusbar(float fps_left, float fps_right,
                                   turn_signal_state_t turn_state)
{
    if (!s_ui.initialized) return;

    int sw = s_ui.config.screen_width;
    int sh = s_ui.config.screen_height;
    int bar_y = sh - STATUSBAR_HEIGHT;

    /* 状态栏背景 */
    lcd_fill_rect(0, bar_y, sw, STATUSBAR_HEIGHT, LCD_COLOR_DARKGRAY);

    /* 分割线 */
    lcd_draw_hline(0, bar_y, sw, LCD_COLOR_GRAY);

    /* 左摄像头 FPS */
    char buf[32];
    snprintf(buf, sizeof(buf), "L:%.0f", fps_left);
    lcd_draw_string(2, bar_y + 2, buf, LCD_COLOR_CYAN, LCD_COLOR_DARKGRAY, 1);

    /* 右摄像头 FPS */
    snprintf(buf, sizeof(buf), "R:%.0f", fps_right);
    lcd_draw_string(50, bar_y + 2, buf, LCD_COLOR_ORANGE, LCD_COLOR_DARKGRAY, 1);

    /* 转向灯状态 */
    uint16_t turn_color = LCD_COLOR_WHITE;
    const char *turn_str = "---";
    switch (turn_state) {
        case TURN_SIGNAL_LEFT:
            turn_str  = "<< LEFT";
            turn_color = LCD_COLOR_YELLOW;
            break;
        case TURN_SIGNAL_RIGHT:
            turn_str  = "RIGHT >>";
            turn_color = LCD_COLOR_YELLOW;
            break;
        case TURN_SIGNAL_BOTH:
            turn_str  = "<< HAZARD >>";
            turn_color = LCD_COLOR_ORANGE;
            break;
        default:
            break;
    }
    lcd_draw_string(sw/2 - 40, bar_y + 2, turn_str, turn_color, LCD_COLOR_DARKGRAY, 1);

    /* 模式指示 */
    const char *mode_str = "SPLIT";
    switch (s_ui.mode) {
        case DISPLAY_MODE_LEFT_FULL:  mode_str = "L-FULL"; break;
        case DISPLAY_MODE_RIGHT_FULL: mode_str = "R-FULL"; break;
        case DISPLAY_MODE_IDLE:       mode_str = "IDLE";   break;
        default:                      mode_str = "SPLIT";  break;
    }
    lcd_draw_string(sw - 50, bar_y + 2, mode_str, LCD_COLOR_LIGHTGRAY, LCD_COLOR_DARKGRAY, 1);
}