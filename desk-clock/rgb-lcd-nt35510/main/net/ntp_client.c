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

static volatile bool s_synced = false;

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
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER_1);
    esp_sntp_setservername(1, NTP_SERVER_2);
    esp_sntp_setservername(2, NTP_SERVER_3);
    sntp_set_time_sync_notification_cb(sntp_sync_cb);
    esp_sntp_init();

    setenv("TZ", NTP_TIMEZONE, 1);
    tzset();

    ESP_LOGI(TAG_NTP, "NTP 客户端已启动 (服务器: %s)", NTP_SERVER_1);
    return ESP_OK;
}

bool ntp_client_is_synced(void) { return s_synced; }

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
    while (!s_synced && wait < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        wait++;
    }

    if (!s_synced) {
        ESP_LOGW(TAG_NTP, "NTP 首次同步超时，将继续重试");
    }

    /* 定期重新同步 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)NTP_SYNC_INTERVAL_S * 1000));
        if (wifi_manager_is_connected()) {
            esp_sntp_restart();
            ESP_LOGI(TAG_NTP, "触发 NTP 重新同步");
        }
    }
}