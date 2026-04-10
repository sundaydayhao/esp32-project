/**
 * @file ntp_client.c
 * @brief NTP 时间同步实现
 */
#include "ntp_client.h"
#include "config.h"
#include "net/wifi_manager.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>

#define NTP_VALID_UNIX_TIME 1704067200LL

static volatile bool s_synced = false;

static bool ntp_time_is_valid(void)
{
    return time(NULL) >= NTP_VALID_UNIX_TIME;
}

static void refresh_sync_state(void)
{
    if (s_synced) {
        return;
    }

    if (ntp_time_is_valid()) {
        s_synced = true;
        return;
    }

    sntp_sync_status_t status = esp_sntp_get_sync_status();
    if (status == SNTP_SYNC_STATUS_COMPLETED) {
        s_synced = true;
    }
}

static void sntp_sync_cb(struct timeval *tv)
{
    s_synced = true;
    char buf[32];
    struct tm t;
    localtime_r(&tv->tv_sec, &t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    ESP_LOGI(TAG_NTP, "时间同步成功: %s (CST)", buf);
}

esp_err_t ntp_client_init(void)
{
    s_synced = false;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER_1);
    esp_sntp_setservername(1, NTP_SERVER_2);
    esp_sntp_setservername(2, NTP_SERVER_3);
    sntp_set_time_sync_notification_cb(sntp_sync_cb);
    esp_sntp_init();

    setenv("TZ", NTP_TIMEZONE, 1);
    tzset();
    refresh_sync_state();

    ESP_LOGI(TAG_NTP, "NTP 客户端已启动 (服务器: %s)", NTP_SERVER_1);
    if (s_synced) {
        ESP_LOGI(TAG_NTP, "检测到系统时间已经有效，跳过等待首次同步状态");
    }
    return ESP_OK;
}

bool ntp_client_is_synced(void)
{
    refresh_sync_state();
    return s_synced;
}

time_t ntp_client_get_time(void) { return time(NULL); }

void ntp_client_get_local_time(struct tm *tm_info)
{
    time_t now = time(NULL);
    localtime_r(&now, tm_info);
}

void ntp_task(void *arg)
{
    ESP_LOGI(TAG_NTP, "NTP 任务启动");

    /* 等待 WiFi 连接 */
    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ntp_client_init();

    /* 等待首次同步（最多 30s） */
    int wait = 0;
    ESP_LOGI(TAG_NTP, "等待首次 NTP 同步...");
    while (!s_synced && wait < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        refresh_sync_state();
        wait++;
    }

    if (!s_synced) {
        ESP_LOGW(TAG_NTP, "NTP 首次同步超时，进入短周期重试");
    }

    /* 未同步前短周期重试；已同步后按常规间隔刷新 */
    while (1) {
        uint32_t interval_s = s_synced ? NTP_SYNC_INTERVAL_S : NTP_RETRY_INTERVAL_S;

        vTaskDelay(pdMS_TO_TICKS(interval_s * 1000U));
        if (wifi_manager_is_connected()) {
            esp_sntp_restart();
            ESP_LOGI(TAG_NTP, "触发 NTP %s同步",
                     s_synced ? "定期重新" : "重试");
        }
        refresh_sync_state();
    }
}
