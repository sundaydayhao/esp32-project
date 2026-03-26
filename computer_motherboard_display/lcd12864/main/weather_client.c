/**
 * @file weather_client.c
 * @brief 天气数据客户端实现（和风天气 QWeather API v7）
 *
 * API 文档：https://dev.qweather.com/docs/api/weather/weather-now/
 * 响应示例：
 * {
 *   "code": "200",
 *   "now": {
 *     "temp": "25",
 *     "feelsLike": "27",
 *     "text": "晴",
 *     "windDir": "东北风",
 *     "windSpeed": "10",
 *     "humidity": "60",
 *     "obsTime": "2026-03-26T10:00+08:00"
 *   }
 * }
 */
#include "weather_client.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "weather";

/* ----------------------------------------------------------------
 * Kconfig 默认值
 * ---------------------------------------------------------------- */
#ifndef CONFIG_WEATHER_API_KEY
#define CONFIG_WEATHER_API_KEY          "your_qweather_api_key"
#endif
#ifndef CONFIG_WEATHER_LOCATION_ID
#define CONFIG_WEATHER_LOCATION_ID      "101010100"   /* 北京 */
#endif
#ifndef CONFIG_WEATHER_UPDATE_INTERVAL_MIN
#define CONFIG_WEATHER_UPDATE_INTERVAL_MIN  30
#endif

/* 和风天气免费版 API 地址 */
#define WEATHER_API_URL \
    "https://devapi.qweather.com/v7/weather/now?location=%s&key=%s"

#define HTTP_RECV_BUF_SIZE  2048

/* ----------------------------------------------------------------
 * 内部状态
 * ---------------------------------------------------------------- */
static weather_data_t s_weather = {0};
static char          *s_recv_buf = NULL;
static int            s_recv_len = 0;

/* ----------------------------------------------------------------
 * HTTP 事件回调
 * ---------------------------------------------------------------- */
static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client)) {
            if (s_recv_buf && (s_recv_len + evt->data_len < HTTP_RECV_BUF_SIZE - 1)) {
                memcpy(s_recv_buf + s_recv_len, evt->data, evt->data_len);
                s_recv_len += evt->data_len;
                s_recv_buf[s_recv_len] = '\0';
            }
        }
        break;
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP error");
        break;
    default:
        break;
    }
    return ESP_OK;
}

/* ----------------------------------------------------------------
 * 解析 JSON 响应
 * ---------------------------------------------------------------- */
static esp_err_t parse_weather_json(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_FAIL;
    }

    /* 检查返回码 */
    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!code || strcmp(code->valuestring, "200") != 0) {
        ESP_LOGE(TAG, "API error code: %s",
                 code ? code->valuestring : "null");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *now = cJSON_GetObjectItem(root, "now");
    if (!now) {
        ESP_LOGE(TAG, "No 'now' field in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    /* 解析各字段 */
    cJSON *item;

    item = cJSON_GetObjectItem(now, "text");
    if (item && item->valuestring) {
        strncpy(s_weather.text, item->valuestring,
                sizeof(s_weather.text) - 1);
    }

    item = cJSON_GetObjectItem(now, "temp");
    if (item && item->valuestring) {
        s_weather.temp = (int8_t)atoi(item->valuestring);
    }

    item = cJSON_GetObjectItem(now, "feelsLike");
    if (item && item->valuestring) {
        s_weather.feels_like = (int8_t)atoi(item->valuestring);
    }

    item = cJSON_GetObjectItem(now, "humidity");
    if (item && item->valuestring) {
        s_weather.humidity = (uint8_t)atoi(item->valuestring);
    }

    item = cJSON_GetObjectItem(now, "windSpeed");
    if (item && item->valuestring) {
        s_weather.wind_speed = (uint16_t)atoi(item->valuestring);
    }

    item = cJSON_GetObjectItem(now, "windDir");
    if (item && item->valuestring) {
        strncpy(s_weather.wind_dir, item->valuestring,
                sizeof(s_weather.wind_dir) - 1);
    }

    item = cJSON_GetObjectItem(now, "obsTime");
    if (item && item->valuestring) {
        strncpy(s_weather.update_time, item->valuestring,
                sizeof(s_weather.update_time) - 1);
    }

    s_weather.valid = true;

    ESP_LOGI(TAG, "Weather: %s, %d°C, humidity=%d%%",
             s_weather.text, s_weather.temp, s_weather.humidity);

    cJSON_Delete(root);
    return ESP_OK;
}

/* ----------------------------------------------------------------
 * 公共 API
 * ---------------------------------------------------------------- */
esp_err_t weather_client_init(void)
{
    if (!s_recv_buf) {
        s_recv_buf = (char *)malloc(HTTP_RECV_BUF_SIZE);
        if (!s_recv_buf) {
            ESP_LOGE(TAG, "Failed to allocate HTTP buffer");
            return ESP_ERR_NO_MEM;
        }
    }
    memset(&s_weather, 0, sizeof(s_weather));
    return ESP_OK;
}

esp_err_t weather_client_fetch(void)
{
    if (!s_recv_buf) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* 构造 URL */
    char url[256];
    snprintf(url, sizeof(url), WEATHER_API_URL,
             CONFIG_WEATHER_LOCATION_ID,
             CONFIG_WEATHER_API_KEY);

    /* 重置接收缓冲区 */
    s_recv_len = 0;
    memset(s_recv_buf, 0, HTTP_RECV_BUF_SIZE);

    esp_http_client_config_t config = {
        .url            = url,
        .event_handler  = http_event_cb,
        .timeout_ms     = 10000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP status=%d, len=%d", status, s_recv_len);
        if (status == 200 && s_recv_len > 0) {
            ret = parse_weather_json(s_recv_buf);
        } else {
            ret = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    return ret;
}

void weather_client_get_data(weather_data_t *out)
{
    if (out) {
        memcpy(out, &s_weather, sizeof(weather_data_t));
    }
}

/* ----------------------------------------------------------------
 * 天气周期更新任务
 * ---------------------------------------------------------------- */
void weather_task(void *arg)
{
    (void)arg;

    /* 启动后稍等 WiFi/NTP 稳定 */
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        ESP_LOGI(TAG, "Fetching weather data...");
        esp_err_t ret = weather_client_fetch();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Weather fetch failed, will retry in 5 min");
            vTaskDelay(pdMS_TO_TICKS(5 * 60 * 1000));
        } else {
            /* 正常间隔 */
            vTaskDelay(pdMS_TO_TICKS(
                (uint32_t)CONFIG_WEATHER_UPDATE_INTERVAL_MIN * 60 * 1000));
        }
    }
}