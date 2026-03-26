/**
 * @file websocket_client.cpp
 * @brief WebSocket客户端，用于与后端AI服务器通信
 */

#include "websocket_client.h"
#include "app_config.h"

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "WS";

static esp_websocket_client_handle_t s_ws_client = NULL;
static ws_event_cb_t s_event_cb = NULL;
static ws_status_t s_ws_status = WS_STATUS_DISCONNECTED;
static SemaphoreHandle_t s_send_mutex = NULL;

/**
 * @brief WebSocket事件处理函数
 */
static void ws_event_handler(void *handler_args, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket已连接");
        s_ws_status = WS_STATUS_CONNECTED;
        if (s_event_cb) {
            s_event_cb(WS_STATUS_CONNECTED, NULL);
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket已断开");
        s_ws_status = WS_STATUS_DISCONNECTED;
        if (s_event_cb) {
            s_event_cb(WS_STATUS_DISCONNECTED, NULL);
        }
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->data_len > 0 && s_event_cb) {
            ws_message_t msg = {
                .type = (data->op_code == 0x02) ? WS_MSG_BINARY : WS_MSG_TEXT,
                .data = (uint8_t *)data->data_ptr,
                .size = (size_t)data->data_len,
            };

#if DEBUG_WEBSOCKET
            if (msg.type == WS_MSG_TEXT) {
                ESP_LOGD(TAG, "收到文本: %.*s", (int)msg.size, (char *)msg.data);
            } else {
                ESP_LOGD(TAG, "收到二进制数据: %d 字节", (int)msg.size);
            }
#endif

            s_event_cb(WS_STATUS_CONNECTED, &msg);
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket错误");
        s_ws_status = WS_STATUS_ERROR;
        if (s_event_cb) {
            s_event_cb(WS_STATUS_ERROR, NULL);
        }
        break;

    default:
        break;
    }
}

// ============================================================
// 公共接口实现
// ============================================================

esp_err_t ws_client_init(const char *uri, ws_event_cb_t event_cb)
{
    if (!uri) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "初始化WebSocket客户端: %s", uri);

    s_event_cb = event_cb;

    // 创建发送互斥锁
    s_send_mutex = xSemaphoreCreateMutex();
    if (!s_send_mutex) {
        ESP_LOGE(TAG, "创建互斥锁失败");
        return ESP_ERR_NO_MEM;
    }

    // 配置WebSocket客户端
    esp_websocket_client_config_t ws_cfg = {
        .uri = uri,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = SERVER_WS_TIMEOUT_MS,
        .buffer_size = 8192,
        .task_stack = TASK_STACK_WEBSOCKET,
        .task_prio = TASK_PRIORITY_WEBSOCKET,
    };

    s_ws_client = esp_websocket_client_init(&ws_cfg);
    if (!s_ws_client) {
        ESP_LOGE(TAG, "创建WebSocket客户端失败");
        return ESP_FAIL;
    }

    // 注册事件处理函数
    esp_err_t ret = esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY,
                                                   ws_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册WebSocket事件失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "WebSocket客户端初始化完成");
    return ESP_OK;
}

esp_err_t ws_client_connect(void)
{
    if (!s_ws_client) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "连接WebSocket服务器...");
    s_ws_status = WS_STATUS_CONNECTING;

    esp_err_t ret = esp_websocket_client_start(s_ws_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动WebSocket客户端失败: %s", esp_err_to_name(ret));
        s_ws_status = WS_STATUS_ERROR;
        return ret;
    }

    return ESP_OK;
}

void ws_client_disconnect(void)
{
    if (s_ws_client) {
        esp_websocket_client_stop(s_ws_client);
        s_ws_status = WS_STATUS_DISCONNECTED;
        ESP_LOGI(TAG, "WebSocket已断开");
    }
}

esp_err_t ws_client_send_text(const char *text)
{
    if (!s_ws_client || !text) return ESP_ERR_INVALID_ARG;
    if (s_ws_status != WS_STATUS_CONNECTED) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_send_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

#if DEBUG_WEBSOCKET
    ESP_LOGD(TAG, "发送文本: %s", text);
#endif

    int ret = esp_websocket_client_send_text(s_ws_client, text, strlen(text), pdMS_TO_TICKS(1000));
    xSemaphoreGive(s_send_mutex);

    if (ret < 0) {
        ESP_LOGE(TAG, "发送文本失败");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ws_client_send_binary(const uint8_t *data, size_t size)
{
    if (!s_ws_client || !data || size == 0) return ESP_ERR_INVALID_ARG;
    if (s_ws_status != WS_STATUS_CONNECTED) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_send_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    int ret = esp_websocket_client_send_bin(s_ws_client, (const char *)data, size, pdMS_TO_TICKS(500));
    xSemaphoreGive(s_send_mutex);

    if (ret < 0) {
        ESP_LOGE(TAG, "发送二进制数据失败 (%d 字节)", (int)size);
        return ESP_FAIL;
    }

    return ESP_OK;
}

ws_status_t ws_client_get_status(void)
{
    return s_ws_status;
}

void ws_client_deinit(void)
{
    ws_client_disconnect();
    if (s_ws_client) {
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }
    if (s_send_mutex) {
        vSemaphoreDelete(s_send_mutex);
        s_send_mutex = NULL;
    }
    ESP_LOGI(TAG, "WebSocket客户端已释放");
}