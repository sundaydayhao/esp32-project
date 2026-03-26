/**
 * camera_handler.cpp
 * 摄像头处理模块实现 / Camera Handler Implementation
 *
 * 使用 ESP-IDF esp_camera 组件
 * 支持 OV2640 摄像头模块
 */

#include "camera_handler.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_psram.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char* TAG = "CAMERA";

// ============================================================
// 内部状态 / Internal State
// ============================================================
static bool     s_initialized    = false;
static uint8_t* s_rgb_buffer     = NULL;   // RGB888 帧缓冲
static size_t   s_buffer_size    = 0;
static uint8_t* s_prev_gray      = NULL;   // 上一帧灰度图 (运动检测)
static bool     s_has_prev_frame = false;

// ============================================================
// 内部函数 / Internal Functions
// ============================================================

/**
 * RGB565 转 RGB888
 */
static void rgb565_to_rgb888(const uint8_t* src, uint8_t* dst, int pixel_count)
{
    const uint16_t* pixels = (const uint16_t*)src;
    for (int i = 0; i < pixel_count; i++) {
        uint16_t p = pixels[i];
        // RGB565: RRRRRGGGGGGBBBBB (big-endian from camera)
        // 字节序交换
        p = (p >> 8) | (p << 8);
        dst[i * 3 + 0] = ((p >> 11) & 0x1F) << 3;  // R: 5bit -> 8bit
        dst[i * 3 + 1] = ((p >> 5)  & 0x3F) << 2;  // G: 6bit -> 8bit
        dst[i * 3 + 2] = ( p        & 0x1F) << 3;  // B: 5bit -> 8bit
    }
}

// ============================================================
// 公共接口实现 / Public Interface Implementation
// ============================================================

esp_err_t camera_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "摄像头已初始化");
        return ESP_OK;
    }

    // 摄像头配置
    camera_config_t config = {
        .pin_pwdn     = CAM_PIN_PWDN,
        .pin_reset    = CAM_PIN_RESET,
        .pin_xclk     = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7       = CAM_PIN_D7,
        .pin_d6       = CAM_PIN_D6,
        .pin_d5       = CAM_PIN_D5,
        .pin_d4       = CAM_PIN_D4,
        .pin_d3       = CAM_PIN_D3,
        .pin_d2       = CAM_PIN_D2,
        .pin_d1       = CAM_PIN_D1,
        .pin_d0       = CAM_PIN_D0,
        .pin_vsync    = CAM_PIN_VSYNC,
        .pin_href     = CAM_PIN_HREF,
        .pin_pclk     = CAM_PIN_PCLK,
        .xclk_freq_hz = 20000000,           // 20MHz XCLK
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_RGB565,   // RGB565 用于颜色分析
        .frame_size   = FRAMESIZE_QVGA,     // 320x240
        .jpeg_quality = 12,
        .fb_count     = 2,
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
    };

    // 检查 PSRAM
    if (esp_psram_get_size() == 0) {
        ESP_LOGW(TAG, "未检测到 PSRAM，使用内部 RAM");
        config.fb_location = CAMERA_FB_IN_DRAM;
        config.fb_count = 1;
    } else {
        ESP_LOGI(TAG, "PSRAM 大小: %d KB", esp_psram_get_size() / 1024);
    }

    esp_err_t ret = esp_camera_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "摄像头初始化失败: 0x%x", ret);
        return ret;
    }

    // 调整传感器参数以优化颜色识别
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_brightness(sensor, 0);     // 亮度
        sensor->set_contrast(sensor, 0);       // 对比度
        sensor->set_saturation(sensor, 2);     // 提高饱和度，增强颜色识别
        sensor->set_sharpness(sensor, 1);      // 锐度
        sensor->set_denoise(sensor, 1);        // 降噪
        sensor->set_whitebal(sensor, 1);       // 自动白平衡
        sensor->set_gain_ctrl(sensor, 1);      // 自动增益
        sensor->set_exposure_ctrl(sensor, 1);  // 自动曝光
        sensor->set_awb_gain(sensor, 1);       // AWB 增益
        sensor->set_lenc(sensor, 1);           // 镜头校正
        sensor->set_hmirror(sensor, 0);        // 水平镜像
        sensor->set_vflip(sensor, 0);          // 垂直翻转
        ESP_LOGI(TAG, "传感器参数已优化");
    }

    // 分配 RGB888 缓冲区
    s_buffer_size = IMG_WIDTH * IMG_HEIGHT * 3;
    if (esp_psram_get_size() > 0) {
        s_rgb_buffer = (uint8_t*)heap_caps_malloc(s_buffer_size, MALLOC_CAP_SPIRAM);
    }
    if (!s_rgb_buffer) {
        s_rgb_buffer = (uint8_t*)malloc(s_buffer_size);
    }
    if (!s_rgb_buffer) {
        ESP_LOGE(TAG, "RGB 缓冲区分配失败 (%d bytes)", s_buffer_size);
        esp_camera_deinit();
        return ESP_ERR_NO_MEM;
    }

    // 分配运动检测灰度缓冲区 (降采样 2x)
    size_t gray_size = (IMG_WIDTH / 2) * (IMG_HEIGHT / 2);
    s_prev_gray = (uint8_t*)malloc(gray_size);
    if (!s_prev_gray) {
        ESP_LOGW(TAG, "运动检测缓冲区分配失败，运动检测不可用");
    }

    s_initialized = true;
    ESP_LOGI(TAG, "摄像头初始化成功 (%dx%d)", IMG_WIDTH, IMG_HEIGHT);
    return ESP_OK;
}

void camera_deinit(void)
{
    if (!s_initialized) return;

    esp_camera_deinit();

    if (s_rgb_buffer) {
        free(s_rgb_buffer);
        s_rgb_buffer = NULL;
    }
    if (s_prev_gray) {
        free(s_prev_gray);
        s_prev_gray = NULL;
    }

    s_initialized = false;
    s_has_prev_frame = false;
    ESP_LOGI(TAG, "摄像头已释放");
}

esp_err_t camera_capture_frame(void)
{
    if (!s_initialized || !s_rgb_buffer) {
        return ESP_ERR_INVALID_STATE;
    }

    // 丢弃旧帧，获取最新帧
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
        esp_camera_fb_return(fb);
    }

    // 获取新帧
    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "帧捕获失败");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "帧捕获: %dx%d, %d bytes, format=%d",
             fb->width, fb->height, fb->len, fb->format);

    if (fb->format == PIXFORMAT_RGB565) {
        rgb565_to_rgb888(fb->buf, s_rgb_buffer, IMG_WIDTH * IMG_HEIGHT);
    } else {
        ESP_LOGW(TAG, "不支持的像素格式: %d", fb->format);
        esp_camera_fb_return(fb);
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_camera_fb_return(fb);
    return ESP_OK;
}

uint8_t* camera_get_rgb_buffer(void)
{
    return s_rgb_buffer;
}

size_t camera_get_buffer_size(void)
{
    return s_buffer_size;
}

bool camera_detect_object(void)
{
    if (!s_initialized || !s_prev_gray) return false;

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return false;

    bool motion_detected = false;

    if (fb->format == PIXFORMAT_RGB565) {
        const uint16_t* pixels = (const uint16_t*)fb->buf;
        int motion_pixels = 0;
        int w2 = IMG_WIDTH / 2;

        // 分析 ROI 区域，降采样 2x
        for (int y = ROI_Y_START; y < ROI_Y_START + ROI_HEIGHT; y += 2) {
            for (int x = ROI_X_START; x < ROI_X_START + ROI_WIDTH; x += 2) {
                int idx = y * IMG_WIDTH + x;
                uint16_t p = pixels[idx];
                p = (p >> 8) | (p << 8);  // 字节序

                uint8_t r = ((p >> 11) & 0x1F) << 3;
                uint8_t g = ((p >> 5)  & 0x3F) << 2;
                uint8_t b = ( p        & 0x1F) << 3;
                // 灰度值 (BT.601)
                uint8_t gray = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);

                int prev_idx = (y / 2) * w2 + (x / 2);
                if (prev_idx < (int)((IMG_WIDTH / 2) * (IMG_HEIGHT / 2))) {
                    if (s_has_prev_frame) {
                        int diff = (int)gray - (int)s_prev_gray[prev_idx];
                        if (diff < 0) diff = -diff;
                        if (diff > MOTION_THRESHOLD) {
                            motion_pixels++;
                        }
                    }
                    s_prev_gray[prev_idx] = gray;
                }
            }
        }

        if (!s_has_prev_frame) {
            s_has_prev_frame = true;
        } else {
            motion_detected = (motion_pixels > MOTION_PIXEL_COUNT);
            if (motion_detected) {
                ESP_LOGI(TAG, "运动检测: %d 像素变化", motion_pixels);
            }
        }
    }

    esp_camera_fb_return(fb);
    return motion_detected;
}

void camera_get_roi_avg_color(uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!s_rgb_buffer || !r || !g || !b) {
        if (r) *r = 0;
        if (g) *g = 0;
        if (b) *b = 0;
        return;
    }

    uint32_t sum_r = 0, sum_g = 0, sum_b = 0;
    int count = 0;

    for (int y = ROI_Y_START; y < ROI_Y_START + ROI_HEIGHT; y++) {
        for (int x = ROI_X_START; x < ROI_X_START + ROI_WIDTH; x++) {
            int idx = (y * IMG_WIDTH + x) * 3;
            sum_r += s_rgb_buffer[idx + 0];
            sum_g += s_rgb_buffer[idx + 1];
            sum_b += s_rgb_buffer[idx + 2];
            count++;
        }
    }

    if (count > 0) {
        *r = (uint8_t)(sum_r / count);
        *g = (uint8_t)(sum_g / count);
        *b = (uint8_t)(sum_b / count);
    } else {
        *r = *g = *b = 0;
    }
}