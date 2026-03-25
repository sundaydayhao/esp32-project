#include "camera_init.h"

#include <string.h>

#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sensor.h"

static const char *TAG = "camera_init";
static const int CAMERA_XCLK_FREQ_HZ_PRIMARY = 20000000;
static const int CAMERA_XCLK_FREQ_HZ_FALLBACK = 10000000;
static const int CAMERA_SENSOR_STABILIZE_DELAY_MS = 1500;
static const int CAMERA_CAPTURE_RETRY_DELAY_MS = 2000;
static const int CAMERA_SENSOR_RESET_DELAY_MS = 1000;
static const int CAMERA_WARMUP_FRAME_COUNT = 2;
static const int CAMERA_WARMUP_FRAME_DELAY_MS = 120;

static bool s_camera_initialized = false;
static int s_camera_recovery_stage = 0;

enum {
    CAMERA_RECOVERY_STAGE_PRIMARY = 0,
    CAMERA_RECOVERY_STAGE_LOW_XCLK = 1,
    CAMERA_RECOVERY_STAGE_CONSERVATIVE = 2,
    CAMERA_RECOVERY_STAGE_RGB565_FALLBACK = 3,
    CAMERA_RECOVERY_STAGE_GRAYSCALE_FALLBACK = 4,
    CAMERA_RECOVERY_STAGE_MAX = CAMERA_RECOVERY_STAGE_GRAYSCALE_FALLBACK,
};

static framesize_t camera_translate_legacy_frame_size(int config_value)
{
    switch (config_value) {
        case 0:
            return FRAMESIZE_96X96;
        case 1:
            return FRAMESIZE_QQVGA;
        case 2:
            return FRAMESIZE_QCIF;
        case 3:
            return FRAMESIZE_HQVGA;
        case 4:
            return FRAMESIZE_240X240;
        case 5:
            return FRAMESIZE_QVGA;
        case 6:
            return FRAMESIZE_CIF;
        case 7:
            return FRAMESIZE_HVGA;
        case 8:
            return FRAMESIZE_VGA;
        case 9:
            return FRAMESIZE_SVGA;
        case 10:
            return FRAMESIZE_XGA;
        case 11:
            return FRAMESIZE_HD;
        case 12:
            return FRAMESIZE_SXGA;
        case 13:
            return FRAMESIZE_UXGA;
        default:
            return FRAMESIZE_VGA;
    }
}

static bool camera_has_psram(void)
{
    return esp_psram_is_initialized();
}

static framesize_t camera_select_frame_size(bool has_psram)
{
    framesize_t configured = camera_translate_legacy_frame_size(CONFIG_CAMERA_FRAME_SIZE);

    if (!has_psram && configured > FRAMESIZE_QVGA) {
        ESP_LOGW(TAG, "PSRAM unavailable, lowering frame size to QVGA");
        return FRAMESIZE_QVGA;
    }

    return configured;
}

static camera_fb_location_t camera_select_fb_location(bool has_psram)
{
    return has_psram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
}

static const char *camera_profile_name(int recovery_stage)
{
    switch (recovery_stage) {
        case CAMERA_RECOVERY_STAGE_PRIMARY:
            return "primary";
        case CAMERA_RECOVERY_STAGE_LOW_XCLK:
            return "low-xclk";
        case CAMERA_RECOVERY_STAGE_CONSERVATIVE:
            return "conservative";
        case CAMERA_RECOVERY_STAGE_RGB565_FALLBACK:
            return "rgb565-fallback";
        case CAMERA_RECOVERY_STAGE_GRAYSCALE_FALLBACK:
            return "grayscale-fallback";
        default:
            return "unknown";
    }
}

static framesize_t camera_select_profile_frame_size(framesize_t configured,
                                                    bool has_psram,
                                                    int recovery_stage)
{
    if (recovery_stage == CAMERA_RECOVERY_STAGE_RGB565_FALLBACK) {
        if (has_psram) {
            return FRAMESIZE_VGA;
        }
        if (configured > FRAMESIZE_CIF) {
            return FRAMESIZE_CIF;
        }
    }

    if (recovery_stage == CAMERA_RECOVERY_STAGE_CONSERVATIVE ||
        recovery_stage == CAMERA_RECOVERY_STAGE_GRAYSCALE_FALLBACK) {
        if (configured > FRAMESIZE_QVGA) {
            return FRAMESIZE_QVGA;
        }
    }

    if (recovery_stage == CAMERA_RECOVERY_STAGE_LOW_XCLK &&
        configured > FRAMESIZE_CIF) {
        return FRAMESIZE_CIF;
    }

    if (recovery_stage == CAMERA_RECOVERY_STAGE_PRIMARY &&
        configured > FRAMESIZE_VGA) {
        return FRAMESIZE_VGA;
    }

    if (recovery_stage > CAMERA_RECOVERY_STAGE_GRAYSCALE_FALLBACK &&
        configured > FRAMESIZE_QVGA) {
        return FRAMESIZE_QVGA;
    }

    return configured;
}

static int camera_select_profile_xclk(int recovery_stage)
{
    return recovery_stage == CAMERA_RECOVERY_STAGE_PRIMARY ?
           CAMERA_XCLK_FREQ_HZ_PRIMARY :
           CAMERA_XCLK_FREQ_HZ_FALLBACK;
}

static int camera_select_profile_jpeg_quality(int recovery_stage)
{
    int jpeg_quality = CONFIG_CAMERA_JPEG_QUALITY > 30 ? 30 : CONFIG_CAMERA_JPEG_QUALITY;

    if (recovery_stage >= CAMERA_RECOVERY_STAGE_CONSERVATIVE && jpeg_quality < 18) {
        return 18;
    }

    return jpeg_quality;
}

static pixformat_t camera_select_profile_pixel_format(int recovery_stage)
{
    switch (recovery_stage) {
        case CAMERA_RECOVERY_STAGE_RGB565_FALLBACK:
            return PIXFORMAT_RGB565;
        case CAMERA_RECOVERY_STAGE_GRAYSCALE_FALLBACK:
            return PIXFORMAT_GRAYSCALE;
        default:
            return PIXFORMAT_JPEG;
    }
}

static camera_config_t camera_build_config(int recovery_stage)
{
    const bool has_psram = camera_has_psram();
    const framesize_t configured_frame_size = camera_select_frame_size(has_psram);
    const framesize_t frame_size = camera_select_profile_frame_size(configured_frame_size,
                                                                    has_psram,
                                                                    recovery_stage);

    camera_config_t config;
    memset(&config, 0, sizeof(config));

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CONFIG_CAM_PIN_D0;
    config.pin_d1 = CONFIG_CAM_PIN_D1;
    config.pin_d2 = CONFIG_CAM_PIN_D2;
    config.pin_d3 = CONFIG_CAM_PIN_D3;
    config.pin_d4 = CONFIG_CAM_PIN_D4;
    config.pin_d5 = CONFIG_CAM_PIN_D5;
    config.pin_d6 = CONFIG_CAM_PIN_D6;
    config.pin_d7 = CONFIG_CAM_PIN_D7;
    config.pin_xclk = CONFIG_CAM_PIN_XCLK;
    config.pin_pclk = CONFIG_CAM_PIN_PCLK;
    config.pin_vsync = CONFIG_CAM_PIN_VSYNC;
    config.pin_href = CONFIG_CAM_PIN_HREF;
    config.pin_sccb_sda = CONFIG_CAM_PIN_SIOD;
    config.pin_sccb_scl = CONFIG_CAM_PIN_SIOC;
    config.pin_pwdn = CONFIG_CAM_PIN_PWDN;
    config.pin_reset = CONFIG_CAM_PIN_RESET;
    config.xclk_freq_hz = camera_select_profile_xclk(recovery_stage);
    config.pixel_format = camera_select_profile_pixel_format(recovery_stage);
    config.frame_size = frame_size;
    config.jpeg_quality = camera_select_profile_jpeg_quality(recovery_stage);
    /* Always use a single frame buffer with GRAB_WHEN_EMPTY.
     * This stops the DMA from running continuously between captures,
     * preventing the sensor from accumulating corrupt state (NO-SOI)
     * after power-hungry operations like WiFi transmission. */
    config.fb_count = 1;
    config.fb_location = camera_select_fb_location(has_psram);
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    return config;
}

static void camera_tune_sensor(sensor_t *sensor)
{
    if (sensor == NULL) {
        return;
    }

    ESP_LOGI(TAG, "Camera sensor PID: 0x%04X", sensor->id.PID);

    if (sensor->id.PID == OV2640_PID) {
        sensor->set_brightness(sensor, 0);
        sensor->set_whitebal(sensor, 1);
        sensor->set_awb_gain(sensor, 1);
        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_aec2(sensor, 0);
        sensor->set_gain_ctrl(sensor, 1);
        sensor->set_bpc(sensor, 0);
        sensor->set_wpc(sensor, 1);
        sensor->set_raw_gma(sensor, 1);
        sensor->set_lenc(sensor, 1);
        sensor->set_hmirror(sensor, 0);
        sensor->set_vflip(sensor, 0);
        sensor->set_dcw(sensor, 1);
        sensor->set_colorbar(sensor, 0);

        if (s_camera_recovery_stage == CAMERA_RECOVERY_STAGE_RGB565_FALLBACK) {
            sensor->set_contrast(sensor, 1);
            sensor->set_saturation(sensor, 1);
        } else if (s_camera_recovery_stage == CAMERA_RECOVERY_STAGE_GRAYSCALE_FALLBACK) {
            sensor->set_contrast(sensor, 1);
            sensor->set_saturation(sensor, 0);
        } else {
            sensor->set_contrast(sensor, 0);
            sensor->set_saturation(sensor, 0);
        }

        sensor->set_sharpness(sensor, 0);
    }
}

static void camera_restore_sensor_profile(sensor_t *sensor)
{
    if (sensor == NULL) {
        return;
    }

    sensor->set_framesize(sensor, camera_select_profile_frame_size(
                                      camera_select_frame_size(camera_has_psram()),
                                      camera_has_psram(),
                                      s_camera_recovery_stage));
    sensor->set_pixformat(sensor, camera_select_profile_pixel_format(s_camera_recovery_stage));
    if (camera_select_profile_pixel_format(s_camera_recovery_stage) == PIXFORMAT_JPEG) {
        sensor->set_quality(sensor, camera_select_profile_jpeg_quality(s_camera_recovery_stage));
    }
    camera_tune_sensor(sensor);
}

static void camera_warmup_pipeline(void)
{
    for (int i = 0; i < CAMERA_WARMUP_FRAME_COUNT; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL) {
            ESP_LOGW(TAG, "Warmup frame %d/%d unavailable", i + 1, CAMERA_WARMUP_FRAME_COUNT);
        } else {
            esp_camera_fb_return(fb);
        }
        vTaskDelay(pdMS_TO_TICKS(CAMERA_WARMUP_FRAME_DELAY_MS));
    }
}

static bool camera_frame_is_usable(const camera_fb_t *fb)
{
    if (fb == NULL || fb->buf == NULL || fb->len == 0) {
        return false;
    }

    switch (fb->format) {
        case PIXFORMAT_JPEG:
            if (fb->len < 4) {
                return false;
            }
            /* Check JPEG SOI (0xFFD8) and EOI (0xFFD9) markers only.
             * esp_jpeg_get_image_info is intentionally avoided here: it can reject
             * valid JPEG frames produced by the OV2640, causing spurious failures. */
            return fb->buf[0] == 0xff &&
                   fb->buf[1] == 0xd8 &&
                   fb->buf[fb->len - 2] == 0xff &&
                   fb->buf[fb->len - 1] == 0xd9;
        case PIXFORMAT_RGB565:
            return fb->width > 0 &&
                   fb->height > 0 &&
                   fb->len >= fb->width * fb->height * 2;
        case PIXFORMAT_GRAYSCALE:
            return fb->width > 0 &&
                   fb->height > 0 &&
                   fb->len >= fb->width * fb->height;
        default:
            return fb->width > 0 && fb->height > 0;
    }
}

static camera_fb_t *camera_try_capture(int attempts, int delay_ms)
{
    for (int i = 0; i < attempts; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL) {
            ESP_LOGW(TAG, "Camera returned null frame on attempt %d", i + 1);
        } else if (!camera_frame_is_usable(fb)) {
            ESP_LOGW(TAG, "Frame unusable on attempt %d: %zux%zu len=%zu fmt=%d first=[%02x %02x] last=[%02x %02x]",
                     i + 1, fb->width, fb->height, fb->len, fb->format,
                     fb->len > 0 ? fb->buf[0] : 0,
                     fb->len > 1 ? fb->buf[1] : 0,
                     fb->len > 1 ? fb->buf[fb->len - 2] : 0,
                     fb->len > 0 ? fb->buf[fb->len - 1] : 0);
            esp_camera_fb_return(fb);
        } else {
            ESP_LOGI(TAG, "Capture complete: %zu bytes, format=%d", fb->len, fb->format);
            return fb;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    return NULL;
}

static esp_err_t camera_apply_profile(int recovery_stage)
{
    camera_config_t config = camera_build_config(recovery_stage);

    if (s_camera_initialized) {
        esp_err_t deinit_err = esp_camera_deinit();
        if (deinit_err != ESP_OK) {
            ESP_LOGW(TAG, "Camera deinit before reconfigure failed: %s", esp_err_to_name(deinit_err));
        }
        s_camera_initialized = false;
        vTaskDelay(pdMS_TO_TICKS(CAMERA_SENSOR_RESET_DELAY_MS));
    }

    ESP_LOGI(TAG, "Initializing camera profile: %s", camera_profile_name(recovery_stage));
    ESP_LOGI(TAG, "PSRAM available: %s", camera_has_psram() ? "yes" : "no");
    ESP_LOGI(TAG,
             "Pins: PWDN=%d RESET=%d XCLK=%d SIOD=%d SIOC=%d D7=%d D6=%d D5=%d D4=%d D3=%d D2=%d D1=%d D0=%d VSYNC=%d HREF=%d PCLK=%d",
             config.pin_pwdn, config.pin_reset, config.pin_xclk,
             config.pin_sccb_sda, config.pin_sccb_scl,
             config.pin_d7, config.pin_d6, config.pin_d5, config.pin_d4,
             config.pin_d3, config.pin_d2, config.pin_d1, config.pin_d0,
             config.pin_vsync, config.pin_href, config.pin_pclk);
    ESP_LOGI(TAG,
             "Camera config: frame_size=%d pixel_format=%d jpeg_quality=%d xclk=%d fb_count=%u grab_mode=%d fb_location=%d",
             config.frame_size, config.pixel_format, config.jpeg_quality, config.xclk_freq_hz,
             (unsigned)config.fb_count, config.grab_mode, config.fb_location);

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s (0x%x)", esp_err_to_name(err), err);
        return err;
    }

    camera_tune_sensor(esp_camera_sensor_get());
    s_camera_initialized = true;
    s_camera_recovery_stage = recovery_stage;

    vTaskDelay(pdMS_TO_TICKS(CAMERA_SENSOR_STABILIZE_DELAY_MS));
    camera_warmup_pipeline();
    return ESP_OK;
}

esp_err_t camera_init(void)
{
    return camera_apply_profile(CAMERA_RECOVERY_STAGE_RGB565_FALLBACK);
}

camera_fb_t *camera_capture(void)
{
    if (!s_camera_initialized) {
        ESP_LOGE(TAG, "Camera is not initialized");
        return NULL;
    }

    ESP_LOGI(TAG, "Capturing image with profile: %s", camera_profile_name(s_camera_recovery_stage));

    camera_fb_t *fb = camera_try_capture(5, CAMERA_CAPTURE_RETRY_DELAY_MS);
    if (fb != NULL) {
        return fb;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL) {
        for (int stage = s_camera_recovery_stage; stage <= CAMERA_RECOVERY_STAGE_MAX; stage++) {
            if (stage == s_camera_recovery_stage) {
                ESP_LOGW(TAG, "Capture failed, performing in-place sensor reset");
            } else {
                ESP_LOGW(TAG, "Capture still failing, switching in-place to profile: %s",
                         camera_profile_name(stage));
            }

            s_camera_recovery_stage = stage;
            sensor->reset(sensor);
            vTaskDelay(pdMS_TO_TICKS(CAMERA_SENSOR_RESET_DELAY_MS));
            camera_restore_sensor_profile(sensor);
            vTaskDelay(pdMS_TO_TICKS(CAMERA_SENSOR_STABILIZE_DELAY_MS));
            camera_warmup_pipeline();

            fb = camera_try_capture(3, CAMERA_CAPTURE_RETRY_DELAY_MS);
            if (fb != NULL) {
                return fb;
            }
        }
    }

    ESP_LOGE(TAG, "Capture failed after sensor reset");
    return NULL;
}
