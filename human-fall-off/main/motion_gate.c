/*
 * 运动门控模块实现
 * 基于 CSI 幅度变化检测人体活动
 */

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "motion_gate.h"
#include "event_detector.h"

static const char *TAG = "MOTION_GATE";

/* 滑动窗口缓冲区 */
#define MAX_WINDOW_SIZE 100

/* 运动门控模块状态 */
static struct {
    bool initialized;
    motion_gate_config_t config;
    motion_state_t current_state;
    motion_state_callback_t callback;
    void *callback_ctx;
    
    // CSI 幅度历史缓冲区
    float amplitude_buffer[MAX_WINDOW_SIZE];
    uint32_t buffer_index;
    uint32_t buffer_count;
    
    // 统计信息
    float current_intensity;
    float baseline_amplitude;
    uint32_t active_count;
    uint32_t idle_count;
} s_motion_ctx = {0};

/**
 * @brief 计算 CSI 数据的平均幅度
 */
static float calculate_amplitude(const csi_data_t *csi_data)
{
    if (csi_data == NULL || csi_data->len == 0) {
        return 0.0f;
    }
    
    float sum = 0.0f;
    uint32_t count = csi_data->len / 2;  // I/Q 对数量
    
    for (uint32_t i = 0; i < count; i++) {
        int8_t i_val = csi_data->data[i * 2];
        int8_t q_val = csi_data->data[i * 2 + 1];
        
        // 计算幅度: sqrt(I^2 + Q^2)
        float amplitude = sqrtf((float)(i_val * i_val + q_val * q_val));
        sum += amplitude;
    }
    
    return sum / count;
}

/**
 * @brief 计算滑动窗口的方差
 */
static float calculate_variance(void)
{
    if (s_motion_ctx.buffer_count < 2) {
        return 0.0f;
    }
    
    // 计算均值
    float mean = 0.0f;
    for (uint32_t i = 0; i < s_motion_ctx.buffer_count; i++) {
        mean += s_motion_ctx.amplitude_buffer[i];
    }
    mean /= s_motion_ctx.buffer_count;
    
    // 计算方差
    float variance = 0.0f;
    for (uint32_t i = 0; i < s_motion_ctx.buffer_count; i++) {
        float diff = s_motion_ctx.amplitude_buffer[i] - mean;
        variance += diff * diff;
    }
    variance /= s_motion_ctx.buffer_count;
    
    return variance;
}

/**
 * @brief 更新运动状态
 */
static void update_motion_state(float intensity)
{
    motion_state_t new_state;
    
    // 根据强度判断状态
    if (intensity > s_motion_ctx.config.threshold) {
        new_state = MOTION_STATE_ACTIVE;
        s_motion_ctx.active_count++;
        s_motion_ctx.idle_count = 0;
    } else {
        new_state = MOTION_STATE_IDLE;
        s_motion_ctx.idle_count++;
        s_motion_ctx.active_count = 0;
    }
    
    // 状态变化时触发回调
    if (new_state != s_motion_ctx.current_state) {
        // 需要连续几次才确认状态变化，避免抖动
        uint32_t confirm_threshold = 3;
        
        if ((new_state == MOTION_STATE_ACTIVE && s_motion_ctx.active_count >= confirm_threshold) ||
            (new_state == MOTION_STATE_IDLE && s_motion_ctx.idle_count >= confirm_threshold)) {
            
            ESP_LOGI(TAG, "运动状态变化: %s -> %s (强度: %.2f)",
                     s_motion_ctx.current_state == MOTION_STATE_IDLE ? "空闲" : "活动",
                     new_state == MOTION_STATE_IDLE ? "空闲" : "活动",
                     intensity);
            
            s_motion_ctx.current_state = new_state;
            
            // 触发回调
            if (s_motion_ctx.callback != NULL) {
                s_motion_ctx.callback(new_state, s_motion_ctx.callback_ctx);
            }
            
            // 通知事件检测器
            event_detector_on_motion_change(new_state);
        }
    }
}

esp_err_t motion_gate_init(const motion_gate_config_t *config)
{
    if (s_motion_ctx.initialized) {
        ESP_LOGW(TAG, "运动门控已初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (config == NULL) {
        ESP_LOGE(TAG, "配置参数为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查参数有效性
    if (config->window_size == 0 || config->window_size > MAX_WINDOW_SIZE) {
        ESP_LOGE(TAG, "窗口大小无效: %lu (最大: %d)", config->window_size, MAX_WINDOW_SIZE);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 保存配置
    memcpy(&s_motion_ctx.config, config, sizeof(motion_gate_config_t));
    
    // 初始化状态
    s_motion_ctx.current_state = MOTION_STATE_UNKNOWN;
    s_motion_ctx.buffer_index = 0;
    s_motion_ctx.buffer_count = 0;
    s_motion_ctx.current_intensity = 0.0f;
    s_motion_ctx.baseline_amplitude = 0.0f;
    s_motion_ctx.active_count = 0;
    s_motion_ctx.idle_count = 0;
    
    s_motion_ctx.initialized = true;
    
    ESP_LOGI(TAG, "运动门控初始化成功 - 阈值: %.2f, 窗口: %lu",
             config->threshold, config->window_size);
    
    return ESP_OK;
}

esp_err_t motion_gate_process_csi(const csi_data_t *csi_data)
{
    if (!s_motion_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (csi_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 计算当前幅度
    float amplitude = calculate_amplitude(csi_data);
    
    // 添加到滑动窗口
    s_motion_ctx.amplitude_buffer[s_motion_ctx.buffer_index] = amplitude;
    s_motion_ctx.buffer_index = (s_motion_ctx.buffer_index + 1) % s_motion_ctx.config.window_size;
    
    if (s_motion_ctx.buffer_count < s_motion_ctx.config.window_size) {
        s_motion_ctx.buffer_count++;
    }
    
    // 初始化基线
    if (s_motion_ctx.baseline_amplitude == 0.0f && s_motion_ctx.buffer_count >= 10) {
        float sum = 0.0f;
        for (uint32_t i = 0; i < s_motion_ctx.buffer_count; i++) {
            sum += s_motion_ctx.amplitude_buffer[i];
        }
        s_motion_ctx.baseline_amplitude = sum / s_motion_ctx.buffer_count;
        ESP_LOGI(TAG, "基线幅度初始化: %.2f", s_motion_ctx.baseline_amplitude);
    }
    
    // 窗口填满后开始检测
    if (s_motion_ctx.buffer_count >= s_motion_ctx.config.window_size) {
        // 计算方差作为运动强度指标
        float variance = calculate_variance();
        s_motion_ctx.current_intensity = variance;
        
        // 更新运动状态
        update_motion_state(variance);
    }
    
    return ESP_OK;
}

motion_state_t motion_gate_get_state(void)
{
    return s_motion_ctx.current_state;
}

esp_err_t motion_gate_register_callback(motion_state_callback_t callback, void *user_ctx)
{
    s_motion_ctx.callback = callback;
    s_motion_ctx.callback_ctx = user_ctx;
    
    ESP_LOGI(TAG, "运动状态回调已注册");
    
    return ESP_OK;
}

float motion_gate_get_intensity(void)
{
    return s_motion_ctx.current_intensity;
}