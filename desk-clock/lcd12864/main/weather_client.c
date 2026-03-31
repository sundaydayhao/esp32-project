/**
 * @file weather_client.c
 * @brief QWeather daily forecast client
 */

#include "weather_client.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/error.h"
#include "mbedtls/x509_crt.h"
#include "miniz.h"

#include "ntp_client.h"
#include "wifi_manager.h"

static const char *TAG = "weather";

extern const char certs_isrg_root_x1_pem_start[] asm("_binary_isrg_root_x1_pem_start");

#ifndef CONFIG_WEATHER_API_KEY
#define CONFIG_WEATHER_API_KEY              "04c81d5d4f3346a48d69b886c6c98cb0"
#endif
#ifndef CONFIG_WEATHER_LOCATION_ID
#define CONFIG_WEATHER_LOCATION_ID          "101280601"
#endif
#ifndef CONFIG_WEATHER_UPDATE_INTERVAL_MIN
#define CONFIG_WEATHER_UPDATE_INTERVAL_MIN  30
#endif

#define WEATHER_API_URL_FMT \
    "https://nx5khwapkg.re.qweatherapi.com/v7/weather/3d?location=%s&key=%s"

#define HTTP_BUF_SIZE            4096
#define WEATHER_NTP_WAIT_MS      45000
#define WEATHER_HTTP_TIMEOUT_MS  15000

static weather_data_t s_weather;
static char *s_http_buf;
static int s_http_len;

static const char *weather_root_cert_pem(void)
{
    return certs_isrg_root_x1_pem_start;
}

static void log_tls_failure_details(esp_http_client_handle_t client)
{
    int tls_code = 0;
    int tls_flags = 0;
    int sock_errno = esp_http_client_get_errno(client);
    char tls_buf[128] = {0};
    char verify_buf[256] = {0};

    esp_http_client_get_and_clear_last_tls_error(client, &tls_code, &tls_flags);

    if (tls_code != 0) {
        mbedtls_strerror(tls_code, tls_buf, sizeof(tls_buf));
    }
    if (tls_flags != 0) {
        mbedtls_x509_crt_verify_info(verify_buf, sizeof(verify_buf), "", (uint32_t)tls_flags);
    }

    ESP_LOGE(TAG, "TLS details: tls_code=%d (%s) tls_flags=0x%x errno=%d",
             tls_code, tls_code != 0 ? tls_buf : "none", tls_flags, sock_errno);
    if (tls_flags != 0) {
        ESP_LOGE(TAG, "TLS verify flags:%s", verify_buf);
    }
}

static bool parse_gzip_header(const uint8_t *data, size_t len, size_t *payload_offset)
{
    const uint8_t flag_fextra = 0x04;
    const uint8_t flag_fname = 0x08;
    const uint8_t flag_fcomment = 0x10;
    const uint8_t flag_fhcrc = 0x02;

    if (!data || !payload_offset || len < 10) {
        return false;
    }
    if (data[0] != 0x1F || data[1] != 0x8B || data[2] != 0x08) {
        return false;
    }

    size_t off = 10;
    uint8_t flags = data[3];

    if (flags & flag_fextra) {
        if (off + 2 > len) {
            return false;
        }
        size_t extra_len = (size_t)data[off] | ((size_t)data[off + 1] << 8);
        off += 2 + extra_len;
    }
    if (flags & flag_fname) {
        while (off < len && data[off] != '\0') {
            ++off;
        }
        ++off;
    }
    if (flags & flag_fcomment) {
        while (off < len && data[off] != '\0') {
            ++off;
        }
        ++off;
    }
    if (flags & flag_fhcrc) {
        off += 2;
    }

    if (off >= len) {
        return false;
    }

    *payload_offset = off;
    return true;
}

static void maybe_decompress_http_body(void)
{
    if (!s_http_buf || s_http_len < 2) {
        return;
    }

    const uint8_t *src = (const uint8_t *)s_http_buf;
    if (src[0] != 0x1F || src[1] != 0x8B) {
        return;
    }

    size_t payload_offset = 0;
    if (!parse_gzip_header(src, (size_t)s_http_len, &payload_offset) ||
        payload_offset + 8 > (size_t)s_http_len) {
        ESP_LOGE(TAG, "Invalid gzip weather payload");
        return;
    }

    size_t compressed_len = (size_t)s_http_len - payload_offset - 8;
    size_t decoded_len = (size_t)src[s_http_len - 4] |
                         ((size_t)src[s_http_len - 3] << 8) |
                         ((size_t)src[s_http_len - 2] << 16) |
                         ((size_t)src[s_http_len - 1] << 24);
    if (decoded_len == 0) {
        ESP_LOGE(TAG, "Gzip payload reported zero decoded length");
        return;
    }

    uint8_t *decoded = (uint8_t *)malloc(decoded_len + 1);
    if (!decoded) {
        ESP_LOGE(TAG, "No memory for gzip payload: %u bytes", (unsigned)decoded_len);
        return;
    }

    size_t out_len = tinfl_decompress_mem_to_mem(decoded, decoded_len,
                                                 src + payload_offset, compressed_len, 0);
    if (out_len == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        ESP_LOGE(TAG, "Gzip decompress failed");
        free(decoded);
        return;
    }

    size_t copy_len = out_len;
    if (copy_len >= HTTP_BUF_SIZE) {
        copy_len = HTTP_BUF_SIZE - 1;
        ESP_LOGW(TAG, "Decoded weather payload truncated: %u bytes", (unsigned)out_len);
    }

    memcpy(s_http_buf, decoded, copy_len);
    s_http_buf[copy_len] = '\0';
    s_http_len = (int)copy_len;
    free(decoded);
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->data && evt->data_len > 0) {
            int copy = evt->data_len;
            if (s_http_len + copy >= HTTP_BUF_SIZE - 1) {
                copy = HTTP_BUF_SIZE - 1 - s_http_len;
            }
            if (copy > 0) {
                memcpy(s_http_buf + s_http_len, evt->data, (size_t)copy);
                s_http_len += copy;
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        if (s_http_buf) {
            s_http_buf[s_http_len] = '\0';
        }
        break;
    default:
        break;
    }

    return ESP_OK;
}

static esp_err_t parse_weather_json(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        const char *err = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "JSON parse failed near: %.32s", err ? err : "unknown");
        return ESP_FAIL;
    }

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!cJSON_IsString(code) || strcmp(code->valuestring, "200") != 0) {
        ESP_LOGE(TAG, "API error code: %s",
                 (code && cJSON_IsString(code)) ? code->valuestring : "unknown");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (!cJSON_IsArray(daily) || cJSON_GetArraySize(daily) <= 0) {
        ESP_LOGE(TAG, "No daily forecast array");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *today = cJSON_GetArrayItem(daily, 0);
    if (!cJSON_IsObject(today)) {
        ESP_LOGE(TAG, "Invalid daily forecast entry");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    memset(&s_weather, 0, sizeof(s_weather));

    cJSON *item = cJSON_GetObjectItem(today, "textDay");
    if (cJSON_IsString(item)) {
        strncpy(s_weather.text, item->valuestring, sizeof(s_weather.text) - 1);
    }

    item = cJSON_GetObjectItem(today, "fxDate");
    if (cJSON_IsString(item)) {
        strncpy(s_weather.forecast_date, item->valuestring, sizeof(s_weather.forecast_date) - 1);
    }

    item = cJSON_GetObjectItem(today, "tempMin");
    if (cJSON_IsString(item)) {
        s_weather.temp_min = (int8_t)atoi(item->valuestring);
    }

    item = cJSON_GetObjectItem(today, "tempMax");
    if (cJSON_IsString(item)) {
        s_weather.temp_max = (int8_t)atoi(item->valuestring);
    }

    item = cJSON_GetObjectItem(today, "humidity");
    if (cJSON_IsString(item)) {
        s_weather.humidity = (uint8_t)atoi(item->valuestring);
    }

    if (s_weather.forecast_date[0] != '\0') {
        strncpy(s_weather.update_time, s_weather.forecast_date, sizeof(s_weather.update_time) - 1);
    }

    s_weather.valid = true;
    ESP_LOGI(TAG, "Forecast: %s, %d-%dC, humidity=%u%%",
             s_weather.text,
             (int)s_weather.temp_min,
             (int)s_weather.temp_max,
             (unsigned)s_weather.humidity);

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t weather_client_init(void)
{
    memset(&s_weather, 0, sizeof(s_weather));
    if (!s_http_buf) {
        s_http_buf = (char *)malloc(HTTP_BUF_SIZE);
        if (!s_http_buf) {
            ESP_LOGE(TAG, "Failed to allocate HTTP buffer");
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t weather_client_fetch(void)
{
    if (!s_http_buf) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi is not connected, skip weather update");
        return ESP_ERR_INVALID_STATE;
    }

    char url[256];
    snprintf(url, sizeof(url), WEATHER_API_URL_FMT,
             CONFIG_WEATHER_LOCATION_ID, CONFIG_WEATHER_API_KEY);

    s_http_len = 0;
    memset(s_http_buf, 0, HTTP_BUF_SIZE);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .cert_pem = weather_root_cert_pem(),
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_http_client_set_header(client, "Accept", "application/json"));
    ESP_ERROR_CHECK(esp_http_client_set_header(client, "Accept-Encoding", "identity"));

    esp_err_t ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    if (ret == ESP_OK && s_http_len > 0) {
        maybe_decompress_http_body();
    }

    ESP_LOGI(TAG, "HTTP status=%d, len=%d", status, s_http_len);
    if (ret != ESP_OK || status != 200 || s_http_len <= 0) {
        log_tls_failure_details(client);
        ESP_LOGE(TAG, "HTTP request failed: ret=%s status=%d body=%s",
                 esp_err_to_name(ret), status, s_http_len > 0 ? s_http_buf : "<empty>");
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    ret = parse_weather_json(s_http_buf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse weather body=%s", s_http_buf);
    }

    esp_http_client_cleanup(client);
    return ret;
}

void weather_client_get_data(weather_data_t *out)
{
    if (out) {
        *out = s_weather;
    }
}

void weather_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Weather task started");

    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (!ntp_client_is_synced()) {
        ESP_LOGI(TAG, "Waiting for first NTP sync before HTTPS weather request");
        (void)ntp_client_wait_sync(WEATHER_NTP_WAIT_MS);
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        ESP_LOGI(TAG, "Fetching daily forecast...");
        esp_err_t ret = weather_client_fetch();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Weather fetch failed, will retry in 5 min");
            vTaskDelay(pdMS_TO_TICKS(5 * 60 * 1000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(
                (uint32_t)CONFIG_WEATHER_UPDATE_INTERVAL_MIN * 60 * 1000));
        }
    }
}
