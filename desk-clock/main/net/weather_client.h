/**
 * @file weather_client.h
 * @brief 天气数据客户端（和风天气 QWeather API）
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 天气状况描述 */
typedef struct {
    char    icon_code[8];       /* 天气图标代码 */
    int8_t  temp_now;           /* 当前温度 (°C) */
    int8_t  temp_feels;         /* 体感温度 (°C) */
    int8_t  temp_high;          /* 今日最高温 */
    int8_t  temp_low;           /* 今日最低温 */
    uint8_t humidity;           /* 相对湿度 (%) */
    uint16_t wind_speed;        /* 风速 (km/h) */
    uint16_t visibility;        /* 能见度 (km) */

    /* 未来 3 天预报 */
    struct {
        char    date[12];
        int8_t  high;
        int8_t  low;
        char    icon_day[8];
    } forecast[3];

    int64_t update_time;        /* 最后更新时间戳 */
    bool    valid;              /* 数据是否有效 */
} weather_data_t;

/**
 * @brief 初始化天气客户端
 */
esp_err_t weather_client_init(void);

/**
 * @brief 立即获取天气数据（阻塞）
 */
esp_err_t weather_client_fetch(void);

/**
 * @brief 获取最新天气数据（非阻塞，返回缓存）
 */
void weather_client_get_data(weather_data_t *data);

/**
 * @brief 天气更新任务（由 main 创建）
 */
void weather_task(void *arg);

#ifdef __cplusplus
}
#endif
