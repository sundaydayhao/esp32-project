#include "notification.h"
#include "wifi_manager.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "notification";

/* Server酱 API 端点 */
#define SERVERCHAN_API_URL  "https://sctapi.ftqq.com/%s.send"
#define HTTP_TIMEOUT_MS     15000
#define MAX_CONTENT_LEN     2048
#define MAX_URL_LEN         256

/* 通知队列（异步发送） */
typedef struct {
    char title[128];
    char content[MAX_CONTENT_LEN];
} notify_msg_t;

static QueueHandle_t s_notify_queue = NULL;
static TaskHandle_t  s_notify_task  = NULL;
static bool          s_initialized  = false;

/* HTTP 事件处理（用于接收响应） */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                ESP_LOGD(TAG, "HTTP 响应: %.*s", evt->data_len, (char *)evt->data);
            }
            break;
        case HTTP_EVENT_ERROR:
            ESP_LOGW(TAG, "HTTP 错误");
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief 实际执行 HTTP POST 发送通知
 */
static esp_err_t do_send_notification(const char *title, const char *content)
{
    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi 未连接，无法发送通知");
        return ESP_ERR_INVALID_STATE;
    }

    const char *sendkey = CONFIG_DOORMAN_SERVERCHAN_KEY;
    if (strlen(sendkey) == 0) {
        ESP_LOGW(TAG, "Server酱 SendKey 未配置，跳过通知");
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* 构建 API URL */
    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), SERVERCHAN_API_URL, sendkey);

    /* 构建 POST 数据（URL 编码） */
    /* 简单处理：对特殊字符进行基本转义 */
    char post_data[MAX_CONTENT_LEN + 256];
    int post_len = snprintf(post_data, sizeof(post_data),
                            "title=%s&desp=%s",
                            title, content);

    if (post_len <= 0 || post_len >= (int)sizeof(post_data)) {
        ESP_LOGE(TAG, "POST 数据构建失败");
        return ESP_ERR_INVALID_SIZE;
    }

    /* 配置 HTTP 客户端 */
    esp_http_client_config_t config = {
        .url            = url,
        .method         = HTTP_METHOD_POST,
        .timeout_ms     = HTTP_TIMEOUT_MS,
        .event_handler  = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach, /* 使用内置证书包 */
        .buffer_size    = 1024,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP 客户端初始化失败");
        return ESP_FAIL;
    }

    /* 设置请求头和数据 */
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, post_len);

    /* 执行请求 */
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "通知发送成功，HTTP 状态码: %d", status);
        if (status != 200) {
            ESP_LOGW(TAG, "Server酱 返回非 200 状态，请检查 SendKey 是否正确");
        }
    } else {
        ESP_LOGE(TAG, "通知发送失败: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

/**
 * @brief 通知发送任务（异步处理队列中的通知）
 */
static void notification_task(void *pvParameters)
{
    notify_msg_t msg;
    ESP_LOGI(TAG, "通知发送任务启动");

    while (1) {
        if (xQueueReceive(s_notify_queue, &msg, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "发送通知: %s", msg.title);
            esp_err_t ret = do_send_notification(msg.title, msg.content);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "通知发送失败，将在 5 秒后重试");
                vTaskDelay(pdMS_TO_TICKS(5000));
                /* 重试一次 */
                do_send_notification(msg.title, msg.content);
            }
            /* 发送间隔，避免频繁请求 */
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

esp_err_t notification_init(void)
{
    if (s_initialized) return ESP_OK;

    /* 创建通知队列 */
    s_notify_queue = xQueueCreate(5, sizeof(notify_msg_t));
    if (s_notify_queue == NULL) {
        ESP_LOGE(TAG, "创建通知队列失败");
        return ESP_ERR_NO_MEM;
    }

    /* 创建通知发送任务 */
    BaseType_t ret = xTaskCreate(notification_task, "notify_task",
                                  8192, NULL, 3, &s_notify_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建通知任务失败");
        vQueueDelete(s_notify_queue);
        s_notify_queue = NULL;
        return ESP_FAIL;
    }

    s_initialized = true;

    const char *sendkey = CONFIG_DOORMAN_SERVERCHAN_KEY;
    if (strlen(sendkey) > 0) {
        ESP_LOGI(TAG, "通知模块初始化完成 (Server酱 SendKey: %.*s...)",
                 8, sendkey);
    } else {
        ESP_LOGW(TAG, "通知模块初始化完成，但 Server酱 SendKey 未配置！");
        ESP_LOGW(TAG, "请前往 https://sct.ftqq.com 注册并在 menuconfig 中配置 SendKey");
    }

    return ESP_OK;
}

esp_err_t notification_send_wechat(notify_type_t type,
                                    const char *image_url,
                                    const char *extra_info)
{
    if (!s_initialized || s_notify_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    notify_msg_t msg;
    memset(&msg, 0, sizeof(msg));

    /* 根据事件类型构建通知内容 */
    const char *emoji = "";
    const char *type_str = "";

    switch (type) {
        case NOTIFY_TYPE_PERSON:
            emoji    = "🚨";
            type_str = "门口有人活动";
            strncpy(msg.title, CONFIG_DOORMAN_WECHAT_NOTIFY_TITLE, sizeof(msg.title) - 1);
            break;
        case NOTIFY_TYPE_PACKAGE:
            emoji    = "📦";
            type_str = "快递到达";
            strncpy(msg.title, "📦 门口有快递！", sizeof(msg.title) - 1);
            break;
        case NOTIFY_TYPE_ONLINE:
            emoji    = "✅";
            type_str = "设备上线";
            strncpy(msg.title, "✅ 门卫系统已上线", sizeof(msg.title) - 1);
            break;
        case NOTIFY_TYPE_OFFLINE:
            emoji    = "❌";
            type_str = "设备离线";
            strncpy(msg.title, "❌ 门卫系统离线", sizeof(msg.title) - 1);
            break;
        default:
            emoji    = "ℹ️";
            type_str = "未知事件";
            strncpy(msg.title, "门卫系统通知", sizeof(msg.title) - 1);
            break;
    }

    /* 构建 Markdown 内容 */
    int offset = 0;
    offset += snprintf(msg.content + offset, sizeof(msg.content) - offset,
                       "## %s %s\n\n", emoji, type_str);

    offset += snprintf(msg.content + offset, sizeof(msg.content) - offset,
                       "**时间**: %llu 秒（设备运行时间）\n\n",
                       (unsigned long long)(esp_timer_get_time() / 1000000));

    if (image_url && strlen(image_url) > 0) {
        offset += snprintf(msg.content + offset, sizeof(msg.content) - offset,
                           "**查看图像**: [点击查看实时画面](%s)\n\n", image_url);
        offset += snprintf(msg.content + offset, sizeof(msg.content) - offset,
                           "**最新图像**: [直接查看](%slatest)\n\n", image_url);
    }

    if (extra_info && strlen(extra_info) > 0) {
        offset += snprintf(msg.content + offset, sizeof(msg.content) - offset,
                           "**详情**: %s\n\n", extra_info);
    }

    snprintf(msg.content + offset, sizeof(msg.content) - offset,
             "> 来自 ESP32-S3 门卫系统");

    /* 加入发送队列 */
    if (xQueueSend(s_notify_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "通知队列已满，丢弃本次通知");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "通知已加入队列: %s", msg.title);
    return ESP_OK;
}

esp_err_t notification_send_custom(const char *title, const char *content)
{
    if (!s_initialized || s_notify_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (title == NULL || content == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    notify_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.title,   title,   sizeof(msg.title)   - 1);
    strncpy(msg.content, content, sizeof(msg.content) - 1);

    if (xQueueSend(s_notify_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "通知队列已满");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

bool notification_is_available(void)
{
    return s_initialized &&
           wifi_manager_is_connected() &&
           strlen(CONFIG_DOORMAN_SERVERCHAN_KEY) > 0;
}