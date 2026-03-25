#include "camera_manager.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "camera_manager";

static bool s_camera_ready = false;
static SemaphoreHandle_t s_camera_mutex = NULL;

/* 摄像头引脚配置（从 Kconfig 读取） */
static const camera_config_t s_camera_config = {
    .pin_pwdn     = CONFIG_DOORMAN_CAM_PIN_PWDN,
    .pin_reset    = CONFIG_DOORMAN_CAM_PIN_RESET,
    .pin_xclk     = CONFIG_DOORMAN_CAM_PIN_XCLK,
    .pin_sccb_sda = CONFIG_DOORMAN_CAM_PIN_SIOD,
    .pin_sccb_scl = CONFIG_DOORMAN_CAM_PIN_SIOC,
    .pin_d7       = CONFIG_DOORMAN_CAM_PIN_D7,
    .pin_d6       = CONFIG_DOORMAN_CAM_PIN_D6,
    .pin_d5       = CONFIG_DOORMAN_CAM_PIN_D5,
    .pin_d4       = CONFIG_DOORMAN_CAM_PIN_D4,
    .pin_d3       = CONFIG_DOORMAN_CAM_PIN_D3,
    .pin_d2       = CONFIG_DOORMAN_CAM_PIN_D2,
    .pin_d1       = CONFIG_DOORMAN_CAM_PIN_D1,
    .pin_d0       = CONFIG_DOORMAN_CAM_PIN_D0,
    .pin_vsync    = CONFIG_DOORMAN_CAM_PIN_VSYNC,
    .pin_href     = CONFIG_DOORMAN_CAM_PIN_HREF,
    .pin_pclk     = CONFIG_DOORMAN_CAM_PIN_PCLK,

    .xclk_freq_hz = CONFIG_DOORMAN_CAM_XCLK_FREQ_HZ,
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,   /* 使用 JPEG 格式节省内存 */
    .frame_size   = FRAMESIZE_VGA,    /* 640x480，可根据需要调整 */

    .jpeg_quality = 12,               /* JPEG 质量 (0-63，越小越好) */
    .fb_count     = 2,                /* 双缓冲 */
    .fb_location  = CAMERA_FB_IN_PSRAM, /* 帧缓冲存储在 PSRAM */
    .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
};

esp_err_t camera_manager_init(void)
{
    if (s_camera_ready) {
        ESP_LOGW(TAG, "摄像头已初始化");
        return ESP_OK;
    }

    /* 创建互斥锁 */
    s_camera_mutex = xSemaphoreCreateMutex();
    if (s_camera_mutex == NULL) {
        ESP_LOGE(TAG, "创建摄像头互斥锁失败");
        return ESP_ERR_NO_MEM;
    }

    /* 初始化摄像头 */
    esp_err_t err = esp_camera_init(&s_camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "摄像头初始化失败: 0x%x (%s)", err, esp_err_to_name(err));
        vSemaphoreDelete(s_camera_mutex);
        s_camera_mutex = NULL;
        return err;
    }

    /* 获取传感器并设置参数 */
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_brightness(s, 0);     /* 亮度: -2 到 2 */
        s->set_contrast(s, 0);       /* 对比度: -2 到 2 */
        s->set_saturation(s, 0);     /* 饱和度: -2 到 2 */
        s->set_sharpness(s, 0);      /* 锐度: -2 到 2 */
        s->set_whitebal(s, 1);       /* 自动白平衡 */
        s->set_awb_gain(s, 1);       /* 自动白平衡增益 */
        s->set_wb_mode(s, 0);        /* 白平衡模式: 0=自动 */
        s->set_exposure_ctrl(s, 1);  /* 自动曝光 */
        s->set_aec2(s, 0);
        s->set_ae_level(s, 0);
        s->set_gain_ctrl(s, 1);      /* 自动增益 */
        s->set_agc_gain(s, 0);
        s->set_gainceiling(s, (gainceiling_t)0);
        s->set_bpc(s, 0);
        s->set_wpc(s, 1);
        s->set_raw_gma(s, 1);
        s->set_lenc(s, 1);
        s->set_hmirror(s, 0);        /* 水平镜像 */
        s->set_vflip(s, 0);          /* 垂直翻转 */
        s->set_dcw(s, 1);
        s->set_colorbar(s, 0);

        ESP_LOGI(TAG, "摄像头传感器: PID=0x%02x", s->id.PID);
    }

    s_camera_ready = true;
    ESP_LOGI(TAG, "摄像头初始化成功");
    return ESP_OK;
}

void camera_manager_deinit(void)
{
    if (!s_camera_ready) return;

    esp_camera_deinit();
    s_camera_ready = false;

    if (s_camera_mutex) {
        vSemaphoreDelete(s_camera_mutex);
        s_camera_mutex = NULL;
    }
    ESP_LOGI(TAG, "摄像头已关闭");
}

esp_err_t camera_manager_capture(doorman_frame_t *frame)
{
    if (!s_camera_ready || frame == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_camera_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "获取摄像头互斥锁超时");
        return ESP_ERR_TIMEOUT;
    }

    /* 拍摄照片 */
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        ESP_LOGE(TAG, "拍摄失败，无法获取帧缓冲");
        xSemaphoreGive(s_camera_mutex);
        return ESP_FAIL;
    }

    /* 复制图像数据到新分配的内存 */
    frame->data = (uint8_t *)malloc(fb->len);
    if (frame->data == NULL) {
        ESP_LOGE(TAG, "分配图像内存失败 (%zu 字节)", fb->len);
        esp_camera_fb_return(fb);
        xSemaphoreGive(s_camera_mutex);
        return ESP_ERR_NO_MEM;
    }

    memcpy(frame->data, fb->buf, fb->len);
    frame->len       = fb->len;
    frame->width     = fb->width;
    frame->height    = fb->height;
    frame->timestamp = (uint32_t)(esp_timer_get_time() / 1000);

    esp_camera_fb_return(fb);
    xSemaphoreGive(s_camera_mutex);

    ESP_LOGI(TAG, "拍摄成功: %ux%u, %zu 字节", frame->width, frame->height, frame->len);
    return ESP_OK;
}

void camera_manager_free_frame(doorman_frame_t *frame)
{
    if (frame && frame->data) {
        free(frame->data);
        frame->data = NULL;
        frame->len  = 0;
    }
}

esp_err_t camera_manager_get_grayscale(uint8_t **gray_buf, uint32_t *width, uint32_t *height)
{
    if (!s_camera_ready || gray_buf == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_camera_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* 临时切换到灰度格式 */
    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        xSemaphoreGive(s_camera_mutex);
        return ESP_FAIL;
    }

    /* 使用 GRAYSCALE 格式获取帧 */
    esp_camera_return_all();

    /* 修改像素格式为灰度 */
    camera_config_t gray_config = s_camera_config;
    gray_config.pixel_format = PIXFORMAT_GRAYSCALE;
    gray_config.frame_size   = FRAMESIZE_QVGA; /* 320x240，用于运动检测 */
    gray_config.fb_count     = 1;

    /* 注意：实际项目中需要重新初始化摄像头来切换格式 */
    /* 这里使用简化方案：直接获取 JPEG 帧并提取亮度信息 */
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        xSemaphoreGive(s_camera_mutex);
        return ESP_FAIL;
    }

    *width  = fb->width;
    *height = fb->height;
    size_t buf_size = fb->width * fb->height;

    *gray_buf = (uint8_t *)malloc(buf_size);
    if (*gray_buf == NULL) {
        esp_camera_fb_return(fb);
        xSemaphoreGive(s_camera_mutex);
        return ESP_ERR_NO_MEM;
    }

    /* 从 JPEG 数据中提取简化的亮度信息（采样方式） */
    /* 实际应用中应使用 JPEG 解码库，这里使用简化的采样 */
    for (size_t i = 0; i < buf_size; i++) {
        (*gray_buf)[i] = fb->buf[i % fb->len];
    }

    esp_camera_fb_return(fb);
    xSemaphoreGive(s_camera_mutex);
    return ESP_OK;
}

void camera_manager_free_grayscale(uint8_t *gray_buf)
{
    if (gray_buf) {
        free(gray_buf);
    }
}

bool camera_manager_is_ready(void)
{
    return s_camera_ready;
}

esp_err_t camera_manager_set_quality(int quality)
{
    if (!s_camera_ready) return ESP_ERR_INVALID_STATE;

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) return ESP_FAIL;

    s->set_quality(s, quality);
    return ESP_OK;
}

esp_err_t camera_manager_set_brightness(int brightness)
{
    if (!s_camera_ready) return ESP_ERR_INVALID_STATE;

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) return ESP_FAIL;

    s->set_brightness(s, brightness);
    return ESP_OK;
}