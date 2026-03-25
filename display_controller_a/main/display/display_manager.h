#pragma once

#include "content_store.h"
#include <stdbool.h>

/**
 * @brief 初始化显示管理器（初始化 LCD，创建显示任务）
 */
void display_manager_init(void);

/**
 * @brief 更新显示内容（线程安全，可从任意任务调用）
 * @param content 新的菜单内容
 */
void display_manager_update(const menu_content_t *content);

/**
 * @brief 显示 WiFi 配置引导页（AP 模式时显示）
 * @param ap_ssid 热点名称
 * @param ap_ip   热点 IP
 */
void display_manager_show_wifi_setup(const char *ap_ssid, const char *ap_ip);

/**
 * @brief 显示 WiFi 连接中动画
 * @param ssid 正在连接的 SSID
 */
void display_manager_show_connecting(const char *ssid);

/**
 * @brief 显示启动画面
 */
void display_manager_show_splash(void);