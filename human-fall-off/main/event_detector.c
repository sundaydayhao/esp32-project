/*
 * 事件检测器模块实现
 * 基于运动状态变化和持续时间判断跌倒事件
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "event_detector.h"
#include "data_uplink.h"

static const char *TAG = "EVENT_DETECT";

/* 事件检测器状态 */
static struct {
    bool initialized;
    event_detector_config_t config;
    event_state_t current_state;
    fall_event_callback_t callback;
    void *callback_ctx;
    
    // 事件跟踪
    fall_event_t last_event;
    motion_state_t last_motion_state;
    uint64_t suspect_start_time;
    uint64_t active_to_idle_time;
    
    // 状态机
    bool in_suspect_phase;
    bool in_confirm_phase;
    
    // 统计
    uint32_t total_suspects;
    uint32_t total_confirmed;
    uint32_t false_alarms;
} s_event_ctx = {0};

/* 确认定时器 */
static TimerHandle_t s_confirm_timer = NULL;

/**
 * @brief 触发跌倒事件
 */
static void trigger_fall_event(event_state_t state, float confidence)
{
    fall_event_t event = {
        .timestamp_us = esp_timer_get_time(),
        .state = state,
        .confidence = confidence,
        .motion_before = s_event_ctx.last_motion_state,
        .motion_after = motion_gate_get_state(),
    };
    
    // 保存事件
    memcpy(&s_event_ctx.last_event, &event, sizeof(fall_event_t));
    
    // 更新统计
    if (state == EVENT_STATE_SUSPECT_FALL) {
        s_event_ctx.total_suspects++;
        ESP_LOGW(TAG, "⚠️  检测到疑似跌倒事件 (置信度: %.2f)", confidence);
    } else if (state == EVENT_STATE_CONFIRMED_FALL) {
        s_event_ctx.total_confirmed++;
        ESP_LOGE(TAG, "🚨 确认跌倒事件! (置信度: %.2f)", confidence);
    }
    
    // 触发回调
    if (s_event_ctx.callback != NULL) {
        s_event_ctx.callback(&event, s_event_ctx.callback_ctx);
    }
    
    // 上报事件
    data_uplink_send_event(&event);
}

/**
 * @brief 确认定时器回调
 */
static void confirm_timer_callback(TimerHandle_t xTimer)
{
    // 检查是否仍处于空闲状态
    motion_state_t current_motion = motion_gate_get_state();
    
    if (current_motion == MOTION_STATE_IDLE) {
        // 持续空闲，确认跌倒
        s_event_ctx.current_state = EVENT_STATE_CONFIRMED_FALL;
        s_event_ctx.in_confirm_phase = false;
        
        trigger_fall_event(EVENT_STATE_CONFIRMED_FALL, 0.9f);
    } else {
        // 恢复活动，可能是误报
        ESP_LOGI(TAG, "疑似跌倒事件已恢复活动，判定为误报");
        s_event_ctx.false_alarms++;
        s_event_ctx.current_state = EVENT_STATE_NORMAL;
        s_event_ctx.in_suspect_phase = false;
        s_event_ctx.in_confirm_phase = false;
    }
}

/**
 * @brief 分析运动状态转换
 */
static void analyze_motion_transition(motion_state_t old_state, motion_state_t new_state)
{
    uint64_t now = esp_timer_get_time();
    
    // 从活动到空闲的转换 - 可能是跌倒
    if (old_state == MOTION_STATE_ACTIVE && new_state == MOTION_STATE_IDLE) {
        s_event_ctx.active_to_idle_time = now;
        
        // 获取运动强度
        float intensity = motion_gate_get_intensity();
        
        // 判断是否为剧烈变化
        if (intensity > s_event_ctx.config.fall_threshold * 1000.0f) {
            // 进入疑似跌倒状态
            s_event_ctx.current_state = EVENT_STATE_SUSPECT_FALL;
            s_event_ctx.in_suspect_phase = true;
            s_event_ctx.suspect_start_time = now;
            
            // 计算初始置信度
            float confidence = 0.6f;  // 基础置信度
            
            trigger_fall_event(EVENT_STATE_SUSPECT_FALL, confidence);
            
            // 启动确认定时器
            s_event_ctx.in_confirm_phase = true;
            xTimerStart(s_confirm_timer, 0);
            
            ESP_LOGW(TAG, "检测到剧烈运动后突然静止，启动跌倒确认流程");
        } else {
            ESP_LOGI(TAG, "正常坐下/躺下动作 (强度: %.2f)", intensity);
        }
    }
    // 从空闲恢复到活动 - 可能是误报或已恢复
    else if (old_state == MOTION_STATE_IDLE && new_state == MOTION_STATE_ACTIVE) {
        if (s_event_ctx.in_suspect_phase || s_event_ctx.in_confirm_phase) {
            uint64_t idle_duration = now - s_event_ctx.active_to_idle_time;
            
            if (idle_duration < s_event_ctx.config.confirm_duration_ms * 1000) {
                // 快速恢复，可能是误报
                ESP_LOGI(TAG, "快速恢复活动 (空闲时长: %llu ms)，判定为误报", 
                         idle_duration / 1000);
                s_event_ctx.false_alarms++;
            } else {
                ESP_LOGI(TAG, "长时间空闲后恢复活动 (空闲时长: %llu ms)", 
                         idle_duration / 1000);
            }
            
            // 停止确认定时器
            xTimerStop(s_confirm_timer, 0);
            
            // 重置状态
            s_event_ctx.current_state = EVENT_STATE_NORMAL;
            s_event_ctx.in_suspect_phase = false;
            s_event_ctx.in_confirm_phase = false;
        }
    }
}

esp_err_t event_detector_init(const event_detector_config_t *config)
{
    if (s_event_ctx.initialized) {
        ESP_LOGW(TAG, "事件检测器已初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (config == NULL) {
        ESP_LOGE(TAG, "配置参数为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 保存配置
    memcpy(&s_event_ctx.config, config, sizeof(event_detector_config_t));
    
    // 初始化状态
    s_event_ctx.current_state = EVENT_STATE_NORMAL;
    s_event_ctx.last_motion_state = MOTION_STATE_UNKNOWN;
    s_event_ctx.in_suspect_phase = false;
    s_event_ctx.in_confirm_phase = false;
    s_event_ctx.total_suspects = 0;
    s_event_ctx.total_confirmed = 0;
    s_event_ctx.false_alarms = 0;
    
    // 创建确认定时器
    s_confirm_timer = xTimerCreate(
        "confirm_timer",
        pdMS_TO_TICKS(config->confirm_duration_ms),
        pdFALSE,  // 单次触发
        NULL,
        confirm_timer_callback
    );
    
    if (s_confirm_timer == NULL) {
        ESP_LOGE(TAG, "创建确认定时器失败");
        return ESP_ERR_NO_MEM;
    }
    
    s_event_ctx.initialized = true;
    
    ESP_LOGI(TAG, "事件检测器初始化成功 - 阈值: %.2f, 确认时长: %lu ms",
             config->fall_threshold, config->confirm_duration_ms);
    
    return ESP_OK;
}

esp_err_t event_detector_on_motion_change(motion_state_t state)
{
    if (!s_event_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 分析状态转换
    if (state != s_event_ctx.last_motion_state) {
        analyze_motion_transition(s_event_ctx.last_motion_state, state);
        s_event_ctx.last_motion_state = state;
    }
    
    return ESP_OK;
}

event_state_t event_detector_get_state(void)
{
    return s_event_ctx.current_state;
}

esp_err_t event_detector_register_callback(fall_event_callback_t callback, void *user_ctx)
{
    s_event_ctx.callback = callback;
    s_event_ctx.callback_ctx = user_ctx;
    
    ESP_LOGI(TAG, "跌倒事件回调已注册");
    
    return ESP_OK;
}

esp_err_t event_detector_get_last_event(fall_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(event, &s_event_ctx.last_event, sizeof(fall_event_t));
    
    return ESP_OK;
}

esp_err_t event_detector_reset(void)
{
    if (!s_event_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 停止定时器
    if (s_confirm_timer != NULL) {
        xTimerStop(s_confirm_timer, 0);
    }
    
    // 重置状态
    s_event_ctx.current_state = EVENT_STATE_NORMAL;
    s_event_ctx.in_suspect_phase = false;
    s_event_ctx.in_confirm_phase = false;
    
    ESP_LOGI(TAG, "事件检测器已重置");
    
    return ESP_OK;
}