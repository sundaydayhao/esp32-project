#include "camera_app.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "camera_app";

static bool s_ready = false;
static SemaphoreHandle_t s_camera_lock = NULL;

static framesize_t camera_frame_size(void)
{
#if CONFIG_CAR_CAMERA_FRAME_SIZE_VGA
    return FRAMESIZE_VGA;
#elif CONFIG_CAR_CAMERA_FRAME_SIZE_QQVGA
    return FRAMESIZE_QQVGA;
#else
    return FRAMESIZE_QVGA;
#endif
}

esp_err_t camera_app_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    s_camera_lock = xSemaphoreCreateMutex();
    if (s_camera_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    camera_config_t config = {
        .pin_pwdn = CONFIG_CAR_CAM_PIN_PWDN,
        .pin_reset = CONFIG_CAR_CAM_PIN_RESET,
        .pin_xclk = CONFIG_CAR_CAM_PIN_XCLK,
        .pin_sccb_sda = CONFIG_CAR_CAM_PIN_SIOD,
        .pin_sccb_scl = CONFIG_CAR_CAM_PIN_SIOC,
        .pin_d7 = CONFIG_CAR_CAM_PIN_D7,
        .pin_d6 = CONFIG_CAR_CAM_PIN_D6,
        .pin_d5 = CONFIG_CAR_CAM_PIN_D5,
        .pin_d4 = CONFIG_CAR_CAM_PIN_D4,
        .pin_d3 = CONFIG_CAR_CAM_PIN_D3,
        .pin_d2 = CONFIG_CAR_CAM_PIN_D2,
        .pin_d1 = CONFIG_CAR_CAM_PIN_D1,
        .pin_d0 = CONFIG_CAR_CAM_PIN_D0,
        .pin_vsync = CONFIG_CAR_CAM_PIN_VSYNC,
        .pin_href = CONFIG_CAR_CAM_PIN_HREF,
        .pin_pclk = CONFIG_CAR_CAM_PIN_PCLK,
        .xclk_freq_hz = CONFIG_CAR_CAM_XCLK_FREQ_HZ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = camera_frame_size(),
        .jpeg_quality = CONFIG_CAR_CAMERA_JPEG_QUALITY,
        .fb_count = CONFIG_CAR_CAMERA_FB_COUNT,
        .grab_mode = CAMERA_GRAB_LATEST,
    };

#if CONFIG_SPIRAM
    config.fb_location = CAMERA_FB_IN_PSRAM;
#else
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
    config.frame_size = FRAMESIZE_QVGA;
#endif

    esp_err_t ret = esp_camera_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_camera_lock);
        s_camera_lock = NULL;
        return ret;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL) {
        sensor->set_brightness(sensor, 0);
        sensor->set_contrast(sensor, 0);
        sensor->set_saturation(sensor, 0);
        sensor->set_whitebal(sensor, 1);
        sensor->set_awb_gain(sensor, 1);
        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_gain_ctrl(sensor, 1);
        sensor->set_hmirror(sensor, 0);
        sensor->set_vflip(sensor, 0);
        ESP_LOGI(TAG, "camera sensor pid=0x%02x", sensor->id.PID);
    }

    s_ready = true;
    ESP_LOGI(TAG, "camera ready");
    return ESP_OK;
}

bool camera_app_is_ready(void)
{
    return s_ready;
}

camera_fb_t *camera_app_fb_get(void)
{
    if (!s_ready || s_camera_lock == NULL) {
        return NULL;
    }

    if (xSemaphoreTake(s_camera_lock, pdMS_TO_TICKS(3000)) != pdTRUE) {
        return NULL;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        xSemaphoreGive(s_camera_lock);
    }
    return fb;
}

void camera_app_fb_return(camera_fb_t *fb)
{
    if (fb != NULL) {
        esp_camera_fb_return(fb);
    }
    if (s_camera_lock != NULL) {
        xSemaphoreGive(s_camera_lock);
    }
}
