/**
 * online_classifier_llm.cpp
 * LLM-backed garbage classification implementation
 */

#include "online_classifier.h"
#include "camera_handler.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ONLINE_CLS";

typedef struct {
    const char *name;
    garbage_type_t type;
} item_entry_t;

static const item_entry_t FALLBACK_ITEMS[] = {
    {"battery", GARBAGE_HAZARDOUS},
    {"expired medicine", GARBAGE_HAZARDOUS},
    {"plastic bottle", GARBAGE_RECYCLABLE},
    {"paper bag", GARBAGE_RECYCLABLE},
    {"apple", GARBAGE_KITCHEN},
    {"banana", GARBAGE_KITCHEN},
    {"tissue", GARBAGE_OTHER},
    {"ceramic", GARBAGE_OTHER},
};

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static void str_to_lower(const char *src, char *dst, size_t max_len)
{
    size_t i = 0;
    for (; i + 1 < max_len && src[i] != '\0'; ++i) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

static bool local_lookup(const char *object_name, garbage_type_t *result)
{
    char lower[96];
    str_to_lower(object_name, lower, sizeof(lower));

    for (size_t i = 0; i < ARRAY_SIZE(FALLBACK_ITEMS); ++i) {
        char fallback[64];
        str_to_lower(FALLBACK_ITEMS[i].name, fallback, sizeof(fallback));
        if (strcmp(lower, fallback) == 0 || strstr(lower, fallback) != NULL) {
            *result = FALLBACK_ITEMS[i].type;
            return true;
        }
    }

    return false;
}

static bool extract_json_string(const char *json, const char *key,
                                char *out, size_t out_len)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);
    if (!p) return false;

    p = strchr(p, ':');
    if (!p) return false;
    p++;

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return false;
    p++;

    size_t j = 0;
    while (*p && *p != '"' && j + 1 < out_len) {
        if (*p == '\\' && p[1] != '\0') {
            p++;
        }
        out[j++] = *p++;
    }
    out[j] = '\0';
    return j > 0;
}

static bool extract_json_int(const char *json, const char *key, int *value)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);
    if (!p) return false;

    p = strchr(p, ':');
    if (!p) return false;
    p++;

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    *value = atoi(p);
    return true;
}

static bool map_type_string(const char *type_str, garbage_type_t *result)
{
    char lower[64];
    str_to_lower(type_str, lower, sizeof(lower));

    if (strstr(lower, "hazardous") != NULL || strstr(lower, "harmful") != NULL) {
        *result = GARBAGE_HAZARDOUS;
        return true;
    }
    if (strstr(lower, "recyclable") != NULL || strstr(lower, "recycle") != NULL) {
        *result = GARBAGE_RECYCLABLE;
        return true;
    }
    if (strstr(lower, "kitchen") != NULL || strstr(lower, "food") != NULL || strstr(lower, "wet") != NULL) {
        *result = GARBAGE_KITCHEN;
        return true;
    }
    if (strstr(lower, "other") != NULL || strstr(lower, "residual") != NULL || strstr(lower, "dry") != NULL) {
        *result = GARBAGE_OTHER;
        return true;
    }
    return false;
}

static bool parse_api_response(const char *json, garbage_type_t *result, int *confidence)
{
    char type_buf[64] = {0};
    char category_buf[64] = {0};
    char name_cn_buf[64] = {0};

    if (extract_json_string(json, "type", type_buf, sizeof(type_buf)) &&
        map_type_string(type_buf, result)) {
        goto confidence_parse;
    }

    if (extract_json_string(json, "category", category_buf, sizeof(category_buf)) &&
        map_type_string(category_buf, result)) {
        goto confidence_parse;
    }

    if (extract_json_string(json, "name_cn", name_cn_buf, sizeof(name_cn_buf)) &&
        map_type_string(name_cn_buf, result)) {
        goto confidence_parse;
    }

    return false;

confidence_parse:
    if (confidence) {
        int conf = 80;
        if (extract_json_int(json, "confidence", &conf)) {
            if (conf < 0 || conf > 100) conf = 80;
        }
        *confidence = conf;
    }

    return true;
}

esp_err_t online_classifier_init(const char *wifi_ssid, const char *wifi_password)
{
    ESP_LOGI(TAG, "Online classifier init");
    ESP_LOGI(TAG, "  endpoint: %s", ONLINE_API_URL);

    if (wifi_ssid && strlen(wifi_ssid) > 0) {
        ESP_LOGI(TAG, "  connecting WiFi: %s", wifi_ssid);
        esp_err_t ret = wifi_connect(wifi_ssid, wifi_password, WIFI_TIMEOUT_MS);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  WiFi connected, image upload enabled");
        } else {
            ESP_LOGW(TAG, "  WiFi connect failed");
        }
    } else {
        ESP_LOGI(TAG, "  WiFi not configured");
    }

    return ESP_OK;
}

esp_err_t online_classifier_query(const char *object_name,
                                  garbage_type_t *result,
                                  int *confidence)
{
    if (!object_name || !result) return ESP_ERR_INVALID_ARG;

    *result = GARBAGE_UNKNOWN;
    if (confidence) *confidence = 0;

    ESP_LOGI(TAG, "Query object label: '%s'", object_name);

#if ONLINE_CLASSIFIER_USE_LOCAL_LOOKUP
    if (local_lookup(object_name, result)) {
        if (confidence) *confidence = 70;
        ESP_LOGI(TAG, "  local fallback hit: %s -> %s",
                 object_name, GARBAGE_NAMES_CN[*result]);
        return ESP_OK;
    }
#endif

    ESP_LOGW(TAG, "  object-label path unavailable, defaulting to other");
    *result = GARBAGE_OTHER;
    if (confidence) *confidence = 40;
    return ESP_OK;
}

esp_err_t online_classifier_query_image(garbage_type_t *result, int *confidence)
{
    if (!result) return ESP_ERR_INVALID_ARG;
    if (!wifi_is_connected()) return ESP_ERR_INVALID_STATE;

    *result = GARBAGE_UNKNOWN;
    if (confidence) *confidence = 0;

    uint8_t *jpeg_buf = NULL;
    size_t jpeg_len = 0;
    esp_err_t ret = camera_get_jpeg_buffer(&jpeg_buf, &jpeg_len);
    if (ret != ESP_OK || !jpeg_buf || jpeg_len == 0) {
        ESP_LOGW(TAG, "  JPEG frame unavailable");
        return ESP_FAIL;
    }

    char url[320];
    snprintf(url, sizeof(url), "%s?locale=%s&standard=%s&prompt_version=%s",
             ONLINE_API_URL,
             ONLINE_API_LOCALE,
             ONLINE_API_STANDARD,
             ONLINE_API_PROMPT_VERSION);

    char response[512] = {0};
    ESP_LOGI(TAG, "  HTTP POST image: %s (%d bytes)", url, (int)jpeg_len);

    ret = http_post(url,
                    jpeg_buf,
                    jpeg_len,
                    "image/jpeg",
                    ONLINE_API_AUTH_TOKEN,
                    response,
                    sizeof(response));
    if (ret != ESP_OK || response[0] == '\0') {
        ESP_LOGW(TAG, "  image upload failed or empty response");
        return ESP_FAIL;
    }

    int parsed_confidence = 0;
    if (!parse_api_response(response, result, &parsed_confidence)) {
        ESP_LOGW(TAG, "  response parse failed: %s", response);
        return ESP_FAIL;
    }

    if (confidence) *confidence = parsed_confidence;
    ESP_LOGI(TAG, "  parsed result: %s (%d%%)", GARBAGE_NAMES_CN[*result], parsed_confidence);
    return ESP_OK;
}

esp_err_t online_classifier_query_api(const char *object_name,
                                      garbage_type_t *result)
{
    int confidence = 0;
    esp_err_t ret = online_classifier_query_image(result, &confidence);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "  image result confidence: %d%%", confidence);
        return ESP_OK;
    }

#if ONLINE_CLASSIFIER_USE_LOCAL_LOOKUP
    if (object_name && result && local_lookup(object_name, result)) {
        return ESP_OK;
    }
#else
    (void)object_name;
#endif

    return ret;
}
