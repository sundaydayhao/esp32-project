/**
 * @file ntp_client.c
 * @brief NTP 时间同步实现
 */
#include "ntp_client.h"

#include <stdlib.h>
#include <time.h>

#include "app_events.h"
#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/wifi_manager.h"

#define NTP_VALID_UNIX_TIME 1704067200LL
#define NTP_NOTIFY_WIFI_CONNECTED BIT0
#define NTP_NOTIFY_WIFI_DISCONNECTED BIT1
#define NTP_STATUS_POLL_MS 1000

static volatile bool s_synced = false;
static bool s_client_started = false;
static TaskHandle_t s_ntp_task_handle = NULL;

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

static void post_ntp_state_event(app_ntp_state_t state)
{
    app_ntp_event_t event = {
        .state = state,
        .synced = ntp_client_is_synced() || ntp_client_has_valid_time(),
        .now = time(NULL),
    };

    esp_err_t ret = app_events_post(APP_EVENT_NTP_STATE_CHANGED, &event, sizeof(event));
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG_NTP, "Post NTP state event failed: %s", esp_err_to_name(ret));
    }
}

static void ntp_app_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)arg;

    if (base != APP_EVENTS || id != APP_EVENT_WIFI_STATE_CHANGED || !s_ntp_task_handle) {
        return;
    }

    const app_wifi_event_t *event = (const app_wifi_event_t *)event_data;
    uint32_t bits = (event && event->connected) ? NTP_NOTIFY_WIFI_CONNECTED : NTP_NOTIFY_WIFI_DISCONNECTED;
    xTaskNotify(s_ntp_task_handle, bits, eSetBits);
}

esp_err_t ntp_client_init(void)
{
    setenv("TZ", NTP_TIMEZONE, 1);
    tzset();

    if (!s_client_started) {
        s_synced = false;
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, NTP_SERVER_1);
        esp_sntp_setservername(1, NTP_SERVER_2);
        esp_sntp_setservername(2, NTP_SERVER_3);
        sntp_set_time_sync_notification_cb(sntp_sync_cb);
        esp_sntp_init();
        s_client_started = true;
        ESP_LOGI(TAG_NTP, "NTP 客户端已启动 (服务器: %s)", NTP_SERVER_1);
    }

    refresh_sync_state();
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

bool ntp_client_has_valid_time(void)
{
    return ntp_time_is_valid();
}

time_t ntp_client_get_time(void) { return time(NULL); }

void ntp_client_get_local_time(struct tm *tm_info)
{
    time_t now = time(NULL);
    localtime_r(&now, tm_info);
}

void ntp_task(void *arg)
{
    bool wifi_ready = false;
    app_ntp_state_t last_state = 0;
    TickType_t last_restart_tick = 0;

    (void)arg;

    ESP_LOGI(TAG_NTP, "NTP 任务启动");
    s_ntp_task_handle = xTaskGetCurrentTaskHandle();
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_events_loop(), APP_EVENTS,
                                                    APP_EVENT_WIFI_STATE_CHANGED,
                                                    ntp_app_event_handler, NULL));

    if (wifi_manager_is_connected()) {
        xTaskNotify(s_ntp_task_handle, NTP_NOTIFY_WIFI_CONNECTED, eSetBits);
    } else {
        post_ntp_state_event(APP_NTP_STATE_WAITING_FOR_NETWORK);
        last_state = APP_NTP_STATE_WAITING_FOR_NETWORK;
    }

    while (1) {
        uint32_t notify_bits = 0;
        TickType_t wait_ticks = wifi_ready ? pdMS_TO_TICKS(NTP_STATUS_POLL_MS) : portMAX_DELAY;
        if (xTaskNotifyWait(0, 0xFFFFFFFFu, &notify_bits, wait_ticks) == pdTRUE) {
            if (notify_bits & NTP_NOTIFY_WIFI_CONNECTED) {
                wifi_ready = true;
                ESP_ERROR_CHECK(ntp_client_init());
                esp_sntp_restart();
                last_restart_tick = xTaskGetTickCount();
                if (last_state != APP_NTP_STATE_SYNCING) {
                    post_ntp_state_event(APP_NTP_STATE_SYNCING);
                    last_state = APP_NTP_STATE_SYNCING;
                }
            }

            if (notify_bits & NTP_NOTIFY_WIFI_DISCONNECTED) {
                wifi_ready = false;
                if (last_state != APP_NTP_STATE_WAITING_FOR_NETWORK) {
                    post_ntp_state_event(APP_NTP_STATE_WAITING_FOR_NETWORK);
                    last_state = APP_NTP_STATE_WAITING_FOR_NETWORK;
                }
            }
        }

        if (!wifi_ready || !s_client_started) {
            continue;
        }

        refresh_sync_state();
        if (s_synced) {
            if (last_state != APP_NTP_STATE_SYNCED) {
                post_ntp_state_event(APP_NTP_STATE_SYNCED);
                last_state = APP_NTP_STATE_SYNCED;
            }
        } else if (last_state != APP_NTP_STATE_SYNCING) {
            post_ntp_state_event(APP_NTP_STATE_SYNCING);
            last_state = APP_NTP_STATE_SYNCING;
        }

        TickType_t now_tick = xTaskGetTickCount();
        uint32_t interval_s = s_synced ? NTP_SYNC_INTERVAL_S : NTP_RETRY_INTERVAL_S;
        if (last_restart_tick == 0 ||
            (now_tick - last_restart_tick) >= pdMS_TO_TICKS(interval_s * 1000U)) {
            esp_sntp_restart();
            ESP_LOGI(TAG_NTP, "触发 NTP %s同步",
                     s_synced ? "定期重新" : "重试");
            last_restart_tick = now_tick;
        }
    }
}
