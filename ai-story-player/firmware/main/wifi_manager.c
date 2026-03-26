/**
 * WiFi 管理模块
 * 处理 WiFi 连接、断线重连
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"

#include "config.h"
#include "wifi_manager.h"

static const char *TAG = "WIFI";

static wifi_connected_cb_t s_on_connected = NULL;
static wifi_disconnected_cb_t s_on_disconnected = NULL;
static bool s_is_connected = false;
static int s_retry_count = 0;
static esp_netif_t *s_netif = NULL;
static char s_ip_addr[16] = {0};

/**
 * WiFi 事件处理
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi 开始连接...");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *disconn =
                    (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "WiFi 断开，原因: %d", disconn->reason);
                s_is_connected = false;
                memset(s_ip_addr, 0, sizeof(s_ip_addr));

                if (s_on_disconnected) {
                    s_on_disconnected();
                }

                if (s_retry_count < WIFI_MAX_RETRY) {
                    s_retry_count++;
                    ESP_LOGI(TAG, "重连尝试 %d/%d...", s_retry_count, WIFI_MAX_RETRY);
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    esp_wifi_connect();
                } else {
                    ESP_LOGE(TAG, "WiFi 重连失败，已达最大重试次数");
                    s_retry_count = 0;
                    // 继续尝试
                    vTaskDelay(pdMS_TO_TICKS(10000));
                    esp_wifi_connect();
                }
                break;
            }

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi 已关联到 AP");
                s_retry_count = 0;
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR,
                     IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "获取 IP 地址: %s", s_ip_addr);
            s_is_connected = true;

            if (s_on_connected) {
                s_on_connected();
            }
        }
    }
}

void wifi_manager_init(wifi_connected_cb_t on_connected,
                       wifi_disconnected_cb_t on_disconnected) {
    s_on_connected = on_connected;
    s_on_disconnected = on_disconnected;

    // 初始化 TCP/IP 栈
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 创建默认 WiFi STA 接口
    s_netif = esp_netif_create_default_wifi_sta();

    // 初始化 WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理器
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_LOGI(TAG, "WiFi 管理器初始化完成");
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password) {
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password,
            sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "正在连接 WiFi: %s", ssid);
    return ESP_OK;
}

void wifi_manager_disconnect(void) {
    esp_wifi_disconnect();
    esp_wifi_stop();
}

bool wifi_manager_is_connected(void) {
    return s_is_connected;
}

void wifi_manager_get_ip(char *ip_buf, size_t buf_len) {
    strncpy(ip_buf, s_ip_addr, buf_len - 1);
    ip_buf[buf_len - 1] = '\0';
}