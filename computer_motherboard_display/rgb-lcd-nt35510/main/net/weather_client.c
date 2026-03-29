/**
 * @file weather_client.c
 * @brief QWeather v7 client
 */
#include "weather_client.h"
#include "config.h"
#include "net/ntp_client.h"
#include "net/wifi_manager.h"
#include "storage/config_store.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "miniz.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

#include "mbedtls/error.h"
#include "mbedtls/x509_crt.h"

#define HTTP_BUF_SIZE 4096
#define WEATHER_NTP_WAIT_MS 45000
#define WEATHER_VALID_UNIX_TIME 1735689600LL

static weather_data_t s_data = { 0 };
static char *s_http_buf = NULL;
static int s_http_len = 0;

#if !CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY
extern const char certs_isrg_root_x1_pem_start[] asm("_binary_isrg_root_x1_pem_start");
static const char *weather_root_cert_pem(void)
{
    return certs_isrg_root_x1_pem_start;
}
#endif

static void log_tls_heap(const char *stage)
{
    ESP_LOGI(TAG_WEATHER, "%s heap=%luKB internal=%luKB largest_internal=%luKB",
             stage,
             (unsigned long)(esp_get_free_heap_size() / 1024),
             (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned long)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
}

static void log_weather_clock_state(const char *stage)
{
    time_t now = ntp_client_get_time();
    struct tm tm_local = { 0 };
    char buf[32] = "invalid";

    if (now > 0) {
        localtime_r(&now, &tm_local);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_local);
    }

    ESP_LOGI(TAG_WEATHER, "%s time=%s synced=%s epoch=%lld",
             stage, buf, ntp_client_is_synced() ? "yes" : "no",
             (long long)now);
}

static bool weather_time_is_valid(void)
{
    return ntp_client_get_time() >= WEATHER_VALID_UNIX_TIME;
}

static void log_tls_failure_details(esp_http_client_handle_t client)
{
    int tls_code = 0;
    int tls_flags = 0;
    int sock_errno = esp_http_client_get_errno(client);
    char tls_buf[128] = { 0 };
    char verify_buf[256] = { 0 };

    esp_http_client_get_and_clear_last_tls_error(client, &tls_code, &tls_flags);

    if (tls_code != 0) {
        mbedtls_strerror(tls_code, tls_buf, sizeof(tls_buf));
    }
    if (tls_flags != 0) {
        mbedtls_x509_crt_verify_info(verify_buf, sizeof(verify_buf), "", (uint32_t)tls_flags);
    }

    ESP_LOGE(TAG_WEATHER, "TLS details: tls_code=%d (%s) tls_flags=0x%x errno=%d",
             tls_code, tls_code != 0 ? tls_buf : "none", tls_flags, sock_errno);
    if (tls_flags != 0) {
        ESP_LOGE(TAG_WEATHER, "TLS verify flags:%s", verify_buf);
    }
    log_weather_clock_state("weather tls failure");
}

static bool parse_gzip_header(const uint8_t *data, size_t len, size_t *payload_offset)
{
    const uint8_t gzip_id1 = 0x1F;
    const uint8_t gzip_id2 = 0x8B;
    const uint8_t gzip_cm_deflate = 0x08;
    const uint8_t flag_fextra = 0x04;
    const uint8_t flag_fname = 0x08;
    const uint8_t flag_fcomment = 0x10;
    const uint8_t flag_fhcrc = 0x02;

    if (!data || len < 10 || !payload_offset) {
        return false;
    }
    if (data[0] != gzip_id1 || data[1] != gzip_id2 || data[2] != gzip_cm_deflate) {
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
            off++;
        }
        off++;
    }
    if (flags & flag_fcomment) {
        while (off < len && data[off] != '\0') {
            off++;
        }
        off++;
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
    if (s_http_len < 2) {
        return;
    }

    const uint8_t *src = (const uint8_t *)s_http_buf;
    if (src[0] != 0x1F || src[1] != 0x8B) {
        return;
    }

    size_t payload_offset = 0;
    if (!parse_gzip_header(src, (size_t)s_http_len, &payload_offset) || payload_offset + 8 > (size_t)s_http_len) {
        ESP_LOGE(TAG_WEATHER, "invalid gzip weather payload");
        return;
    }

    size_t compressed_len = (size_t)s_http_len - payload_offset - 8;
    size_t decoded_len = (size_t)src[s_http_len - 4] |
                         ((size_t)src[s_http_len - 3] << 8) |
                         ((size_t)src[s_http_len - 2] << 16) |
                         ((size_t)src[s_http_len - 1] << 24);
    if (decoded_len == 0) {
        ESP_LOGE(TAG_WEATHER, "gzip payload reported zero decoded length");
        return;
    }

    uint8_t *decoded = malloc(decoded_len + 1);
    if (!decoded) {
        ESP_LOGE(TAG_WEATHER, "no memory for gzip payload: %u bytes", (unsigned)decoded_len);
        return;
    }

    size_t out_len = tinfl_decompress_mem_to_mem(decoded, decoded_len, src + payload_offset, compressed_len, 0);
    if (out_len == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
        ESP_LOGE(TAG_WEATHER, "gzip decompress failed: payload_off=%u compressed=%u decoded=%u",
                 (unsigned)payload_offset, (unsigned)compressed_len, (unsigned)decoded_len);
        free(decoded);
        return;
    }

    size_t copy_len = out_len;
    if (copy_len >= HTTP_BUF_SIZE) {
        copy_len = HTTP_BUF_SIZE - 1;
        ESP_LOGW(TAG_WEATHER, "decoded weather payload truncated: %u bytes", (unsigned)out_len);
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
                memcpy(s_http_buf + s_http_len, evt->data, copy);
                s_http_len += copy;
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        s_http_buf[s_http_len] = '\0';
        break;
    default:
        break;
    }

    return ESP_OK;
}

static esp_err_t parse_now_weather(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return ESP_FAIL;
    }

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!code || !cJSON_IsString(code) || strcmp(code->valuestring, "200") != 0) {
        ESP_LOGE(TAG_WEATHER, "QWeather now response code: %s",
                 (code && cJSON_IsString(code)) ? code->valuestring : "unknown");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *now = cJSON_GetObjectItem(root, "now");
    if (!now) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

#define GET_STR(key, dst) do { \
    cJSON *item = cJSON_GetObjectItem(now, key); \
    if (item && cJSON_IsString(item)) { \
        strlcpy((dst), item->valuestring, sizeof(dst)); \
    } \
} while (0)

#define GET_INT(key, dst) do { \
    cJSON *item = cJSON_GetObjectItem(now, key); \
    if (item && cJSON_IsString(item)) { \
        (dst) = (int8_t)atoi(item->valuestring); \
    } \
} while (0)

    GET_STR("icon", s_data.icon_code);
    GET_INT("temp", s_data.temp_now);
    GET_INT("feelsLike", s_data.temp_feels);
    GET_INT("humidity", s_data.humidity);

    cJSON *wind_speed = cJSON_GetObjectItem(now, "windSpeed");
    if (wind_speed && cJSON_IsString(wind_speed)) {
        s_data.wind_speed = (uint16_t)atoi(wind_speed->valuestring);
    }

    cJSON *visibility = cJSON_GetObjectItem(now, "vis");
    if (visibility && cJSON_IsString(visibility)) {
        s_data.visibility = (uint16_t)atoi(visibility->valuestring);
    }

#undef GET_STR
#undef GET_INT

    s_data.update_time = esp_timer_get_time() / 1000000;
    s_data.valid = true;
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t parse_forecast(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return ESP_FAIL;
    }

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!code || !cJSON_IsString(code) || strcmp(code->valuestring, "200") != 0) {
        ESP_LOGE(TAG_WEATHER, "QWeather forecast response code: %s",
                 (code && cJSON_IsString(code)) ? code->valuestring : "unknown");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (!daily || !cJSON_IsArray(daily)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    int cnt = cJSON_GetArraySize(daily);
    if (cnt > 3) {
        cnt = 3;
    }

    for (int i = 0; i < cnt; i++) {
        cJSON *day = cJSON_GetArrayItem(daily, i);
        if (!day) {
            continue;
        }

        cJSON *item = cJSON_GetObjectItem(day, "fxDate");
        if (item && cJSON_IsString(item)) {
            strlcpy(s_data.forecast[i].date, item->valuestring, sizeof(s_data.forecast[i].date));
        }

        item = cJSON_GetObjectItem(day, "tempMax");
        if (item && cJSON_IsString(item)) {
            s_data.forecast[i].high = (int8_t)atoi(item->valuestring);
        }

        item = cJSON_GetObjectItem(day, "tempMin");
        if (item && cJSON_IsString(item)) {
            s_data.forecast[i].low = (int8_t)atoi(item->valuestring);
        }

        item = cJSON_GetObjectItem(day, "iconDay");
        if (item && cJSON_IsString(item)) {
            strlcpy(s_data.forecast[i].icon_day, item->valuestring, sizeof(s_data.forecast[i].icon_day));
        }
    }

    if (cnt > 0) {
        s_data.temp_high = s_data.forecast[0].high;
        s_data.temp_low = s_data.forecast[0].low;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t http_get(const char *url, const char *api_key)
{
    s_http_len = 0;
    memset(s_http_buf, 0, HTTP_BUF_SIZE);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
#if !CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY
        .cert_pem = weather_root_cert_pem(),
#endif
        .user_agent = DEVICE_NAME "/" FW_VERSION_STR,
    };

#if CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY
    ESP_LOGW(TAG_WEATHER, "TLS server certificate verification is disabled for weather requests");
#endif

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_FAIL;
    }

    if (!api_key || !api_key[0]) {
        ESP_LOGE(TAG_WEATHER, "weather api key is empty");
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_ERROR_CHECK(esp_http_client_set_header(client, "X-QW-Api-Key", api_key));
    ESP_ERROR_CHECK(esp_http_client_set_header(client, "Accept", "application/json"));
    ESP_ERROR_CHECK(esp_http_client_set_header(client, "Accept-Encoding", "identity"));

    log_weather_clock_state("weather request start");
    log_tls_heap("weather request start");
    esp_err_t ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    if (ret == ESP_OK && s_http_len > 0) {
        maybe_decompress_http_body();
    }

    if (ret != ESP_OK || status != 200) {
        log_tls_failure_details(client);
        ESP_LOGE(TAG_WEATHER, "HTTP request failed: ret=%s status=%d body=%s",
                 esp_err_to_name(ret), status, s_http_len > 0 ? s_http_buf : "<empty>");
        log_tls_heap("weather request failed");
        if (status == 403) {
            ESP_LOGE(TAG_WEATHER, "QWeather authorization failed. Check API Key, API Host and console security settings.");
        }
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    esp_http_client_cleanup(client);
    return ESP_OK;
}

esp_err_t weather_client_init(void)
{
    memset(&s_data, 0, sizeof(s_data));
    if (!s_http_buf) {
        s_http_buf = heap_caps_malloc(HTTP_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_http_buf) {
            s_http_buf = malloc(HTTP_BUF_SIZE);
        }
        if (!s_http_buf) {
            ESP_LOGE(TAG_WEATHER, "failed to allocate HTTP buffer");
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG_WEATHER, "Weather client initialized");
    return ESP_OK;
}

esp_err_t weather_client_fetch(void)
{
    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG_WEATHER, "WiFi is not connected, skip weather update");
        return ESP_ERR_INVALID_STATE;
    }
    if (!weather_time_is_valid()) {
        ESP_LOGW(TAG_WEATHER, "System time is not valid yet, skip weather update");
        log_weather_clock_state("weather skipped");
        return ESP_ERR_INVALID_STATE;
    }

    char api_key[64] = WEATHER_API_KEY;
    char location[32] = WEATHER_LOCATION_ID;
    char url[256];

    config_store_get_str(NVS_KEY_API_KEY, api_key, sizeof(api_key));
    config_store_get_str(NVS_KEY_CITY_ID, location, sizeof(location));

    snprintf(url, sizeof(url), "https://" WEATHER_API_HOST "/v7/weather/now?location=%s&lang=zh", location);
    ESP_LOGI(TAG_WEATHER, "Fetching current weather...");
    if (http_get(url, api_key) == ESP_OK) {
        if (parse_now_weather(s_http_buf) != ESP_OK) {
            ESP_LOGE(TAG_WEATHER, "failed to parse current weather body=%s",
                     s_http_len > 0 ? s_http_buf : "<empty>");
        }
    }

    snprintf(url, sizeof(url), "https://" WEATHER_API_HOST "/v7/weather/3d?location=%s&lang=zh", location);
    ESP_LOGI(TAG_WEATHER, "Fetching forecast...");
    if (http_get(url, api_key) == ESP_OK) {
        if (parse_forecast(s_http_buf) != ESP_OK) {
            ESP_LOGE(TAG_WEATHER, "failed to parse forecast body=%s",
                     s_http_len > 0 ? s_http_buf : "<empty>");
        }
    }

    if (s_data.valid) {
        ESP_LOGI(TAG_WEATHER, "Weather updated: %dC icon=%s",
                 s_data.temp_now, s_data.icon_code);
    }

    return s_data.valid ? ESP_OK : ESP_FAIL;
}

void weather_client_get_data(weather_data_t *data)
{
    if (data) {
        *data = s_data;
    }
}

void weather_task(void *arg)
{
    ESP_LOGI(TAG_WEATHER, "Weather task started");

    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    int waited_ms = 0;
    while (!weather_time_is_valid() && waited_ms < WEATHER_NTP_WAIT_MS) {
        if (waited_ms == 0) {
            ESP_LOGI(TAG_WEATHER, "Waiting for valid system time before first HTTPS request");
        }
        if ((waited_ms % 5000) == 0) {
            log_weather_clock_state("weather waiting");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        waited_ms += 1000;
    }
    if (!weather_time_is_valid()) {
        ESP_LOGW(TAG_WEATHER, "System time still invalid after wait, first weather fetch skipped");
        log_weather_clock_state("weather wait timeout");
    } else {
        log_weather_clock_state("weather time ready");
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    weather_client_fetch();
    ESP_LOGI(TAG_WEATHER, "weather task stack watermark=%u",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));

    while (1) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)WEATHER_UPDATE_INTERVAL_S * 1000));
        weather_client_fetch();
        ESP_LOGI(TAG_WEATHER, "weather task stack watermark=%u",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}
