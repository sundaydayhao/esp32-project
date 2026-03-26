/**
 * WebSocket 客户端模块
 * 与后端服务器进行实时双向通信
 */

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "mbedtls/base64.h"
#include "cJSON.h"

#include "config.h"
#include "websocket_client.h"

static const char *TAG = "WS_CLIENT";

static esp_websocket_client_handle_t s_ws_client = NULL;
static ws_connected_cb_t s_on_connected = NULL;
static ws_disconnected_cb_t s_on_disconnected = NULL;
static ws_data_cb_t s_on_data = NULL;
static bool s_is_connected = false;

// 接收缓冲区（用于处理分片消息）
#define RX_BUFFER_SIZE  (32 * 1024)  // 32KB
static char *s_rx_buffer = NULL;
static size_t s_rx_buffer_len = 0;

/**
 * WebSocket 事件处理
 */
static void ws_event_handler(void *handler_args, esp_event_base_t base,
                              int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket 已连接");
            s_is_connected = true;
            s_rx_buffer_len = 0;
            if (s_on_connected) {
                s_on_connected();
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket 已断开");
            s_is_connected = false;
            s_rx_buffer_len = 0;
            if (s_on_disconnected) {
                s_on_disconnected();
            }
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x08) {
                // 关闭帧
                ESP_LOGW(TAG, "收到关闭帧");
                break;
            }

            if (data->data_ptr && data->data_len > 0) {
                // 处理分片消息
                if (s_rx_buffer && s_rx_buffer_len + data->data_len < RX_BUFFER_SIZE) {
                    memcpy(s_rx_buffer + s_rx_buffer_len, data->data_ptr, data->data_len);
                    s_rx_buffer_len += data->data_len;

                    // 检查是否是最后一帧
                    if (data->fin) {
                        s_rx_buffer[s_rx_buffer_len] = '\0';
                        ESP_LOGD(TAG, "收到完整消息，长度: %d", s_rx_buffer_len);

                        if (s_on_data) {
                            s_on_data(s_rx_buffer, s_rx_buffer_len);
                        }
                        s_rx_buffer_len = 0;
                    }
                } else {
                    ESP_LOGE(TAG, "接收缓冲区溢出，丢弃消息");
                    s_rx_buffer_len = 0;
                }
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket 错误");
            s_is_connected = false;
            break;

        default:
            break;
    }
}

esp_err_t ws_client_init(const char *uri,
                          ws_connected_cb_t on_connected,
                          ws_disconnected_cb_t on_disconnected,
                          ws_data_cb_t on_data) {
    s_on_connected = on_connected;
    s_on_disconnected = on_disconnected;
    s_on_data = on_data;

    // 分配接收缓冲区
    s_rx_buffer = (char *)malloc(RX_BUFFER_SIZE + 1);
    if (!s_rx_buffer) {
        ESP_LOGE(TAG, "分配接收缓冲区失败");
        return ESP_ERR_NO_MEM;
    }

    esp_websocket_client_config_t ws_cfg = {
        .uri = uri,
        .buffer_size = WS_BUFFER_SIZE,
        .reconnect_timeout_ms = WS_RECONNECT_INTERVAL_MS,
        .network_timeout_ms = 10000,
        .ping_interval_sec = WS_PING_INTERVAL_MS / 1000,
        .disable_auto_reconnect = false,
    };

    s_ws_client = esp_websocket_client_init(&ws_cfg);
    if (!s_ws_client) {
        ESP_LOGE(TAG, "WebSocket 客户端初始化失败");
        return ESP_FAIL;
    }

    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY,
                                   ws_event_handler, NULL);

    ESP_LOGI(TAG, "WebSocket 客户端初始化完成: %s", uri);
    return ESP_OK;
}

esp_err_t ws_client_start(void) {
    if (!s_ws_client) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_websocket_client_start(s_ws_client);
}

void ws_client_stop(void) {
    if (s_ws_client) {
        esp_websocket_client_stop(s_ws_client);
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }
    if (s_rx_buffer) {
        free(s_rx_buffer);
        s_rx_buffer = NULL;
    }
}

esp_err_t ws_send_text(const char *text) {
    if (!s_ws_client || !s_is_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    int ret = esp_websocket_client_send_text(s_ws_client, text, strlen(text),
                                              pdMS_TO_TICKS(5000));
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t ws_send_json(const char *json_str) {
    return ws_send_text(json_str);
}

esp_err_t ws_send_audio(const uint8_t *audio_data, size_t audio_len, int sample_rate) {
    if (!audio_data || audio_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Base64 编码
    size_t b64_len = ((audio_len + 2) / 3) * 4 + 1;
    char *b64_data = (char *)malloc(b64_len);
    if (!b64_data) {
        ESP_LOGE(TAG, "分配 Base64 缓冲区失败");
        return ESP_ERR_NO_MEM;
    }

    size_t out_len = 0;
    int ret = mbedtls_base64_encode(
        (unsigned char *)b64_data, b64_len, &out_len,
        audio_data, audio_len
    );

    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 编码失败: %d", ret);
        free(b64_data);
        return ESP_FAIL;
    }
    b64_data[out_len] = '\0';

    // 构建 JSON 消息
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "audio");
    cJSON_AddStringToObject(root, "data", b64_data);
    cJSON_AddNumberToObject(root, "sample_rate", sample_rate);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(b64_data);

    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "发送音频数据，原始大小: %d bytes，Base64 大小: %d bytes",
             audio_len, out_len);

    esp_err_t err = ws_send_text(json_str);
    free(json_str);

    return err;
}

esp_err_t ws_send_keywords(const char **keywords, int count) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "keywords");

    cJSON *kw_array = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(kw_array, cJSON_CreateString(keywords[i]));
    }
    cJSON_AddItemToObject(root, "keywords", kw_array);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ws_send_text(json_str);
    free(json_str);
    return err;
}

esp_err_t ws_send_ping(void) {
    return ws_send_json("{\"type\":\"ping\"}");
}

bool ws_is_connected(void) {
    return s_is_connected;
}