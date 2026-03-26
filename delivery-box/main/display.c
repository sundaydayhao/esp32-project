/**
 * @file display.c
 * @brief 显示界面管理模块实现
 */

#include "display.h"
#include "ssd1306.h"
#include "locker.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "display";

// ============================================================
// 内部状态
// ============================================================

typedef struct {
    display_state_t state;
    char            line1[32];
    char            line2[32];
    char            line3[32];
    char            line4[32];
    bool            dirty;          // 需要刷新
    uint32_t        auto_return_ms; // 自动返回待机的时间 (0=不自动返回)
    uint32_t        show_time;      // 当前界面显示时间戳
} display_ctx_t;

static display_ctx_t s_ctx;
static SemaphoreHandle_t s_mutex = NULL;

// ============================================================
// 初始化
// ============================================================

esp_err_t display_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "创建互斥锁失败");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ssd1306_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SSD1306初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.state = DISPLAY_STATE_IDLE;
    s_ctx.dirty = true;

    ESP_LOGI(TAG, "显示模块初始化完成");
    return ESP_OK;
}

// ============================================================
// 内部绘制函数
// ============================================================

/**
 * @brief 绘制待机界面
 *
 * ┌────────────────────────┐
 * │  Smart Parcel Locker   │  <- 标题 (反色)
 * ├────────────────────────┤
 * │ [1]EMPTY [2]OCCUPIED   │  <- 格口状态
 * │ [3]EMPTY [4]EMPTY      │
 * ├────────────────────────┤
 * │ Press # to pickup      │  <- 提示
 * │ Press A to deposit     │
 * └────────────────────────┘
 */
static void draw_idle(void)
{
    ssd1306_clear();

    // 标题栏 (反色背景)
    ssd1306_fill_rect(0, 0, SSD1306_WIDTH, 10, true);
    ssd1306_draw_string(8, 1, "Smart Parcel Locker", false);

    // 分隔线
    ssd1306_draw_hline(0, 11, SSD1306_WIDTH, true);

    // 格口状态 (2列布局)
    const char *state_str[] = {"EMPTY", "OCCUP", "OPEN ", "LOCK "};
    for (int i = 0; i < LOCKER_COUNT; i++) {
        locker_info_t info;
        locker_get_info(i, &info);

        char buf[20];
        int state_idx = (int)info.state;
        if (state_idx > 3) state_idx = 3;

        snprintf(buf, sizeof(buf), "[%d]%s", i + 1, state_str[state_idx]);

        int col = (i % 2) * 64;
        int row = 13 + (i / 2) * 10;
        ssd1306_draw_string(col + 2, row, buf, true);
    }

    // 分隔线
    ssd1306_draw_hline(0, 43, SSD1306_WIDTH, true);

    // 操作提示
    ssd1306_draw_string(2, 45, "# Pickup  A Deposit", true);
    ssd1306_draw_string(2, 55, "D Admin   * Cancel", true);

    ssd1306_flush();
}

/**
 * @brief 绘制输入取件码界面
 *
 * ┌────────────────────────┐
 * │     Enter Pickup Code  │
 * ├────────────────────────┤
 * │  Code: _ _ _ _ _ _     │
 * │        1 2 3 * * *     │  <- 已输入的数字
 * ├────────────────────────┤
 * │  # Confirm  * Delete   │
 * └────────────────────────┘
 */
static void draw_enter_code(void)
{
    ssd1306_clear();

    // 标题
    ssd1306_fill_rect(0, 0, SSD1306_WIDTH, 10, true);
    ssd1306_draw_string(10, 1, "Enter Pickup Code", false);

    ssd1306_draw_hline(0, 11, SSD1306_WIDTH, true);

    // 提示文字
    ssd1306_draw_string(2, 14, "Code:", true);

    // 显示已输入的数字 (用 * 遮盖)
    char display_buf[16] = {0};
    int len = strlen(s_ctx.line1);
    for (int i = 0; i < CODE_LENGTH; i++) {
        if (i < len) {
            display_buf[i * 2]     = '*';
        } else {
            display_buf[i * 2]     = '_';
        }
        display_buf[i * 2 + 1] = ' ';
    }
    ssd1306_draw_string(38, 14, display_buf, true);

    // 显示实际数字 (小字)
    ssd1306_draw_string(38, 24, s_ctx.line1, true);

    // 进度条
    int progress = len * SSD1306_WIDTH / CODE_LENGTH;
    ssd1306_fill_rect(0, 35, progress, 3, true);
    ssd1306_draw_rect(0, 35, SSD1306_WIDTH, 3, true);

    ssd1306_draw_hline(0, 43, SSD1306_WIDTH, true);

    // 操作提示
    ssd1306_draw_string(2, 45, "# Confirm", true);
    ssd1306_draw_string(70, 45, "* Delete", true);
    ssd1306_draw_string(2, 55, "B Cancel", true);

    ssd1306_flush();
}

/**
 * @brief 绘制存件界面
 */
static void draw_depositing(void)
{
    ssd1306_clear();

    ssd1306_fill_rect(0, 0, SSD1306_WIDTH, 10, true);
    ssd1306_draw_string(16, 1, "Deposit Package", false);

    ssd1306_draw_hline(0, 11, SSD1306_WIDTH, true);

    ssd1306_draw_string(2, 14, s_ctx.line1, true); // "Locker #X is open"
    ssd1306_draw_string(2, 24, "Pickup Code:", true);

    // 大字显示取件码
    char *code = s_ctx.line2;
    int code_x = (SSD1306_WIDTH - strlen(code) * FONT_WIDTH) / 2;
    ssd1306_draw_string(code_x, 34, code, true);

    ssd1306_draw_hline(0, 46, SSD1306_WIDTH, true);
    ssd1306_draw_string(2, 48, "Close door when done", true);

    ssd1306_flush();
}

/**
 * @brief 绘制成功界面
 */
static void draw_success(void)
{
    ssd1306_clear();

    // 成功图标区域
    ssd1306_fill_rect(0, 0, SSD1306_WIDTH, 10, true);
    ssd1306_draw_string(30, 1, "** SUCCESS **", false);

    ssd1306_draw_hline(0, 11, SSD1306_WIDTH, true);

    ssd1306_draw_string_centered(2, s_ctx.line1, true);
    if (s_ctx.line2[0]) {
        ssd1306_draw_string_centered(3, s_ctx.line2, true);
    }
    if (s_ctx.line3[0]) {
        ssd1306_draw_string_centered(4, s_ctx.line3, true);
    }

    ssd1306_draw_hline(0, 53, SSD1306_WIDTH, true);
    ssd1306_draw_string_centered(7, "Returning...", true);

    ssd1306_flush();
}

/**
 * @brief 绘制错误界面
 */
static void draw_error(void)
{
    ssd1306_clear();

    ssd1306_fill_rect(0, 0, SSD1306_WIDTH, 10, true);
    ssd1306_draw_string(36, 1, "** ERROR **", false);

    ssd1306_draw_hline(0, 11, SSD1306_WIDTH, true);

    ssd1306_draw_string_centered(2, s_ctx.line1, true);
    if (s_ctx.line2[0]) {
        ssd1306_draw_string_centered(3, s_ctx.line2, true);
    }
    if (s_ctx.line3[0]) {
        ssd1306_draw_string_centered(4, s_ctx.line3, true);
    }

    ssd1306_draw_hline(0, 53, SSD1306_WIDTH, true);
    ssd1306_draw_string_centered(7, "Press any key...", true);

    ssd1306_flush();
}

/**
 * @brief 绘制WiFi信息界面
 */
static void draw_wifi_info(void)
{
    ssd1306_clear();

    ssd1306_fill_rect(0, 0, SSD1306_WIDTH, 10, true);
    ssd1306_draw_string(22, 1, "WiFi / HTTP Info", false);

    ssd1306_draw_hline(0, 11, SSD1306_WIDTH, true);

    ssd1306_draw_string(2, 14, "SSID:", true);
    ssd1306_draw_string(38, 14, s_ctx.line1, true);

    ssd1306_draw_string(2, 24, "IP:", true);
    ssd1306_draw_string(20, 24, s_ctx.line2, true);

    ssd1306_draw_string(2, 34, "URL: http://", true);
    ssd1306_draw_string(2, 44, s_ctx.line2, true);

    ssd1306_draw_hline(0, 53, SSD1306_WIDTH, true);
    ssd1306_draw_string(2, 55, "API port: 80", true);

    ssd1306_flush();
}

// ============================================================
// 显示任务
// ============================================================

void display_task(void *arg)
{
    ESP_LOGI(TAG, "显示任务启动");

    uint32_t idle_refresh_counter = 0;

    while (1) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);

        bool need_draw = s_ctx.dirty;

        // 待机界面定期刷新 (每2秒更新格口状态)
        if (s_ctx.state == DISPLAY_STATE_IDLE) {
            idle_refresh_counter++;
            if (idle_refresh_counter >= 20) { // 20 * 100ms = 2s
                idle_refresh_counter = 0;
                need_draw = true;
            }
        }

        // 检查自动返回待机
        if (s_ctx.auto_return_ms > 0 && s_ctx.state != DISPLAY_STATE_IDLE) {
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if ((now - s_ctx.show_time) >= s_ctx.auto_return_ms) {
                s_ctx.state = DISPLAY_STATE_IDLE;
                s_ctx.auto_return_ms = 0;
                need_draw = true;
            }
        }

        if (need_draw) {
            s_ctx.dirty = false;
            display_state_t state = s_ctx.state;
            xSemaphoreGive(s_mutex);

            // 根据状态绘制界面
            switch (state) {
            case DISPLAY_STATE_IDLE:
                draw_idle();
                break;
            case DISPLAY_STATE_ENTER_CODE:
                draw_enter_code();
                break;
            case DISPLAY_STATE_DEPOSITING:
                draw_depositing();
                break;
            case DISPLAY_STATE_SUCCESS:
                draw_success();
                break;
            case DISPLAY_STATE_ERROR:
                draw_error();
                break;
            case DISPLAY_STATE_WIFI_INFO:
                draw_wifi_info();
                break;
            default:
                draw_idle();
                break;
            }
        } else {
            xSemaphoreGive(s_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms 刷新间隔
    }
}

// ============================================================
// 公共 API
// ============================================================

void display_show_idle(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_ctx.state = DISPLAY_STATE_IDLE;
    s_ctx.auto_return_ms = 0;
    s_ctx.dirty = true;
    xSemaphoreGive(s_mutex);
}

void display_show_enter_code(const char *entered_digits, uint8_t digit_count)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_ctx.state = DISPLAY_STATE_ENTER_CODE;
    s_ctx.auto_return_ms = 0;
    if (entered_digits) {
        strncpy(s_ctx.line1, entered_digits, sizeof(s_ctx.line1) - 1);
    } else {
        s_ctx.line1[0] = '\0';
    }
    s_ctx.dirty = true;
    xSemaphoreGive(s_mutex);
}

void display_show_depositing(uint8_t locker_id, const char *code)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_ctx.state = DISPLAY_STATE_DEPOSITING;
    s_ctx.auto_return_ms = 0;
    snprintf(s_ctx.line1, sizeof(s_ctx.line1), "Locker #%d is open", locker_id + 1);
    if (code) {
        strncpy(s_ctx.line2, code, sizeof(s_ctx.line2) - 1);
    }
    s_ctx.dirty = true;
    xSemaphoreGive(s_mutex);
}

void display_show_success(const char *message, uint8_t locker_id)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_ctx.state = DISPLAY_STATE_SUCCESS;
    s_ctx.auto_return_ms = 3000; // 3秒后自动返回待机
    s_ctx.show_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (message) {
        strncpy(s_ctx.line1, message, sizeof(s_ctx.line1) - 1);
    }
    if (locker_id != 0xFF) {
        snprintf(s_ctx.line2, sizeof(s_ctx.line2), "Locker #%d", locker_id + 1);
    } else {
        s_ctx.line2[0] = '\0';
    }
    s_ctx.line3[0] = '\0';
    s_ctx.dirty = true;
    xSemaphoreGive(s_mutex);
}

void display_show_error(const char *message, uint8_t retry_left)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_ctx.state = DISPLAY_STATE_ERROR;
    s_ctx.auto_return_ms = 3000; // 3秒后自动返回
    s_ctx.show_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (message) {
        strncpy(s_ctx.line1, message, sizeof(s_ctx.line1) - 1);
    }
    if (retry_left != 0xFF) {
        snprintf(s_ctx.line2, sizeof(s_ctx.line2), "Retries left: %d", retry_left);
    } else {
        s_ctx.line2[0] = '\0';
    }
    s_ctx.line3[0] = '\0';
    s_ctx.dirty = true;
    xSemaphoreGive(s_mutex);
}

void display_show_wifi_info(const char *ip_addr, const char *ssid)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_ctx.state = DISPLAY_STATE_WIFI_INFO;
    s_ctx.auto_return_ms = 10000; // 10秒后返回待机
    s_ctx.show_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (ssid)    strncpy(s_ctx.line1, ssid,    sizeof(s_ctx.line1) - 1);
    if (ip_addr) strncpy(s_ctx.line2, ip_addr, sizeof(s_ctx.line2) - 1);
    s_ctx.dirty = true;
    xSemaphoreGive(s_mutex);
}

display_state_t display_get_state(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    display_state_t state = s_ctx.state;
    xSemaphoreGive(s_mutex);
    return state;
}

void display_refresh(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_ctx.dirty = true;
    xSemaphoreGive(s_mutex);
}