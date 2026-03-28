/**
 * camera_handler.cpp
 * Camera handler implementation
 */

#include "camera_handler.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "img_converters.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "CAMERA";

static bool     s_initialized    = false;
static uint8_t *s_rgb_buffer     = NULL;
static size_t   s_buffer_size    = 0;
static uint8_t *s_jpeg_buffer    = NULL;
static size_t   s_jpeg_size      = 0;
static bool     s_jpeg_dirty     = true;
static uint8_t *s_prev_gray      = NULL;
static bool     s_has_prev_frame = false;

static void rgb565_to_rgb888(const uint8_t *src, uint8_t *dst, int pixel_count)
{
    const uint16_t *pixels = (const uint16_t *)src;
    for (int i = 0; i < pixel_count; i++) {
        uint16_t p = pixels[i];
        p = (p >> 8) | (p << 8);
        dst[i * 3 + 0] = ((p >> 11) & 0x1F) << 3;
        dst[i * 3 + 1] = ((p >> 5)  & 0x3F) << 2;
        dst[i * 3 + 2] = ( p        & 0x1F) << 3;
    }
}

esp_err_t camera_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Camera already initialized");
        return ESP_OK;
    }

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
        .xclk_freq_hz = 20000000,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size   = FRAMESIZE_QVGA,
        .jpeg_quality = 12,
        .fb_count     = 2,
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
    };

    if (esp_psram_get_size() == 0) {
        ESP_LOGW(TAG, "PSRAM not detected, using internal RAM");
        config.fb_location = CAMERA_FB_IN_DRAM;
        config.fb_count = 1;
    } else {
        ESP_LOGI(TAG, "PSRAM size: %d KB", esp_psram_get_size() / 1024);
    }

    esp_err_t ret = esp_camera_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", ret);
        return ret;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_brightness(sensor, 0);
        sensor->set_contrast(sensor, 0);
        sensor->set_saturation(sensor, 2);
        sensor->set_sharpness(sensor, 1);
        sensor->set_denoise(sensor, 1);
        sensor->set_whitebal(sensor, 1);
        sensor->set_gain_ctrl(sensor, 1);
        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_awb_gain(sensor, 1);
        sensor->set_lenc(sensor, 1);
        sensor->set_hmirror(sensor, 0);
        sensor->set_vflip(sensor, 0);
    }

    s_buffer_size = IMG_WIDTH * IMG_HEIGHT * 3;
    if (esp_psram_get_size() > 0) {
        s_rgb_buffer = (uint8_t *)heap_caps_malloc(s_buffer_size, MALLOC_CAP_SPIRAM);
    }
    if (!s_rgb_buffer) {
        s_rgb_buffer = (uint8_t *)malloc(s_buffer_size);
    }
    if (!s_rgb_buffer) {
        esp_camera_deinit();
        return ESP_ERR_NO_MEM;
    }

    const size_t gray_size = (IMG_WIDTH / 2) * (IMG_HEIGHT / 2);
    s_prev_gray = (uint8_t *)malloc(gray_size);
    if (!s_prev_gray) {
        ESP_LOGW(TAG, "Motion detection buffer allocation failed");
    }

    s_initialized = true;
    s_jpeg_dirty = true;
    ESP_LOGI(TAG, "Camera init ok (%dx%d)", IMG_WIDTH, IMG_HEIGHT);
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
    if (s_jpeg_buffer) {
        free(s_jpeg_buffer);
        s_jpeg_buffer = NULL;
    }

    s_initialized = false;
    s_has_prev_frame = false;
    s_jpeg_size = 0;
    s_jpeg_dirty = true;
    ESP_LOGI(TAG, "Camera released");
}

esp_err_t camera_capture_frame(void)
{
    if (!s_initialized || !s_rgb_buffer) {
        return ESP_ERR_INVALID_STATE;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
        esp_camera_fb_return(fb);
    }

    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Capture failed");
        return ESP_FAIL;
    }

    if (fb->format != PIXFORMAT_RGB565) {
        ESP_LOGW(TAG, "Unsupported frame format: %d", fb->format);
        esp_camera_fb_return(fb);
        return ESP_ERR_NOT_SUPPORTED;
    }

    rgb565_to_rgb888(fb->buf, s_rgb_buffer, IMG_WIDTH * IMG_HEIGHT);
    esp_camera_fb_return(fb);

    if (s_jpeg_buffer) {
        free(s_jpeg_buffer);
        s_jpeg_buffer = NULL;
    }
    s_jpeg_size = 0;
    s_jpeg_dirty = true;
    return ESP_OK;
}

uint8_t *camera_get_rgb_buffer(void)
{
    return s_rgb_buffer;
}

size_t camera_get_buffer_size(void)
{
    return s_buffer_size;
}

esp_err_t camera_get_jpeg_buffer(uint8_t **jpeg_buf, size_t *jpeg_len)
{
    if (!jpeg_buf || !jpeg_len) return ESP_ERR_INVALID_ARG;
    if (!s_initialized || !s_rgb_buffer) return ESP_ERR_INVALID_STATE;

    if (s_jpeg_dirty || !s_jpeg_buffer) {
        if (s_jpeg_buffer) {
            free(s_jpeg_buffer);
            s_jpeg_buffer = NULL;
            s_jpeg_size = 0;
        }

        if (!fmt2jpg(s_rgb_buffer,
                     s_buffer_size,
                     IMG_WIDTH,
                     IMG_HEIGHT,
                     PIXFORMAT_RGB888,
                     80,
                     &s_jpeg_buffer,
                     &s_jpeg_size)) {
            ESP_LOGE(TAG, "JPEG conversion failed");
            return ESP_FAIL;
        }

        s_jpeg_dirty = false;
        ESP_LOGI(TAG, "JPEG prepared: %d bytes", (int)s_jpeg_size);
    }

    *jpeg_buf = s_jpeg_buffer;
    *jpeg_len = s_jpeg_size;
    return ESP_OK;
}

bool camera_detect_object(void)
{
    if (!s_initialized || !s_prev_gray) return false;

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return false;

    bool motion_detected = false;

    if (fb->format == PIXFORMAT_RGB565) {
        const uint16_t *pixels = (const uint16_t *)fb->buf;
        int motion_pixels = 0;
        const int w2 = IMG_WIDTH / 2;

        for (int y = ROI_Y_START; y < ROI_Y_START + ROI_HEIGHT; y += 2) {
            for (int x = ROI_X_START; x < ROI_X_START + ROI_WIDTH; x += 2) {
                const int idx = y * IMG_WIDTH + x;
                uint16_t p = pixels[idx];
                p = (p >> 8) | (p << 8);

                const uint8_t r = ((p >> 11) & 0x1F) << 3;
                const uint8_t g = ((p >> 5)  & 0x3F) << 2;
                const uint8_t b = ( p        & 0x1F) << 3;
                const uint8_t gray = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);

                const int prev_idx = (y / 2) * w2 + (x / 2);
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
                ESP_LOGI(TAG, "Motion detected: %d pixels changed", motion_pixels);
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

    uint32_t sum_r = 0;
    uint32_t sum_g = 0;
    uint32_t sum_b = 0;
    int count = 0;

    for (int y = ROI_Y_START; y < ROI_Y_START + ROI_HEIGHT; y++) {
        for (int x = ROI_X_START; x < ROI_X_START + ROI_WIDTH; x++) {
            const int idx = (y * IMG_WIDTH + x) * 3;
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
        *r = 0;
        *g = 0;
        *b = 0;
    }
}
