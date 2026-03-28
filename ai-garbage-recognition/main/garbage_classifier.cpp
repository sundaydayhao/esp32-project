/**
 * garbage_classifier.cpp
 * Garbage classifier implementation
 */

#include "garbage_classifier.h"
#include "camera_handler.h"
#include "online_classifier.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "CLASSIFIER";

typedef struct {
    float h_min;
    float h_max;
    float s_min;
    float v_min;
} hsv_range_t;

static const hsv_range_t RED_RANGE_1 = { 0.0f,   15.0f,  0.40f, 0.25f };
static const hsv_range_t RED_RANGE_2 = { 345.0f, 360.0f, 0.40f, 0.25f };
static const hsv_range_t BLUE_RANGE  = { 195.0f, 265.0f, 0.30f, 0.20f };
static const hsv_range_t GREEN_RANGE = { 75.0f,  165.0f, 0.30f, 0.20f };

#define GRAY_MAX_SATURATION  0.20f
#define GRAY_MIN_VALUE       0.10f
#define MIN_COLOR_RATIO      0.12f

static void rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b,
                       float *h, float *s, float *v)
{
    const float rf = r / 255.0f;
    const float gf = g / 255.0f;
    const float bf = b / 255.0f;

    const float max_c = rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf);
    const float min_c = rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf);
    const float delta = max_c - min_c;

    *v = max_c;

    if (max_c < 1e-5f) {
        *s = 0.0f;
        *h = 0.0f;
        return;
    }

    *s = delta / max_c;
    if (delta < 1e-5f) {
        *h = 0.0f;
        return;
    }

    float hue = 0.0f;
    if (max_c == rf) {
        hue = 60.0f * fmodf((gf - bf) / delta, 6.0f);
    } else if (max_c == gf) {
        hue = 60.0f * ((bf - rf) / delta + 2.0f);
    } else {
        hue = 60.0f * ((rf - gf) / delta + 4.0f);
    }

    if (hue < 0.0f) hue += 360.0f;
    *h = hue;
}

static inline bool in_hsv_range(float h, float s, float v, const hsv_range_t *range)
{
    return h >= range->h_min && h <= range->h_max &&
           s >= range->s_min && v >= range->v_min;
}

typedef struct {
    int red_pixels;
    int blue_pixels;
    int green_pixels;
    int gray_pixels;
    int total_pixels;
} color_stats_t;

static void analyze_roi_colors(const uint8_t *rgb_buf, color_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));

    for (int y = ROI_Y_START; y < ROI_Y_START + ROI_HEIGHT; y++) {
        for (int x = ROI_X_START; x < ROI_X_START + ROI_WIDTH; x++) {
            const int idx = (y * IMG_WIDTH + x) * 3;
            const uint8_t r = rgb_buf[idx + 0];
            const uint8_t g = rgb_buf[idx + 1];
            const uint8_t b = rgb_buf[idx + 2];

            float h, s, v;
            rgb_to_hsv(r, g, b, &h, &s, &v);
            if (v < 0.08f) continue;

            stats->total_pixels++;

            if (in_hsv_range(h, s, v, &RED_RANGE_1) ||
                in_hsv_range(h, s, v, &RED_RANGE_2)) {
                stats->red_pixels++;
            } else if (in_hsv_range(h, s, v, &BLUE_RANGE)) {
                stats->blue_pixels++;
            } else if (in_hsv_range(h, s, v, &GREEN_RANGE)) {
                stats->green_pixels++;
            } else if (s < GRAY_MAX_SATURATION && v >= GRAY_MIN_VALUE) {
                stats->gray_pixels++;
            }
        }
    }
}

static esp_err_t run_color_card_mode(classification_result_t *result)
{
    uint8_t *buf = camera_get_rgb_buffer();
    if (!buf) {
        ESP_LOGE(TAG, "RGB buffer is null");
        return ESP_ERR_INVALID_STATE;
    }

    camera_get_roi_avg_color(&result->avg_r, &result->avg_g, &result->avg_b);
    rgb_to_hsv(result->avg_r, result->avg_g, result->avg_b,
               &result->hue, &result->saturation, &result->value);

    color_stats_t stats;
    analyze_roi_colors(buf, &stats);
    if (stats.total_pixels == 0) {
        ESP_LOGW(TAG, "No valid pixels in ROI");
        return ESP_OK;
    }

    const float red_r   = (float)stats.red_pixels / stats.total_pixels;
    const float blue_r  = (float)stats.blue_pixels / stats.total_pixels;
    const float green_r = (float)stats.green_pixels / stats.total_pixels;
    const float gray_r  = (float)stats.gray_pixels / stats.total_pixels;

    float max_r = red_r;
    if (blue_r > max_r) max_r = blue_r;
    if (green_r > max_r) max_r = green_r;
    if (gray_r > max_r) max_r = gray_r;

    if (max_r < MIN_COLOR_RATIO) {
        ESP_LOGW(TAG, "Color signature too weak: %.2f", max_r);
        return ESP_OK;
    }

    if (max_r == red_r) {
        result->type = GARBAGE_HAZARDOUS;
        result->confidence = (int)(red_r * 100);
    } else if (max_r == blue_r) {
        result->type = GARBAGE_RECYCLABLE;
        result->confidence = (int)(blue_r * 100);
    } else if (max_r == green_r) {
        result->type = GARBAGE_KITCHEN;
        result->confidence = (int)(green_r * 100);
    } else {
        result->type = GARBAGE_OTHER;
        result->confidence = (int)(gray_r * 100);
    }

    result->confidence += (int)(result->saturation * 25.0f);
    if (result->confidence > 100) result->confidence = 100;
    return ESP_OK;
}

#if (RECOGNITION_MODE == MODE_TFLITE_LOOKUP) || (RECOGNITION_MODE == MODE_TFLITE_ONLINE)
static const char *DEMO_OBJECTS[] = {
    "plastic_bottle",
    "banana",
    "battery",
    "tissue",
};

static int s_demo_idx = 0;

static const char *mock_tflite_inference(void)
{
    const char *obj = DEMO_OBJECTS[s_demo_idx % 4];
    s_demo_idx++;
    ESP_LOGW(TAG, "[demo] mock object recognition: %s", obj);
    return obj;
}

static esp_err_t run_tflite_mode(classification_result_t *result)
{
    uint8_t *buf = camera_get_rgb_buffer();
    if (!buf) {
        ESP_LOGE(TAG, "RGB buffer is null");
        return ESP_ERR_INVALID_STATE;
    }

    int confidence = 0;
    esp_err_t ret;

#if RECOGNITION_MODE == MODE_TFLITE_ONLINE
    ESP_LOGI(TAG, "Uploading image to online classifier");
    ret = online_classifier_query_image(&result->type, &confidence);
#else
    const char *object_name = mock_tflite_inference();
    ESP_LOGI(TAG, "Mock object label: '%s'", object_name);
    ret = online_classifier_query(object_name, &result->type, &confidence);
#endif

    if (ret != ESP_OK) return ret;

    result->confidence = confidence;
    camera_get_roi_avg_color(&result->avg_r, &result->avg_g, &result->avg_b);
    rgb_to_hsv(result->avg_r, result->avg_g, result->avg_b,
               &result->hue, &result->saturation, &result->value);
    return ESP_OK;
}
#endif

esp_err_t classifier_init(void)
{
#if RECOGNITION_MODE == MODE_COLOR_CARD
    ESP_LOGI(TAG, "Recognition mode: color card");
#elif RECOGNITION_MODE == MODE_TFLITE_LOOKUP
    ESP_LOGI(TAG, "Recognition mode: object label + local lookup");
    online_classifier_init(NULL, NULL);
#elif RECOGNITION_MODE == MODE_TFLITE_ONLINE
    ESP_LOGI(TAG, "Recognition mode: image upload + online LLM");
    ESP_LOGI(TAG, "  WiFi: %s", WIFI_SSID);
    online_classifier_init(WIFI_SSID, WIFI_PASSWORD);
#endif
    return ESP_OK;
}

esp_err_t classifier_run(classification_result_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;

    memset(result, 0, sizeof(*result));
    result->type = GARBAGE_UNKNOWN;
    result->confidence = 0;

    esp_err_t ret;

#if RECOGNITION_MODE == MODE_COLOR_CARD
    ret = run_color_card_mode(result);
#elif (RECOGNITION_MODE == MODE_TFLITE_LOOKUP) || (RECOGNITION_MODE == MODE_TFLITE_ONLINE)
    ret = run_tflite_mode(result);
#else
    ret = run_color_card_mode(result);
#endif

    if (ret == ESP_OK && result->type != GARBAGE_UNKNOWN) {
        ESP_LOGI(TAG, "Recognition result: %s (%d%%)",
                 classifier_get_name_cn(result->type),
                 result->confidence);
    }

    return ret;
}

const char *classifier_get_name_cn(garbage_type_t type)
{
    if (type < GARBAGE_TYPE_COUNT) return GARBAGE_NAMES_CN[type];
    return GARBAGE_NAMES_CN[GARBAGE_UNKNOWN];
}

const char *classifier_get_name_en(garbage_type_t type)
{
    if (type < GARBAGE_TYPE_COUNT) return GARBAGE_NAMES_EN[type];
    return GARBAGE_NAMES_EN[GARBAGE_UNKNOWN];
}
