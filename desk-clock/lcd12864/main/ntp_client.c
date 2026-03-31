/**
 * @file ntp_client.c
 * @brief NTP 时间同步客户端实现
 */
#include "ntp_client.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <time.h>
#include <string.h>

static const char *TAG = "ntp_client";

/* NTP 重新同步间隔（分钟） */
#ifndef CONFIG_NTP_SYNC_INTERVAL_MIN
#define CONFIG_NTP_SYNC_INTERVAL_MIN    60
#endif

#define NTP_SYNC_BIT    BIT0

static EventGroupHandle_t s_ntp_evt_grp = NULL;
static volatile bool      s_synced      = false;

/* ----------------------------------------------------------------
 * 回调：SNTP 同步完成
 * ---------------------------------------------------------------- */
static void time_sync_cb(struct timeval *tv)
{
    (void)tv;
    ESP_LOGI(TAG, "NTP time synchronized");
    s_synced = true;
    if (s_ntp_evt_grp) {
        xEventGroupSetBits(s_ntp_evt_grp, NTP_SYNC_BIT);
    }
}

/* ----------------------------------------------------------------
 * 公共 API
 * ---------------------------------------------------------------- */
void ntp_client_init(void)
{
    if (s_ntp_evt_grp == NULL) {
        s_ntp_evt_grp = xEventGroupCreate();
    }

    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "cn.pool.ntp.org");
    esp_sntp_setservername(2, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();

    /* 设置时区为北京时间 CST+8 */
    setenv("TZ", "CST-8", 1);
    tzset();
}

bool ntp_client_wait_sync(uint32_t timeout_ms)
{
    if (s_synced) return true;
    if (!s_ntp_evt_grp) return false;

    EventBits_t bits = xEventGroupWaitBits(
        s_ntp_evt_grp,
        NTP_SYNC_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    return (bits & NTP_SYNC_BIT) != 0;
}

bool ntp_client_is_synced(void)
{
    return s_synced;
}

void ntp_client_get_time(struct tm *tm_out)
{
    time_t now = 0;
    time(&now);
    localtime_r(&now, tm_out);
}

/* ----------------------------------------------------------------
 * NTP 周期同步任务
 * ---------------------------------------------------------------- */
void ntp_task(void *arg)
{
    (void)arg;

    /* 首次同步：等待最多 30 秒 */
    ESP_LOGI(TAG, "Waiting for first NTP sync...");
    bool ok = ntp_client_wait_sync(30000);
    if (ok) {
        struct tm t;
        ntp_client_get_time(&t);
        ESP_LOGI(TAG, "Time synced: %04d-%02d-%02d %02d:%02d:%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        ESP_LOGW(TAG, "NTP sync timeout, will retry later");
    }

    /* 周期重新同步 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(
            (uint32_t)CONFIG_NTP_SYNC_INTERVAL_MIN * 60 * 1000));

        ESP_LOGI(TAG, "Periodic NTP re-sync");
        /* 重置同步标志，触发重新同步 */
        s_synced = false;
        if (s_ntp_evt_grp) {
            xEventGroupClearBits(s_ntp_evt_grp, NTP_SYNC_BIT);
        }
        esp_sntp_restart();

        /* 等待同步完成 */
        ok = ntp_client_wait_sync(30000);
        if (ok) {
            struct tm t;
            ntp_client_get_time(&t);
            ESP_LOGI(TAG, "Re-synced: %04d-%02d-%02d %02d:%02d:%02d",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                     t.tm_hour, t.tm_min, t.tm_sec);
        } else {
            ESP_LOGW(TAG, "NTP re-sync timeout");
        }
    }
}