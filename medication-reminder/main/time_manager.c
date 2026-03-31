/**
 * @file time_manager.c
 * @brief 时间管理模块实现 - DS3231 RTC + NTP 时间同步
 */

#include "time_manager.h"
#include "ds3231.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "sdkconfig.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "TIME_MGR";

/* 星期中文名称 */
static const char *weekday_names[] = {
    "周日", "周一", "周二", "周三", "周四", "周五", "周六"
};

static time_source_t s_time_source = TIME_SOURCE_NONE;
static bool s_ntp_synced = false;

/* NTP 同步回调 */
static void ntp_sync_callback(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP 时间同步成功");
    s_ntp_synced = true;
    s_time_source = TIME_SOURCE_NTP;

    /* 将 NTP 时间写入 DS3231 */
    struct tm timeinfo;
    localtime_r(&tv->tv_sec, &timeinfo);
    esp_err_t ret = ds3231_set_time(&timeinfo);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NTP 时间已写入 DS3231 RTC");
    } else {
        ESP_LOGW(TAG, "写入 DS3231 失败: %s", esp_err_to_name(ret));
    }
}

esp_err_t time_manager_init(void)
{
    ESP_LOGI(TAG, "初始化时间管理模块");

    /* 初始化 DS3231 */
    esp_err_t ret = ds3231_init(CONFIG_I2C_SDA_PIN, CONFIG_I2C_SCL_PIN);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DS3231 初始化失败，将使用 NTP 时间");
        return ret;
    }

    /* 尝试从 DS3231 读取时间 */
    if (ds3231_is_running()) {
        struct tm rtc_time;
        ret = ds3231_get_time(&rtc_time);
        if (ret == ESP_OK) {
            /* 将 RTC 时间设置为系统时间 */
            struct timeval tv;
            tv.tv_sec = mktime(&rtc_time);
            tv.tv_usec = 0;
            settimeofday(&tv, NULL);
            s_time_source = TIME_SOURCE_RTC;
            ESP_LOGI(TAG, "从 DS3231 读取时间: %04d-%02d-%02d %02d:%02d:%02d",
                     rtc_time.tm_year + 1900, rtc_time.tm_mon + 1, rtc_time.tm_mday,
                     rtc_time.tm_hour, rtc_time.tm_min, rtc_time.tm_sec);
        }
    } else {
        ESP_LOGW(TAG, "DS3231 振荡器未运行，时间可能不准确");
    }

    /* 设置时区 */
    setenv("TZ", CONFIG_TIMEZONE, 1);
    tzset();

    return ESP_OK;
}

esp_err_t time_manager_sync_ntp(void)
{
    ESP_LOGI(TAG, "启动 NTP 时间同步...");

    /* 配置 SNTP */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_NTP_SERVER1);
    esp_sntp_setservername(1, CONFIG_NTP_SERVER2);
    sntp_set_time_sync_notification_cb(ntp_sync_callback);
    esp_sntp_init();

    /* 等待同步（最多 10 秒） */
    int retry = 0;
    const int max_retry = 20;
    while (!s_ntp_synced && retry < max_retry) {
        ESP_LOGI(TAG, "等待 NTP 同步... (%d/%d)", retry + 1, max_retry);
        vTaskDelay(pdMS_TO_TICKS(500));
        retry++;
    }

    if (s_ntp_synced) {
        ESP_LOGI(TAG, "NTP 同步成功");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "NTP 同步超时，使用 RTC 时间");
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t time_manager_get_time(struct tm *tm)
{
    time_t now = time(NULL);
    localtime_r(&now, tm);
    return ESP_OK;
}

time_t time_manager_get_timestamp(void)
{
    return time(NULL);
}

time_source_t time_manager_get_source(void)
{
    return s_time_source;
}

bool time_manager_is_synced(void)
{
    return s_time_source != TIME_SOURCE_NONE;
}

void time_manager_get_time_str(char *buf, size_t len)
{
    struct tm timeinfo;
    time_manager_get_time(&timeinfo);
    snprintf(buf, len, "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

void time_manager_get_date_str(char *buf, size_t len)
{
    struct tm timeinfo;
    time_manager_get_time(&timeinfo);
    snprintf(buf, len, "%04d-%02d-%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
}

void time_manager_get_weekday_str(char *buf, size_t len)
{
    struct tm timeinfo;
    time_manager_get_time(&timeinfo);
    int wday = timeinfo.tm_wday;
    if (wday < 0 || wday > 6) wday = 0;
    strncpy(buf, weekday_names[wday], len - 1);
    buf[len - 1] = '\0';
}

void time_manager_get_datetime_str(char *buf, size_t len)
{
    struct tm timeinfo;
    time_manager_get_time(&timeinfo);
    snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}