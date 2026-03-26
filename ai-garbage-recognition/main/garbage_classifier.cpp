/**
 * garbage_classifier.cpp
 * 垃圾分类识别模块实现 / Garbage Classifier Implementation
 *
 * 支持三种识别模式 (在 app_config.h 中配置 RECOGNITION_MODE):
 *
 *   MODE_COLOR_CARD    - 颜色卡片识别 (默认，离线)
 *     红色卡片 -> 有害垃圾
 *     蓝色卡片 -> 可回收物
 *     绿色卡片 -> 厨余垃圾
 *     灰色卡片 -> 其他垃圾
 *
 *   MODE_TFLITE_LOOKUP - MobileNetV2 识别物体 + 本地查找表 (离线)
 *     摄像头 -> MobileNetV2 -> 物体名称 -> 本地300+条目查找表 -> 垃圾类型
 *
 *   MODE_TFLITE_ONLINE - MobileNetV2 识别物体 + WiFi 在线查询
 *     摄像头 -> MobileNetV2 -> 物体名称 -> HTTP API -> 垃圾类型
 */

#include "garbage_classifier.h"
#include "camera_handler.h"
#include "online_classifier.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "CLASSIFIER";

// ============================================================
// HSV 颜色范围定义 / HSV Color Range Definitions
// ============================================================
typedef struct {
    float h_min, h_max;
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

// ============================================================
// 内部函数 / Internal Functions
// ============================================================

static void rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b,
                       float *h, float *s, float *v)
{
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;

    float max_c = rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf);
    float min_c = rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf);
    float delta = max_c - min_c;

    *v = max_c;

    if (max_c < 1e-5f) { *s = 0.0f; *h = 0.0f; return; }
    *s = delta / max_c;
    if (delta < 1e-5f) { *h = 0.0f; return; }

    float hue;
    if (max_c == rf)      hue = 60.0f * fmodf((gf - bf) / delta, 6.0f);
    else if (max_c == gf) hue = 60.0f * ((bf - rf) / delta + 2.0f);
    else                  hue = 60.0f * ((rf - gf) / delta + 4.0f);

    if (hue < 0.0f) hue += 360.0f;
    *h = hue;
}

static inline bool in_hsv_range(float h, float s, float v,
                                 const hsv_range_t *range)
{
    return (h >= range->h_min && h <= range->h_max &&
            s >= range->s_min && v >= range->v_min);
}

typedef struct {
    int red_pixels, blue_pixels, green_pixels, gray_pixels, total_pixels;
} color_stats_t;

static void analyze_roi_colors(const uint8_t *rgb_buf, color_stats_t *stats)
{
    memset(stats, 0, sizeof(color_stats_t));

    for (int y = ROI_Y_START; y < ROI_Y_START + ROI_HEIGHT; y++) {
        for (int x = ROI_X_START; x < ROI_X_START + ROI_WIDTH; x++) {
            int idx = (y * IMG_WIDTH + x) * 3;
            uint8_t r = rgb_buf[idx + 0];
            uint8_t g = rgb_buf[idx + 1];
            uint8_t b = rgb_buf[idx + 2];

            float h, s, v;
            rgb_to_hsv(r, g, b, &h, &s, &v);
            if (v < 0.08f) continue;

            stats->total_pixels++;

            if      (in_hsv_range(h, s, v, &RED_RANGE_1) ||
                     in_hsv_range(h, s, v, &RED_RANGE_2))  stats->red_pixels++;
            else if (in_hsv_range(h, s, v, &BLUE_RANGE))   stats->blue_pixels++;
            else if (in_hsv_range(h, s, v, &GREEN_RANGE))  stats->green_pixels++;
            else if (s < GRAY_MAX_SATURATION && v >= GRAY_MIN_VALUE) stats->gray_pixels++;
        }
    }
}

// ============================================================
// 颜色卡片识别 / Color Card Recognition
// ============================================================
static esp_err_t run_color_card_mode(classification_result_t *result)
{
    uint8_t *buf = camera_get_rgb_buffer();
    if (!buf) { ESP_LOGE(TAG, "RGB 缓冲区为空"); return ESP_ERR_INVALID_STATE; }

    camera_get_roi_avg_color(&result->avg_r, &result->avg_g, &result->avg_b);
    rgb_to_hsv(result->avg_r, result->avg_g, result->avg_b,
               &result->hue, &result->saturation, &result->value);

    ESP_LOGI(TAG, "ROI 颜色: R=%d G=%d B=%d  HSV: H=%.1f S=%.2f V=%.2f",
             result->avg_r, result->avg_g, result->avg_b,
             result->hue, result->saturation, result->value);

    color_stats_t stats;
    analyze_roi_colors(buf, &stats);

    if (stats.total_pixels == 0) { ESP_LOGW(TAG, "没有有效像素"); return ESP_OK; }

    ESP_LOGI(TAG, "颜色统计: 红=%d 蓝=%d 绿=%d 灰=%d 总=%d",
             stats.red_pixels, stats.blue_pixels,
             stats.green_pixels, stats.gray_pixels, stats.total_pixels);

    float red_r   = (float)stats.red_pixels   / stats.total_pixels;
    float blue_r  = (float)stats.blue_pixels  / stats.total_pixels;
    float green_r = (float)stats.green_pixels / stats.total_pixels;
    float gray_r  = (float)stats.gray_pixels  / stats.total_pixels;

    float max_r = red_r;
    if (blue_r  > max_r) max_r = blue_r;
    if (green_r > max_r) max_r = green_r;
    if (gray_r  > max_r) max_r = gray_r;

    if (max_r < MIN_COLOR_RATIO) {
        ESP_LOGW(TAG, "颜色特征不明显 (max=%.2f)，无法分类", max_r);
        return ESP_OK;
    }

    if      (max_r == red_r)   { result->type = GARBAGE_HAZARDOUS;  result->confidence = (int)(red_r   * 100); }
    else if (max_r == blue_r)  { result->type = GARBAGE_RECYCLABLE; result->confidence = (int)(blue_r  * 100); }
    else if (max_r == green_r) { result->type = GARBAGE_KITCHEN;    result->confidence = (int)(green_r * 100); }
    else                       { result->type = GARBAGE_OTHER;      result->confidence = (int)(gray_r  * 100); }

    result->confidence += (int)(result->saturation * 25.0f);
    if (result->confidence > 100) result->confidence = 100;

    return ESP_OK;
}

// ============================================================
// TFLite + 查找表/在线查询模式 / TFLite + Lookup/Online Mode
// ============================================================
#if (RECOGNITION_MODE == MODE_TFLITE_LOOKUP) || (RECOGNITION_MODE == MODE_TFLITE_ONLINE)

/**
 * 模拟 MobileNetV2 推理 (占位符)
 *
 * 实际使用时，需要:
 *   1. 运行 model/train_model.py 训练模型
 *   2. 将生成的 model_data.h 复制到 main/ 目录
 *   3. 使用 TensorFlow Lite for Microcontrollers 进行推理
 *   4. 将推理结果 (ImageNet 类别名) 传入 online_classifier_query()
 *
 * ImageNet 1000类标签参考:
 *   https://gist.github.com/yrevar/942d3a0ac09ec9e5eb3a
 */
static const char* DEMO_OBJECTS[] = {
    "plastic_bottle",  // 演示: 塑料瓶 -> 可回收物
    "banana",          // 演示: 香蕉 -> 厨余垃圾
    "battery",         // 演示: 电池 -> 有害垃圾
    "tissue",          // 演示: 纸巾 -> 其他垃圾
};
static int s_demo_idx = 0;

static const char* mock_tflite_inference(void)
{
    // TODO: 替换为真实 TFLite 推理代码
    // 真实代码示例:
    //   uint8_t *img = camera_get_rgb_buffer();
    //   tflite_run_inference(img, IMG_WIDTH, IMG_HEIGHT);
    //   return tflite_get_top1_label();  // 返回 ImageNet 类别名

    const char* obj = DEMO_OBJECTS[s_demo_idx % 4];
    s_demo_idx++;
    ESP_LOGW(TAG, "[演示模式] 模拟识别物体: %s (请替换为真实TFLite推理)", obj);
    return obj;
}

static esp_err_t run_tflite_mode(classification_result_t *result)
{
    // 步骤1: 获取图像
    uint8_t *buf = camera_get_rgb_buffer();
    if (!buf) { ESP_LOGE(TAG, "RGB 缓冲区为空"); return ESP_ERR_INVALID_STATE; }

    // 步骤2: MobileNetV2 推理 → 物体名称
    const char *object_name = mock_tflite_inference();
    ESP_LOGI(TAG, "MobileNetV2 识别物体: '%s'", object_name);

    // 步骤3: 查询垃圾类型 (本地查找表 + 可选在线API)
    int confidence = 0;
    esp_err_t ret = online_classifier_query(object_name, &result->type, &confidence);
    if (ret != ESP_OK) return ret;

    result->confidence = confidence;

    // 填充颜色信息 (用于日志显示)
    camera_get_roi_avg_color(&result->avg_r, &result->avg_g, &result->avg_b);
    rgb_to_hsv(result->avg_r, result->avg_g, result->avg_b,
               &result->hue, &result->saturation, &result->value);

    return ESP_OK;
}
#endif  // MODE_TFLITE_LOOKUP || MODE_TFLITE_ONLINE

// ============================================================
// 公共接口实现 / Public Interface Implementation
// ============================================================

esp_err_t classifier_init(void)
{
#if RECOGNITION_MODE == MODE_COLOR_CARD
    ESP_LOGI(TAG, "识别模式: 颜色卡片识别 (离线)");
    ESP_LOGI(TAG, "  红色卡片 -> 有害垃圾  (H: 0-15 或 345-360)");
    ESP_LOGI(TAG, "  蓝色卡片 -> 可回收物  (H: 195-265)");
    ESP_LOGI(TAG, "  绿色卡片 -> 厨余垃圾  (H: 75-165)");
    ESP_LOGI(TAG, "  灰色卡片 -> 其他垃圾  (S < 0.20)");

#elif RECOGNITION_MODE == MODE_TFLITE_LOOKUP
    ESP_LOGI(TAG, "识别模式: MobileNetV2 物体识别 + 本地查找表 (离线)");
    ESP_LOGI(TAG, "  流程: 摄像头 -> MobileNetV2 -> 物体名称 -> 本地查找表 -> 垃圾类型");
    online_classifier_init(NULL, NULL);

#elif RECOGNITION_MODE == MODE_TFLITE_ONLINE
    ESP_LOGI(TAG, "识别模式: MobileNetV2 物体识别 + WiFi 在线查询");
    ESP_LOGI(TAG, "  流程: 摄像头 -> MobileNetV2 -> 物体名称 -> HTTP API -> 垃圾类型");
    ESP_LOGI(TAG, "  WiFi: %s", WIFI_SSID);
    online_classifier_init(WIFI_SSID, WIFI_PASSWORD);
#endif

    return ESP_OK;
}

esp_err_t classifier_run(classification_result_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;

    memset(result, 0, sizeof(classification_result_t));
    result->type       = GARBAGE_UNKNOWN;
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
        ESP_LOGI(TAG, "识别结果: %s (置信度: %d%%)",
                 classifier_get_name_cn(result->type), result->confidence);
    }

    return ret;
}

const char* classifier_get_name_cn(garbage_type_t type)
{
    if (type < GARBAGE_TYPE_COUNT) return GARBAGE_NAMES_CN[type];
    return GARBAGE_NAMES_CN[GARBAGE_UNKNOWN];
}

const char* classifier_get_name_en(garbage_type_t type)
{
    if (type < GARBAGE_TYPE_COUNT) return GARBAGE_NAMES_EN[type];
    return GARBAGE_NAMES_EN[GARBAGE_UNKNOWN];
}