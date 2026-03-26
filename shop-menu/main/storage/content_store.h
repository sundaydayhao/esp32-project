#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "config.h"

// ============================================================
// 数据结构
// ============================================================

typedef struct {
    char name[MAX_ITEM_NAME_LEN];
    char price[MAX_ITEM_PRICE_LEN];
} menu_item_t;

typedef struct {
    char restaurant_name[MAX_RESTAURANT_NAME_LEN];
    uint8_t theme;                          // 0~3
    char promo_text[MAX_PROMO_TEXT_LEN];
    uint8_t item_count;
    menu_item_t items[MAX_MENU_ITEMS];
} menu_content_t;

typedef struct {
    char ssid[MAX_WIFI_SSID_LEN];
    char password[MAX_WIFI_PASS_LEN];
    bool configured;
} wifi_config_store_t;

// ============================================================
// 函数声明
// ============================================================

/**
 * @brief 初始化 NVS 存储
 * @return true 成功
 */
bool content_store_init(void);

/**
 * @brief 加载菜单内容（失败时填充默认值）
 */
void content_store_load(menu_content_t *content);

/**
 * @brief 保存菜单内容到 NVS
 * @return true 成功
 */
bool content_store_save(const menu_content_t *content);

/**
 * @brief 加载 WiFi 配置
 */
void content_store_load_wifi(wifi_config_store_t *wifi_cfg);

/**
 * @brief 保存 WiFi 配置
 * @return true 成功
 */
bool content_store_save_wifi(const wifi_config_store_t *wifi_cfg);

/**
 * @brief 清除所有存储（恢复出厂）
 */
void content_store_erase_all(void);

/**
 * @brief 将 menu_content_t 序列化为 JSON 字符串
 * @param content 内容结构体
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 写入的字节数，-1 表示失败
 */
int content_to_json(const menu_content_t *content, char *buf, size_t buf_size);

/**
 * @brief 从 JSON 字符串解析 menu_content_t
 * @return true 成功
 */
bool content_from_json(const char *json, menu_content_t *content);
