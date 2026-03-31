/**
 * @file weather_client.h
 * @brief 天气数据客户端（和风天气 QWeather API v7）
 *
 * 使用免费版 API：https://devapi.qweather.com/v7/weather/now
 * 需要在 Kconfig 中配置 API Key 和城市 Location ID
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 天气数据结构 */
typedef struct {
    char    text[32];       /* 天气状况描述，如"晴"、"多云"等 */
    int8_t  temp;           /* 当前温度 (°C) */
    int8_t  feels_like;     /* 体感温度 (°C) */
    uint8_t humidity;       /* 相对湿度 (%) */
    uint16_t wind_speed;    /* 风速 (km/h) */
    char    wind_dir[16];   /* 风向，如"东北风" */
    char    update_time[20];/* 数据更新时间 */
    bool    valid;          /* 数据是否有效 */
} weather_data_t;

/**
 * @brief 初始化天气客户端（分配缓冲区等）
 */
esp_err_t weather_client_init(void);

/**
 * @brief 立即拉取一次天气数据（阻塞，需要 WiFi 已连接）
 * @return ESP_OK 成功
 */
esp_err_t weather_client_fetch(void);

/**
 * @brief 获取最新缓存的天气数据（非阻塞）
 * @param out  输出缓冲区
 */
void weather_client_get_data(weather_data_t *out);

/**
 * @brief 天气周期更新任务（由 main 创建）
 *        每隔 CONFIG_WEATHER_UPDATE_INTERVAL_MIN 分钟更新一次
 */
void weather_task(void *arg);

#ifdef __cplusplus
}
#endif