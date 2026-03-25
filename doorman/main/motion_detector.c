#include "motion_detector.h"
#include "camera_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "motion_detector";

/* PIR 传感器 GPIO 配置 */
#define PIR_GPIO            CONFIG_DOORMAN_PIR_GPIO
#define LED_GPIO            CONFIG_DOORMAN_LED_GPIO
#define MOTION_COOLDOWN_MS  (CONFIG_DOORMAN_MOTION_COOLDOWN_SEC * 1000)

/* 软件运动检测参数 */
#define SW_MOTION_THRESHOLD     (CONFIG_DOORMAN_SW_MOTION_THRESHOLD / 100.0f)
#define PACKAGE_DIFF_THRESHOLD  (CONFIG_DOORMAN_PACKAGE_DIFF_THRESHOLD / 100.0f)
#define PACKAGE_DETECT_DELAY_MS (CONFIG_DOORMAN_PACKAGE_DETECT_DELAY_SEC * 1000)

/* 帧差检测参数 */
#define MOTION_DETECT_WIDTH     80   /* 降采样宽度，用于运动检测 */
#define MOTION_DETECT_HEIGHT    60   /* 降采样高度 */
#define MOTION_DETECT_PIXELS    (MOTION_DETECT_WIDTH * MOTION_DETECT_HEIGHT)

static motion_callback_t s_callback = NULL;
static void             *s_callback_user_data = NULL;
static TaskHandle_t      s_motion_task_handle = NULL;
static QueueHandle_t     s_pir_event_queue = NULL;
static volatile bool     s_running = false;
static uint32_t          s_last_motion_time = 0;

/* 背景帧（用于快递检测） */
static uint8_t *s_background_frame = NULL;
static bool     s_background_valid = false;
static SemaphoreHandle_t s_bg_mutex = NULL;

/* PIR 中断服务程序 */
static void IRAM_ATTR pir_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(s_pir_event_queue, &gpio_num, NULL);
}

/**
 * @brief 计算两帧之间的差异比例
 * @param frame1 帧1数据
 * @param frame2 帧2数据
 * @param size 数据大小
 * @return 差异比例 (0.0-1.0)
 */
static float calculate_frame_diff(const uint8_t *frame1, const uint8_t *frame2, size_t size)
{
    if (frame1 == NULL || frame2 == NULL || size == 0) return 0.0f;

    uint32_t diff_count = 0;
    const int threshold = 30; /* 像素差异阈值 */

    for (size_t i = 0; i < size; i++) {
        int diff = (int)frame1[i] - (int)frame2[i];
        if (diff < 0) diff = -diff;
        if (diff > threshold) {
            diff_count++;
        }
    }

    return (float)diff_count / (float)size;
}

/**
 * @brief 更新背景帧
 */
static void update_background_frame(const uint8_t *frame, size_t size)
{
    if (xSemaphoreTake(s_bg_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;

    if (s_background_frame == NULL) {
        s_background_frame = (uint8_t *)malloc(size);
        if (s_background_frame == NULL) {
            ESP_LOGE(TAG, "分配背景帧内存失败");
            xSemaphoreGive(s_bg_mutex);
            return;
        }
    }

    memcpy(s_background_frame, frame, size);
    s_background_valid = true;

    xSemaphoreGive(s_bg_mutex);
}

/**
 * @brief 软件运动检测任务（帧差法）
 */
static void software_motion_detect_task(void *pvParameters)
{
    uint8_t *prev_frame = NULL;
    uint8_t *curr_frame = NULL;
    uint32_t prev_width = 0, prev_height = 0;
    uint32_t curr_width = 0, curr_height = 0;
    bool motion_active = false;
    uint32_t motion_start_time = 0;
    uint32_t last_motion_frame_time = 0;

    ESP_LOGI(TAG, "软件运动检测任务启动");

    /* 等待摄像头就绪 */
    while (!camera_manager_is_ready() && s_running) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    /* 采集初始背景帧 */
    vTaskDelay(pdMS_TO_TICKS(2000)); /* 等待摄像头稳定 */

    while (s_running) {
        /* 获取当前灰度帧 */
        esp_err_t ret = camera_manager_get_grayscale(&curr_frame, &curr_width, &curr_height);
        if (ret != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        size_t frame_size = curr_width * curr_height;
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        if (prev_frame != NULL && prev_width == curr_width && prev_height == curr_height) {
            /* 计算帧差 */
            float diff = calculate_frame_diff(prev_frame, curr_frame, frame_size);

            if (diff > SW_MOTION_THRESHOLD) {
                /* 检测到运动 */
                if (!motion_active) {
                    motion_active = true;
                    motion_start_time = now;
                    ESP_LOGI(TAG, "软件检测到运动，帧差: %.2f%%", diff * 100.0f);
                }
                last_motion_frame_time = now;

                /* 检查冷却期 */
                if (!motion_detector_is_in_cooldown()) {
                    s_last_motion_time = now;

                    if (s_callback) {
                        motion_event_t event = {
                            .type       = MOTION_EVENT_PERSON,
                            .timestamp  = now,
                            .diff_ratio = diff,
                        };
                        s_callback(&event, s_callback_user_data);
                    }
                }
            } else {
                /* 运动停止 */
                if (motion_active && (now - last_motion_frame_time) > 3000) {
                    motion_active = false;
                    ESP_LOGI(TAG, "运动停止，持续时间: %lu ms", (unsigned long)(now - motion_start_time));

                    /* 快递检测：运动停止后检查是否有物体留下 */
#if CONFIG_DOORMAN_ENABLE_PACKAGE_DETECTION
                    if (s_background_valid && s_background_frame != NULL) {
                        vTaskDelay(pdMS_TO_TICKS(PACKAGE_DETECT_DELAY_MS));

                        /* 重新获取当前帧 */
                        uint8_t *check_frame = NULL;
                        uint32_t check_w = 0, check_h = 0;
                        if (camera_manager_get_grayscale(&check_frame, &check_w, &check_h) == ESP_OK) {
                            if (xSemaphoreTake(s_bg_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                                float pkg_diff = calculate_frame_diff(s_background_frame, check_frame,
                                                                       check_w * check_h);
                                xSemaphoreGive(s_bg_mutex);

                                if (pkg_diff > PACKAGE_DIFF_THRESHOLD) {
                                    ESP_LOGI(TAG, "检测到可能有快递！差异: %.2f%%", pkg_diff * 100.0f);
                                    if (s_callback) {
                                        motion_event_t pkg_event = {
                                            .type       = MOTION_EVENT_PACKAGE,
                                            .timestamp  = (uint32_t)(esp_timer_get_time() / 1000),
                                            .diff_ratio = pkg_diff,
                                        };
                                        s_callback(&pkg_event, s_callback_user_data);
                                    }
                                } else {
                                    /* 更新背景帧 */
                                    update_background_frame(check_frame, check_w * check_h);
                                }
                            }
                            camera_manager_free_grayscale(check_frame);
                        }
                    } else {
                        /* 首次：保存当前帧为背景 */
                        update_background_frame(curr_frame, frame_size);
                    }
#endif
                }

                /* 定期更新背景帧（无运动时） */
                if (!motion_active && !s_background_valid) {
                    update_background_frame(curr_frame, frame_size);
                }
            }
        } else {
            /* 首帧，保存为背景 */
            update_background_frame(curr_frame, frame_size);
        }

        /* 更新前一帧 */
        if (prev_frame) {
            camera_manager_free_grayscale(prev_frame);
        }
        prev_frame  = curr_frame;
        prev_width  = curr_width;
        prev_height = curr_height;
        curr_frame  = NULL;

        /* 检测间隔 500ms */
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    /* 清理资源 */
    if (prev_frame) camera_manager_free_grayscale(prev_frame);
    if (curr_frame) camera_manager_free_grayscale(curr_frame);

    ESP_LOGI(TAG, "软件运动检测任务结束");
    vTaskDelete(NULL);
}

/**
 * @brief PIR 传感器事件处理任务
 */
static void pir_event_task(void *pvParameters)
{
    uint32_t gpio_num;
    ESP_LOGI(TAG, "PIR 事件处理任务启动");

    while (s_running) {
        if (xQueueReceive(s_pir_event_queue, &gpio_num, pdMS_TO_TICKS(100)) == pdTRUE) {
            int level = gpio_get_level((gpio_num_t)gpio_num);
            if (level == 1) {
                /* PIR 高电平 = 检测到运动 */
                uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
                ESP_LOGI(TAG, "PIR 传感器触发！");

                /* 点亮 LED */
                gpio_set_level(LED_GPIO, 1);

                if (!motion_detector_is_in_cooldown()) {
                    s_last_motion_time = now;

                    if (s_callback) {
                        motion_event_t event = {
                            .type       = MOTION_EVENT_PERSON,
                            .timestamp  = now,
                            .diff_ratio = 1.0f, /* PIR 触发，差异设为最大 */
                        };
                        s_callback(&event, s_callback_user_data);
                    }
                } else {
                    ESP_LOGD(TAG, "PIR 触发，但处于冷却期，忽略");
                }
            } else {
                /* PIR 低电平 = 运动停止 */
                gpio_set_level(LED_GPIO, 0);
                ESP_LOGD(TAG, "PIR 传感器复位");
            }
        }
    }

    ESP_LOGI(TAG, "PIR 事件处理任务结束");
    vTaskDelete(NULL);
}

esp_err_t motion_detector_init(void)
{
    /* 创建互斥锁 */
    s_bg_mutex = xSemaphoreCreateMutex();
    if (s_bg_mutex == NULL) {
        ESP_LOGE(TAG, "创建背景帧互斥锁失败");
        return ESP_ERR_NO_MEM;
    }

    /* 创建 PIR 事件队列 */
    s_pir_event_queue = xQueueCreate(10, sizeof(uint32_t));
    if (s_pir_event_queue == NULL) {
        ESP_LOGE(TAG, "创建 PIR 事件队列失败");
        return ESP_ERR_NO_MEM;
    }

    /* 配置 PIR GPIO */
    gpio_config_t pir_conf = {
        .intr_type    = GPIO_INTR_ANYEDGE,  /* 上升沿和下降沿都触发 */
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIR_GPIO),
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&pir_conf));

    /* 配置 LED GPIO */
    gpio_config_t led_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_conf));
    gpio_set_level(LED_GPIO, 0);

    /* 安装 GPIO 中断服务 */
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIR_GPIO, pir_isr_handler, (void *)PIR_GPIO));

    ESP_LOGI(TAG, "运动检测器初始化完成 (PIR GPIO: %d, LED GPIO: %d)", PIR_GPIO, LED_GPIO);
    return ESP_OK;
}

void motion_detector_register_callback(motion_callback_t callback, void *user_data)
{
    s_callback           = callback;
    s_callback_user_data = user_data;
}

esp_err_t motion_detector_start(void)
{
    if (s_running) {
        ESP_LOGW(TAG, "运动检测器已在运行");
        return ESP_OK;
    }

    s_running = true;

    /* 启动 PIR 事件处理任务 */
    BaseType_t ret = xTaskCreate(pir_event_task, "pir_task", 4096, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建 PIR 任务失败");
        s_running = false;
        return ESP_FAIL;
    }

#if CONFIG_DOORMAN_ENABLE_SW_MOTION_DETECT
    /* 启动软件运动检测任务 */
    ret = xTaskCreate(software_motion_detect_task, "sw_motion_task", 8192, NULL, 4,
                      &s_motion_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建软件运动检测任务失败");
        /* 不返回错误，PIR 检测仍然可用 */
    }
#endif

    ESP_LOGI(TAG, "运动检测器已启动");
    return ESP_OK;
}

void motion_detector_stop(void)
{
    s_running = false;
    if (s_motion_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        s_motion_task_handle = NULL;
    }
    ESP_LOGI(TAG, "运动检测器已停止");
}

void motion_detector_trigger(void)
{
    uint32_t gpio_num = PIR_GPIO;
    xQueueSend(s_pir_event_queue, &gpio_num, 0);
}

uint32_t motion_detector_get_last_motion_time(void)
{
    return s_last_motion_time;
}

bool motion_detector_is_in_cooldown(void)
{
    if (s_last_motion_time == 0) return false;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    return (now - s_last_motion_time) < MOTION_COOLDOWN_MS;
}