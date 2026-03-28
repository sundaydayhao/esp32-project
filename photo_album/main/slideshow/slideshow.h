/**
 * @file slideshow.h
 * @brief 幻灯片管理器
 *
 * 功能：
 *  - 定时自动切换图片
 *  - 支持淡入淡出过渡动画
 *  - 支持触摸手势（左滑/右滑）切换
 *  - 支持顺序/随机/倒序播放
 *  - 配置持久化到 NVS
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sd/photo_scanner.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TRANSITION_NONE    = 0,  /* 直接切换 */
    TRANSITION_FADE    = 1,  /* 淡入淡出 */
    TRANSITION_SLIDE_L = 2,  /* 向左滑动 */
    TRANSITION_SLIDE_R = 3,  /* 向右滑动 */
} transition_type_t;

typedef struct {
    uint32_t         interval_ms;   /* 切换间隔 (ms) */
    play_order_t     order;         /* 播放顺序 */
    transition_type_t transition;   /* 过渡动画 */
    uint8_t          brightness;    /* 背光亮度 0~100 */
    bool             show_info;     /* 是否显示文件名/序号 */
} slideshow_config_t;

/**
 * @brief 初始化幻灯片管理器
 */
esp_err_t slideshow_init(void);

/**
 * @brief 启动幻灯片任务
 */
void slideshow_start(void);

/**
 * @brief 暂停/恢复幻灯片
 */
void slideshow_pause(bool pause);

/**
 * @brief 手动切换到下一张
 */
void slideshow_next(void);

/**
 * @brief 手动切换到上一张
 */
void slideshow_prev(void);

/**
 * @brief 更新配置
 */
esp_err_t slideshow_set_config(const slideshow_config_t *cfg);

/**
 * @brief 获取当前配置
 */
void slideshow_get_config(slideshow_config_t *cfg);

/**
 * @brief 幻灯片主任务（由 main 创建）
 */
void slideshow_task(void *arg);

#ifdef __cplusplus
}
#endif