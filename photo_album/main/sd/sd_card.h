/**
 * @file sd_card.h
 * @brief SD 卡驱动（SPI 模式，FAT 文件系统）
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 SD 卡并挂载 FAT 文件系统
 */
esp_err_t sd_card_init(void);

/**
 * @brief 卸载 SD 卡
 */
void sd_card_deinit(void);

/**
 * @brief 检查 SD 卡是否已挂载
 */
bool sd_card_is_mounted(void);

/**
 * @brief 获取 SD 卡容量信息
 * @param total_mb  总容量 (MB)
 * @param free_mb   剩余容量 (MB)
 */
esp_err_t sd_card_get_info(uint64_t *total_mb, uint64_t *free_mb);

#ifdef __cplusplus
}
#endif