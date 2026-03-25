/**
 * @file slideshow.c
 * @brief 幻灯片管理器实现
 */
#include "slideshow.h"
#include "config.h"
#include "lcd/lcd_driver.h"
#include "sd/photo_scanner.h"
#include "decoder/photo_jpeg_decoder.h"
#include "decoder/bmp_decoder.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static slideshow_config_t s_cfg;
static bool               s_paused    = false;
static bool               s_running   = false;
static SemaphoreHandle_t  s_next_sem  = NULL;   /* 手动切换信号 */
static bool               s_go_prev   = false;

/* ---- 从 NVS 加载配置 ---- */
static void load_config(void)
{
    s_cfg.interval_ms = SLIDESHOW_INTERVAL_MS_DEFAULT;
    s_cfg.order       = PLAY_ORDER_SEQUENTIAL;
    s_cfg.transition  = TRANSITION_FADE;
    s_cfg.brightness  = 100;
    s_cfg.show_info   = true;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint32_t v32;
        uint8_t  v8;
        if (nvs_get_u32(h, NVS_KEY_INTERVAL,   &v32) == ESP_OK) s_cfg.interval_ms = v32;
        if (nvs_get_u8 (h, NVS_KEY_BRIGHTNESS, &v8)  == ESP_OK) s_cfg.brightness  = v8;
        if (nvs_get_u8 (h, NVS_KEY_SCALE_MODE, &v8)  == ESP_OK) s_cfg.order       = (play_order_t)v8;
        nvs_close(h);
    }
}

static void save_config(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, NVS_KEY_INTERVAL,   s_cfg.interval_ms);
        nvs_set_u8 (h, NVS_KEY_BRIGHTNESS, s_cfg.brightness);
        nvs_set_u8 (h, NVS_KEY_SCALE_MODE, (uint8_t)s_cfg.order);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ---- 判断文件类型 ---- */
static bool is_jpeg(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    return (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0);
}

/* ---- 解码图片到帧缓冲区 ---- */
static esp_err_t decode_photo(const char *path, uint16_t *fb)
{
    if (is_jpeg(path)) {
        return jpeg_decode_to_fb(path, fb, SCALE_MODE_DEFAULT, COLOR_BLACK, NULL);
    } else {
        return bmp_decode_to_fb(path, fb, SCALE_MODE_DEFAULT, COLOR_BLACK);
    }
}

/* ---- 淡入淡出过渡 ---- */
static void transition_fade(uint16_t *fb_old, uint16_t *fb_new)
{
    uint32_t total = LCD_H_RES * LCD_V_RES;
    uint32_t step_ms = TRANSITION_DURATION_MS / TRANSITION_STEPS;

    for (int step = 0; step <= TRANSITION_STEPS; step++) {
        float alpha = (float)step / TRANSITION_STEPS;
        float beta  = 1.0f - alpha;

        /* 混合两帧（在 fb_old 上直接操作以节省内存） */
        for (uint32_t i = 0; i < total; i++) {
            uint16_t c0 = fb_old[i];
            uint16_t c1 = fb_new[i];

            uint8_t r0 = (c0 >> 11) & 0x1F;
            uint8_t g0 = (c0 >>  5) & 0x3F;
            uint8_t b0 = (c0      ) & 0x1F;

            uint8_t r1 = (c1 >> 11) & 0x1F;
            uint8_t g1 = (c1 >>  5) & 0x3F;
            uint8_t b1 = (c1      ) & 0x1F;

            uint8_t r = (uint8_t)(r0 * beta + r1 * alpha);
            uint8_t g = (uint8_t)(g0 * beta + g1 * alpha);
            uint8_t b = (uint8_t)(b0 * beta + b1 * alpha);

            fb_old[i] = (uint16_t)((r << 11) | (g << 5) | b);
        }

        lcd_driver_flush(fb_old);
        lcd_driver_wait_vsync();
        vTaskDelay(pdMS_TO_TICKS(step_ms));
    }
}

/* ---- 向左滑动过渡 ---- */
static void transition_slide(uint16_t *fb_old, uint16_t *fb_new, bool left)
{
    uint32_t step_ms = TRANSITION_DURATION_MS / TRANSITION_STEPS;
    /* 使用临时缓冲区合成滑动帧 */
    uint16_t *tmp = heap_caps_malloc(LCD_H_RES * LCD_V_RES * 2,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tmp) {
        /* 内存不足，直接切换 */
        lcd_driver_flush(fb_new);
        return;
    }

    for (int step = 1; step <= TRANSITION_STEPS; step++) {
        int offset = (LCD_H_RES * step) / TRANSITION_STEPS;
        if (!left) offset = -offset;

        for (int y = 0; y < LCD_V_RES; y++) {
            for (int x = 0; x < LCD_H_RES; x++) {
                int src_x_old = x + offset;
                int src_x_new = x + offset - (left ? LCD_H_RES : -LCD_H_RES);

                uint16_t px;
                if (src_x_old >= 0 && src_x_old < LCD_H_RES) {
                    px = fb_old[y * LCD_H_RES + src_x_old];
                } else if (src_x_new >= 0 && src_x_new < LCD_H_RES) {
                    px = fb_new[y * LCD_H_RES + src_x_new];
                } else {
                    px = COLOR_BLACK;
                }
                tmp[y * LCD_H_RES + x] = px;
            }
        }

        lcd_driver_flush(tmp);
        lcd_driver_wait_vsync();
        vTaskDelay(pdMS_TO_TICKS(step_ms));
    }

    free(tmp);
}

/* ---- 显示图片信息覆盖层 ---- */
static void draw_info_overlay(uint16_t *fb, const char *path, int index, int total)
{
    /* 在底部绘制半透明信息条（简单实现：暗化底部 20 行） */
    int bar_y = LCD_V_RES - 20;
    for (int y = bar_y; y < LCD_V_RES; y++) {
        for (int x = 0; x < LCD_H_RES; x++) {
            uint16_t c = fb[y * LCD_H_RES + x];
            /* 亮度减半 */
            uint8_t r = ((c >> 11) & 0x1F) >> 1;
            uint8_t g = ((c >>  5) & 0x3F) >> 1;
            uint8_t b = ((c      ) & 0x1F) >> 1;
            fb[y * LCD_H_RES + x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
    /* 注：文字渲染需要字体库，此处仅做暗化处理 */
    (void)path; (void)index; (void)total;
}

esp_err_t slideshow_init(void)
{
    s_next_sem = xSemaphoreCreateBinary();
    configASSERT(s_next_sem);
    load_config();
    lcd_driver_set_brightness(s_cfg.brightness);
    ESP_LOGI(TAG_SLIDESHOW, "幻灯片管理器初始化完成 (间隔=%lums, 顺序=%d)",
             (unsigned long)s_cfg.interval_ms, s_cfg.order);
    return ESP_OK;
}

void slideshow_pause(bool pause)
{
    s_paused = pause;
    ESP_LOGI(TAG_SLIDESHOW, "幻灯片%s", pause ? "已暂停" : "已恢复");
}

void slideshow_next(void)
{
    s_go_prev = false;
    xSemaphoreGive(s_next_sem);
}

void slideshow_prev(void)
{
    s_go_prev = true;
    xSemaphoreGive(s_next_sem);
}

esp_err_t slideshow_set_config(const slideshow_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    s_cfg = *cfg;
    lcd_driver_set_brightness(s_cfg.brightness);
    save_config();
    return ESP_OK;
}

void slideshow_get_config(slideshow_config_t *cfg)
{
    if (cfg) *cfg = s_cfg;
}

void slideshow_task(void *arg)
{
    ESP_LOGI(TAG_SLIDESHOW, "幻灯片任务启动");

    int photo_count = photo_scanner_get_count();
    if (photo_count == 0) {
        ESP_LOGE(TAG_SLIDESHOW, "SD 卡中没有找到图片！");
        /* 显示提示信息 */
        uint16_t *fb0, *fb1;
        lcd_driver_get_framebuf(&fb0, &fb1);
        lcd_fb_fill(fb0, COLOR_BLACK);
        lcd_driver_flush(fb0);
        vTaskDelete(NULL);
        return;
    }

    /* 获取双帧缓冲区 */
    uint16_t *fb[2];
    lcd_driver_get_framebuf(&fb[0], &fb[1]);
    int cur_fb = 0;

    /* 恢复上次播放位置 */
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint32_t last_idx = 0;
        if (nvs_get_u32(h, NVS_KEY_LAST_INDEX, &last_idx) == ESP_OK) {
            if ((int)last_idx < photo_count)
                photo_scanner_set_index((int)last_idx);
        }
        nvs_close(h);
    }

    /* 解码并显示第一张 */
    const char *first_path = photo_scanner_get_path(photo_scanner_get_current_index());
    if (first_path) {
        ESP_LOGI(TAG_SLIDESHOW, "显示: %s", first_path);
        if (decode_photo(first_path, fb[cur_fb]) == ESP_OK) {
            if (s_cfg.show_info)
                draw_info_overlay(fb[cur_fb], first_path,
                                  photo_scanner_get_current_index() + 1, photo_count);
            lcd_driver_flush(fb[cur_fb]);
        }
    }

    s_running = true;

    while (1) {
        /* 等待定时器或手动切换信号 */
        bool manual = (xSemaphoreTake(s_next_sem,
                       pdMS_TO_TICKS(s_paused ? portMAX_DELAY : s_cfg.interval_ms))
                       == pdTRUE);

        if (s_paused && !manual) continue;

        /* 确定下一张 */
        const char *next_path;
        if (manual && s_go_prev) {
            next_path = photo_scanner_prev();
        } else {
            next_path = photo_scanner_next(s_cfg.order);
        }

        if (!next_path) continue;

        ESP_LOGI(TAG_SLIDESHOW, "[%d/%d] %s",
                 photo_scanner_get_current_index() + 1, photo_count, next_path);

        /* 解码到备用帧缓冲区 */
        int next_fb = 1 - cur_fb;
        esp_err_t ret = decode_photo(next_path, fb[next_fb]);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG_SLIDESHOW, "解码失败，跳过: %s", next_path);
            continue;
        }

        if (s_cfg.show_info)
            draw_info_overlay(fb[next_fb], next_path,
                              photo_scanner_get_current_index() + 1, photo_count);

        /* 执行过渡动画 */
        switch (s_cfg.transition) {
        case TRANSITION_FADE:
            transition_fade(fb[cur_fb], fb[next_fb]);
            break;
        case TRANSITION_SLIDE_L:
            transition_slide(fb[cur_fb], fb[next_fb], true);
            break;
        case TRANSITION_SLIDE_R:
            transition_slide(fb[cur_fb], fb[next_fb], false);
            break;
        default:
            lcd_driver_flush(fb[next_fb]);
            lcd_driver_wait_vsync();
            break;
        }

        cur_fb = next_fb;

        /*
         * RGB LCD 当前使用了 PSRAM frame buffer + bounce buffer。
         * 在显示进行中执行 NVS/flash 写入会暂时关闭 cache，
         * LCD EOF ISR 再访问 PSRAM 时会触发 "Cache disabled but cached memory region accessed"。
         * 因此这里先禁用运行时持久化，避免播放时重启。
         */
    }
}
