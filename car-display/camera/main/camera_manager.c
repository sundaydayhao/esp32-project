/**
 * @file camera_manager.c
 * @brief 车辆盲区摄像头管理器
 *
 * 管理左右两个 OV2640 摄像头，通过 PWDN 引脚切换激活摄像头。
 * 两个摄像头共享同一组 DVP 数据线，通过电源控制实现分时复用。
 *
 * 硬件连接：
 *   - 两个 OV2640 共享 XCLK/PCLK/VSYNC/HREF/D0-D7/SDA/SCL
 *   - 左摄像头：独立 PWDN 和 RESET 引脚
 *   - 右摄像头：独立 PWDN 和 RESET 引脚
 *   - PWDN 高电平 = 摄像头掉电（禁用），低电平 = 正常工作
 */

#include "camera_manager.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "camera_mgr";

/* 摄像头统计信息 */
typedef struct {
    uint32_t frame_count;
    uint64_t last_fps_time_us;
    float    fps;
    uint32_t fps_frame_count;
} camera_stats_t;

/* 摄像头管理器内部状态 */
static struct {
    camera_manager_config_t config;
    camera_id_t             active_camera;
    bool                    initialized;
    SemaphoreHandle_t       mutex;
    camera_stats_t          stats[2];
} s_cam_mgr = {
    .active_camera = CAMERA_NONE,
    .initialized   = false,
};

/* ============================================================
 * 内部辅助函数
 * ============================================================ */

/**
 * @brief 设置摄像头 PWDN 引脚状态
 * @param camera_id 摄像头ID
 * @param power_down true=掉电(禁用), false=上电(启用)
 */
static void set_camera_power(camera_id_t camera_id, bool power_down)
{
    int pwdn_pin = (camera_id == CAMERA_LEFT)
                   ? s_cam_mgr.config.pin_left_pwdn
                   : s_cam_mgr.config.pin_right_pwdn;

    if (pwdn_pin >= 0) {
        /* OV2640 PWDN: 高电平掉电，低电平正常工作 */
        gpio_set_level(pwdn_pin, power_down ? 1 : 0);
    }
}

/**
 * @brief 复位摄像头
 * @param camera_id 摄像头ID
 */
static void reset_camera(camera_id_t camera_id)
{
    int reset_pin = (camera_id == CAMERA_LEFT)
                    ? s_cam_mgr.config.pin_left_reset
                    : s_cam_mgr.config.pin_right_reset;

    if (reset_pin >= 0) {
        gpio_set_level(reset_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(reset_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief 配置 GPIO 控制引脚
 */
static esp_err_t init_control_gpios(void)
{
    gpio_config_t io_conf = {
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    /* 配置左摄像头控制引脚 */
    if (s_cam_mgr.config.pin_left_pwdn >= 0) {
        io_conf.pin_bit_mask = (1ULL << s_cam_mgr.config.pin_left_pwdn);
        gpio_config(&io_conf);
        gpio_set_level(s_cam_mgr.config.pin_left_pwdn, 1); /* 初始掉电 */
    }

    if (s_cam_mgr.config.pin_left_reset >= 0) {
        io_conf.pin_bit_mask = (1ULL << s_cam_mgr.config.pin_left_reset);
        gpio_config(&io_conf);
        gpio_set_level(s_cam_mgr.config.pin_left_reset, 1);
    }

    /* 配置右摄像头控制引脚 */
    if (s_cam_mgr.config.pin_right_pwdn >= 0) {
        io_conf.pin_bit_mask = (1ULL << s_cam_mgr.config.pin_right_pwdn);
        gpio_config(&io_conf);
        gpio_set_level(s_cam_mgr.config.pin_right_pwdn, 1); /* 初始掉电 */
    }

    if (s_cam_mgr.config.pin_right_reset >= 0) {
        io_conf.pin_bit_mask = (1ULL << s_cam_mgr.config.pin_right_reset);
        gpio_config(&io_conf);
        gpio_set_level(s_cam_mgr.config.pin_right_reset, 1);
    }

    return ESP_OK;
}

/**
 * @brief 初始化 esp_camera 驱动（使用当前激活摄像头的引脚）
 */
static esp_err_t init_esp_camera(camera_id_t camera_id)
{
    int pwdn_pin  = (camera_id == CAMERA_LEFT)
                    ? s_cam_mgr.config.pin_left_pwdn
                    : s_cam_mgr.config.pin_right_pwdn;
    int reset_pin = (camera_id == CAMERA_LEFT)
                    ? s_cam_mgr.config.pin_left_reset
                    : s_cam_mgr.config.pin_right_reset;

    camera_config_t cam_config = {
        .pin_pwdn   = pwdn_pin,
        .pin_reset  = reset_pin,
        .pin_xclk   = s_cam_mgr.config.pin_xclk,
        .pin_sccb_sda = s_cam_mgr.config.pin_sda,
        .pin_sccb_scl = s_cam_mgr.config.pin_scl,
        .pin_d7     = s_cam_mgr.config.pin_d7,
        .pin_d6     = s_cam_mgr.config.pin_d6,
        .pin_d5     = s_cam_mgr.config.pin_d5,
        .pin_d4     = s_cam_mgr.config.pin_d4,
        .pin_d3     = s_cam_mgr.config.pin_d3,
        .pin_d2     = s_cam_mgr.config.pin_d2,
        .pin_d1     = s_cam_mgr.config.pin_d1,
        .pin_d0     = s_cam_mgr.config.pin_d0,
        .pin_vsync  = s_cam_mgr.config.pin_vsync,
        .pin_href   = s_cam_mgr.config.pin_href,
        .pin_pclk   = s_cam_mgr.config.pin_pclk,

        .xclk_freq_hz = s_cam_mgr.config.xclk_freq_hz,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_RGB565,
        .frame_size   = FRAMESIZE_QVGA,   /* 320x240 */

        .jpeg_quality = 12,
        .fb_count     = 2,                /* 双缓冲 */
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
    };

    esp_err_t ret = esp_camera_init(&cam_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "摄像头 %s 初始化失败: %s",
                 camera_id == CAMERA_LEFT ? "左" : "右",
                 esp_err_to_name(ret));
        return ret;
    }

    /* 配置摄像头传感器参数 */
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_brightness(sensor, 0);     /* 亮度 -2~2 */
        sensor->set_contrast(sensor, 0);       /* 对比度 -2~2 */
        sensor->set_saturation(sensor, 0);     /* 饱和度 -2~2 */
        sensor->set_sharpness(sensor, 0);      /* 锐度 */
        sensor->set_denoise(sensor, 1);        /* 降噪 */
        sensor->set_gainceiling(sensor, (gainceiling_t)0);
        sensor->set_quality(sensor, 10);
        sensor->set_colorbar(sensor, 0);
        sensor->set_whitebal(sensor, 1);       /* 自动白平衡 */
        sensor->set_gain_ctrl(sensor, 1);      /* 自动增益 */
        sensor->set_exposure_ctrl(sensor, 1);  /* 自动曝光 */
        sensor->set_hmirror(sensor, 0);        /* 水平镜像 */
        sensor->set_vflip(sensor, 0);          /* 垂直翻转 */
        sensor->set_awb_gain(sensor, 1);
        sensor->set_agc_gain(sensor, 0);
        sensor->set_aec_value(sensor, 300);
        sensor->set_aec2(sensor, 0);
        sensor->set_dcw(sensor, 1);
        sensor->set_bpc(sensor, 0);
        sensor->set_wpc(sensor, 1);
        sensor->set_raw_gma(sensor, 1);
        sensor->set_lenc(sensor, 1);
        sensor->set_special_effect(sensor, 0); /* 无特效 */
        sensor->set_wb_mode(sensor, 0);        /* 自动白平衡模式 */
        sensor->set_ae_level(sensor, 0);
    }

    ESP_LOGI(TAG, "摄像头 %s 初始化成功",
             camera_id == CAMERA_LEFT ? "左" : "右");
    return ESP_OK;
}

/* ============================================================
 * 公共接口实现
 * ============================================================ */

esp_err_t camera_manager_init(const camera_manager_config_t *config)
{
    if (s_cam_mgr.initialized) {
        ESP_LOGW(TAG, "摄像头管理器已初始化");
        return ESP_OK;
    }

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_cam_mgr.config, config, sizeof(camera_manager_config_t));
    memset(s_cam_mgr.stats, 0, sizeof(s_cam_mgr.stats));

    /* 创建互斥锁 */
    s_cam_mgr.mutex = xSemaphoreCreateMutex();
    if (!s_cam_mgr.mutex) {
        ESP_LOGE(TAG, "创建互斥锁失败");
        return ESP_ERR_NO_MEM;
    }

    /* 初始化控制 GPIO */
    esp_err_t ret = init_control_gpios();
    if (ret != ESP_OK) {
        vSemaphoreDelete(s_cam_mgr.mutex);
        return ret;
    }

    /* 两个摄像头初始均掉电 */
    set_camera_power(CAMERA_LEFT, true);
    set_camera_power(CAMERA_RIGHT, true);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* 默认激活左摄像头 */
    ret = camera_manager_select(CAMERA_LEFT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "激活左摄像头失败，尝试右摄像头");
        ret = camera_manager_select(CAMERA_RIGHT);
        if (ret != ESP_OK) {
            vSemaphoreDelete(s_cam_mgr.mutex);
            return ret;
        }
    }

    s_cam_mgr.initialized = true;
    ESP_LOGI(TAG, "摄像头管理器初始化完成");
    return ESP_OK;
}

void camera_manager_deinit(void)
{
    if (!s_cam_mgr.initialized) return;

    esp_camera_deinit();
    set_camera_power(CAMERA_LEFT, true);
    set_camera_power(CAMERA_RIGHT, true);

    if (s_cam_mgr.mutex) {
        vSemaphoreDelete(s_cam_mgr.mutex);
        s_cam_mgr.mutex = NULL;
    }

    s_cam_mgr.active_camera = CAMERA_NONE;
    s_cam_mgr.initialized   = false;
    ESP_LOGI(TAG, "摄像头管理器已释放");
}

esp_err_t camera_manager_select(camera_id_t camera_id)
{
    if (camera_id != CAMERA_LEFT && camera_id != CAMERA_RIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_cam_mgr.active_camera == camera_id) {
        return ESP_OK; /* 已是目标摄像头，无需切换 */
    }

    ESP_LOGD(TAG, "切换摄像头: %s -> %s",
             s_cam_mgr.active_camera == CAMERA_LEFT ? "左" : "右/无",
             camera_id == CAMERA_LEFT ? "左" : "右");

    /* 如果已有激活摄像头，先反初始化 */
    if (s_cam_mgr.active_camera != CAMERA_NONE) {
        esp_camera_deinit();
        set_camera_power(s_cam_mgr.active_camera, true); /* 掉电旧摄像头 */
        vTaskDelay(pdMS_TO_TICKS(s_cam_mgr.config.switch_delay_ms));
    }

    /* 上电目标摄像头 */
    set_camera_power(camera_id, false);
    vTaskDelay(pdMS_TO_TICKS(s_cam_mgr.config.switch_delay_ms));

    /* 复位目标摄像头 */
    reset_camera(camera_id);

    /* 初始化 esp_camera 驱动 */
    esp_err_t ret = init_esp_camera(camera_id);
    if (ret != ESP_OK) {
        set_camera_power(camera_id, true); /* 失败则掉电 */
        return ret;
    }

    s_cam_mgr.active_camera = camera_id;
    return ESP_OK;
}

camera_id_t camera_manager_get_active(void)
{
    return s_cam_mgr.active_camera;
}

esp_err_t camera_manager_capture(camera_id_t camera_id, camera_frame_t **frame_out)
{
    if (!s_cam_mgr.initialized || !frame_out) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_cam_mgr.mutex, portMAX_DELAY);

    /* 切换到目标摄像头 */
    esp_err_t ret = camera_manager_select(camera_id);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_cam_mgr.mutex);
        return ret;
    }

    /* 采集帧 */
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "摄像头 %s 采集帧失败",
                 camera_id == CAMERA_LEFT ? "左" : "右");
        xSemaphoreGive(s_cam_mgr.mutex);
        return ESP_FAIL;
    }

    /* 分配帧结构 */
    camera_frame_t *frame = (camera_frame_t *)malloc(sizeof(camera_frame_t));
    if (!frame) {
        esp_camera_fb_return(fb);
        xSemaphoreGive(s_cam_mgr.mutex);
        return ESP_ERR_NO_MEM;
    }

    /* 分配数据缓冲区并复制 */
    frame->buf = (uint8_t *)malloc(fb->len);
    if (!frame->buf) {
        free(frame);
        esp_camera_fb_return(fb);
        xSemaphoreGive(s_cam_mgr.mutex);
        return ESP_ERR_NO_MEM;
    }

    memcpy(frame->buf, fb->buf, fb->len);
    frame->len          = fb->len;
    frame->width        = fb->width;
    frame->height       = fb->height;
    frame->camera_id    = camera_id;
    frame->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    esp_camera_fb_return(fb);

    /* 更新统计信息 */
    camera_stats_t *stats = &s_cam_mgr.stats[camera_id];
    stats->frame_count++;
    stats->fps_frame_count++;

    uint64_t now_us = esp_timer_get_time();
    uint64_t elapsed_us = now_us - stats->last_fps_time_us;
    if (elapsed_us >= 1000000ULL) { /* 每秒更新一次FPS */
        stats->fps = (float)stats->fps_frame_count * 1000000.0f / (float)elapsed_us;
        stats->fps_frame_count = 0;
        stats->last_fps_time_us = now_us;
    }

    xSemaphoreGive(s_cam_mgr.mutex);

    *frame_out = frame;
    return ESP_OK;
}

void camera_frame_free(camera_frame_t *frame)
{
    if (frame) {
        if (frame->buf) {
            free(frame->buf);
        }
        free(frame);
    }
}

void camera_frame_flip_horizontal(camera_frame_t *frame)
{
    if (!frame || !frame->buf) return;

    uint16_t *pixels = (uint16_t *)frame->buf;
    uint32_t  w      = frame->width;
    uint32_t  h      = frame->height;

    for (uint32_t y = 0; y < h; y++) {
        uint16_t *row = pixels + y * w;
        for (uint32_t x = 0; x < w / 2; x++) {
            uint16_t tmp       = row[x];
            row[x]             = row[w - 1 - x];
            row[w - 1 - x]     = tmp;
        }
    }
}

void camera_frame_flip_vertical(camera_frame_t *frame)
{
    if (!frame || !frame->buf) return;

    uint16_t *pixels = (uint16_t *)frame->buf;
    uint32_t  w      = frame->width;
    uint32_t  h      = frame->height;
    uint16_t *tmp_row = (uint16_t *)malloc(w * sizeof(uint16_t));
    if (!tmp_row) return;

    for (uint32_t y = 0; y < h / 2; y++) {
        uint16_t *top = pixels + y * w;
        uint16_t *bot = pixels + (h - 1 - y) * w;
        memcpy(tmp_row, top, w * sizeof(uint16_t));
        memcpy(top, bot, w * sizeof(uint16_t));
        memcpy(bot, tmp_row, w * sizeof(uint16_t));
    }

    free(tmp_row);
}

void camera_manager_get_stats(camera_id_t camera_id, float *fps, uint32_t *frame_count)
{
    if (camera_id != CAMERA_LEFT && camera_id != CAMERA_RIGHT) return;

    camera_stats_t *stats = &s_cam_mgr.stats[camera_id];
    if (fps)         *fps         = stats->fps;
    if (frame_count) *frame_count = stats->frame_count;
}