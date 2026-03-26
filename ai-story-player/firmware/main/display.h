#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * 初始化 TFT 显示屏 (ST7789 240x240)
 */
esp_err_t display_init(void);

/**
 * 显示开机画面
 */
void display_show_boot_screen(void);

/**
 * 显示欢迎界面（空闲状态）
 */
void display_show_welcome(void);

/**
 * 显示正在聆听界面
 */
void display_show_listening(void);

/**
 * 显示正在处理界面
 */
void display_show_processing(void);

/**
 * 显示状态信息
 * @param title     标题文字
 * @param subtitle  副标题文字
 * @param color     文字颜色 (RGB565)
 */
void display_show_status(const char *title, const char *subtitle, uint16_t color);

/**
 * 显示错误信息
 */
void display_show_error(const char *title, const char *message);

/**
 * 显示睡眠界面
 */
void display_show_sleeping(void);

/**
 * 显示故事插图（RGB565 格式）
 * @param rgb565_data   RGB565 图像数据
 * @param data_len      数据长度（字节）
 * @param width         图像宽度
 * @param height        图像高度
 */
void display_show_image(const uint8_t *rgb565_data, size_t data_len,
                         uint16_t width, uint16_t height);

/**
 * 显示故事文字（滚动显示）
 * @param text  故事文字
 */
void display_show_story_text(const char *text);

/**
 * 追加故事文字
 */
void display_append_story_text(const char *text);

/**
 * 清屏
 * @param color 填充颜色 (RGB565)
 */
void display_clear(uint16_t color);

/**
 * 在指定位置绘制文字
 */
void display_draw_text(uint16_t x, uint16_t y, const char *text,
                        uint16_t color, uint8_t size);

/**
 * 绘制进度条
 */
void display_draw_progress(uint16_t x, uint16_t y, uint16_t width,
                            uint16_t height, uint8_t percent, uint16_t color);

/**
 * 显示关键词标签
 */
void display_show_keywords(const char **keywords, int count);

/**
 * 反初始化
 */
void display_deinit(void);