#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "camera_manager.h"
#include "turn_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 显示模式枚举
 */
typedef enum {
    DISPLAY_MODE_SPLIT      = 0,  /*!< 分屏模式：左右各占一半 */
    DISPLAY_MODE_LEFT_FULL  = 1,  /*!< 左摄像头全屏 */
    DISPLAY_MODE_RIGHT_FULL = 2,  /*!< 右摄像头全屏 */
    DISPLAY_MODE_IDLE       = 3,  /*!< 待机模式：显示提示信息 */
} display_mode_t;

/**
 * @brief 盲区警告等级
 */
typedef enum {
    WARN_LEVEL_NONE    = 0,  /*!< 无警告 (安全) */
    WARN_LEVEL_CAUTION = 1,  /*!< 注意 (黄色) */
    WARN_LEVEL_DANGER  = 2,  /*!< 危险 (红色) */
} warn_level_t;

/**
 * @brief 盲区 UI 配置
 */
typedef struct {
    int screen_width;           /*!< 屏幕宽度 */
    int screen_height;          /*!< 屏幕高度 */
    int cam_frame_width;        /*!< 摄像头帧宽度 */
    int cam_frame_height;       /*!< 摄像头帧高度 */
    bool warning_enabled;       /*!< 是否启用警告叠加 */
    int warn_threshold_pct;     /*!< 警告阈值 (%) */
    int danger_threshold_pct;   /*!< 危险阈值 (%) */
} blind_spot_ui_config_t;

/**
 * @brief 初始化盲区 UI
 *
 * @param config UI 配置
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t blind_spot_ui_init(const blind_spot_ui_config_t *config);

/**
 * @brief 反初始化盲区 UI
 */
void blind_spot_ui_deinit(void);

/**
 * @brief 设置显示模式
 *
 * @param mode 显示模式
 */
void blind_spot_ui_set_mode(display_mode_t mode);

/**
 * @brief 获取当前显示模式
 */
display_mode_t blind_spot_ui_get_mode(void);

/**
 * @brief 根据转向灯状态自动切换显示模式
 *
 * @param turn_state 转向灯状态
 */
void blind_spot_ui_update_by_turn_signal(turn_signal_state_t turn_state);

/**
 * @brief 渲染分屏模式 (左右各半屏)
 *
 * @param left_frame  左摄像头帧 (可为NULL)
 * @param right_frame 右摄像头帧 (可为NULL)
 */
void blind_spot_ui_render_split(const camera_frame_t *left_frame,
                                 const camera_frame_t *right_frame);

/**
 * @brief 渲染全屏模式
 *
 * @param frame     摄像头帧
 * @param camera_id 摄像头ID (用于显示标签)
 */
void blind_spot_ui_render_fullscreen(const camera_frame_t *frame,
                                      camera_id_t camera_id);

/**
 * @brief 渲染待机界面
 */
void blind_spot_ui_render_idle(void);

/**
 * @brief 分析帧图像，估算盲区警告等级
 *
 * 使用简单的亮度/边缘检测算法估算画面中是否有物体接近。
 *
 * @param frame 摄像头帧
 * @return warn_level_t 警告等级
 */
warn_level_t blind_spot_ui_analyze_frame(const camera_frame_t *frame);

/**
 * @brief 在帧上叠加警告边框
 *
 * @param x          目标区域 X
 * @param y          目标区域 Y
 * @param w          目标区域宽度
 * @param h          目标区域高度
 * @param warn_level 警告等级
 */
void blind_spot_ui_draw_warning(int x, int y, int w, int h,
                                 warn_level_t warn_level);

/**
 * @brief 绘制状态栏 (FPS、转向灯状态等)
 *
 * @param fps_left   左摄像头帧率
 * @param fps_right  右摄像头帧率
 * @param turn_state 转向灯状态
 */
void blind_spot_ui_draw_statusbar(float fps_left, float fps_right,
                                   turn_signal_state_t turn_state);

#ifdef __cplusplus
}
#endif