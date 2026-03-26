/**
 * @file photo_scanner.h
 * @brief SD 卡图片文件扫描器
 *
 * 递归扫描指定目录，收集所有支持格式的图片路径。
 * 支持顺序播放和随机播放。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAY_ORDER_SEQUENTIAL = 0,  /* 顺序播放 */
    PLAY_ORDER_RANDOM     = 1,  /* 随机播放 */
    PLAY_ORDER_REVERSE    = 2,  /* 倒序播放 */
} play_order_t;

/**
 * @brief 初始化扫描器并扫描图片
 * @param root_path  扫描根目录
 * @return 找到的图片数量，负数表示错误
 */
int photo_scanner_init(const char *root_path);

/**
 * @brief 获取图片总数
 */
int photo_scanner_get_count(void);

/**
 * @brief 获取指定索引的图片路径
 * @param index  图片索引 (0 ~ count-1)
 * @return 图片路径字符串，NULL 表示索引越界
 */
const char *photo_scanner_get_path(int index);

/**
 * @brief 获取下一张图片路径
 * @param order  播放顺序
 * @return 图片路径
 */
const char *photo_scanner_next(play_order_t order);

/**
 * @brief 获取上一张图片路径
 */
const char *photo_scanner_prev(void);

/**
 * @brief 获取当前图片索引
 */
int photo_scanner_get_current_index(void);

/**
 * @brief 设置当前图片索引
 */
void photo_scanner_set_index(int index);

/**
 * @brief 重新扫描（SD 卡内容变化时调用）
 */
int photo_scanner_rescan(void);

/**
 * @brief 释放扫描器资源
 */
void photo_scanner_deinit(void);

#ifdef __cplusplus
}
#endif