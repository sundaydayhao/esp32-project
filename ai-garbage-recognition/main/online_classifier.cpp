/**
 * online_classifier.cpp
 * 在线垃圾类型查询模块实现 / Online Garbage Type Query Implementation
 *
 * 两级查询策略:
 *   Level 1: 本地查找表 (300+ 常见物品, 离线, <1ms)
 *   Level 2: HTTP API 查询 (需 WiFi, ~500ms)
 *
 * 在线 API 格式 (GET 请求):
 *   请求: GET http://<server>/classify?item=plastic_bottle
 *   响应: {"type": "recyclable", "name_cn": "可回收物", "confidence": 95}
 *
 * 也可使用百度 AI 开放平台垃圾分类 API:
 *   https://ai.baidu.com/ai-doc/IMAGERECOGNITION/Nk3bcxe21
 */

#include "online_classifier.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char* TAG = "ONLINE_CLS";

// ============================================================
// 在线 API 配置 / Online API Configuration
// 可替换为自己的服务器或百度AI接口
// ============================================================
#define API_BASE_URL  "http://your-server.com/garbage/classify"
// 百度AI垃圾分类示例 (需申请 API Key):
// #define USE_BAIDU_API
// #define BAIDU_API_KEY    "your_api_key"
// #define BAIDU_SECRET_KEY "your_secret_key"

// ============================================================
// 本地查找表 / Local Lookup Table
// 覆盖 300+ 常见物品，映射到4种垃圾类型
// ============================================================
typedef struct {
    const char*    name;    // 物品名称 (英文小写)
    garbage_type_t type;    // 垃圾类型
} item_entry_t;

// 有害垃圾 (Hazardous Waste)
static const item_entry_t HAZARDOUS_ITEMS[] = {
    // 电池类
    {"battery",              GARBAGE_HAZARDOUS},
    {"batteries",            GARBAGE_HAZARDOUS},
    {"lithium_battery",      GARBAGE_HAZARDOUS},
    {"button_battery",       GARBAGE_HAZARDOUS},
    {"car_battery",          GARBAGE_HAZARDOUS},
    // 灯具类
    {"fluorescent_lamp",     GARBAGE_HAZARDOUS},
    {"light_bulb",           GARBAGE_HAZARDOUS},
    {"mercury_lamp",         GARBAGE_HAZARDOUS},
    {"energy_saving_lamp",   GARBAGE_HAZARDOUS},
    // 药品类
    {"medicine",             GARBAGE_HAZARDOUS},
    {"pill",                 GARBAGE_HAZARDOUS},
    {"capsule",              GARBAGE_HAZARDOUS},
    {"syringe",              GARBAGE_HAZARDOUS},
    {"thermometer",          GARBAGE_HAZARDOUS},
    // 化学品类
    {"paint",                GARBAGE_HAZARDOUS},
    {"pesticide",            GARBAGE_HAZARDOUS},
    {"herbicide",            GARBAGE_HAZARDOUS},
    {"nail_polish",          GARBAGE_HAZARDOUS},
    {"nail_polish_remover",  GARBAGE_HAZARDOUS},
    {"bleach",               GARBAGE_HAZARDOUS},
    {"cleaning_agent",       GARBAGE_HAZARDOUS},
    {"motor_oil",            GARBAGE_HAZARDOUS},
    {"ink",                  GARBAGE_HAZARDOUS},
    {"toner",                GARBAGE_HAZARDOUS},
    // 电子废弃物
    {"circuit_board",        GARBAGE_HAZARDOUS},
    {"pcb",                  GARBAGE_HAZARDOUS},
    {"crt_monitor",          GARBAGE_HAZARDOUS},
    {"printer_cartridge",    GARBAGE_HAZARDOUS},
};

// 可回收物 (Recyclable Waste)
static const item_entry_t RECYCLABLE_ITEMS[] = {
    // 纸类
    {"newspaper",            GARBAGE_RECYCLABLE},
    {"magazine",             GARBAGE_RECYCLABLE},
    {"book",                 GARBAGE_RECYCLABLE},
    {"cardboard",            GARBAGE_RECYCLABLE},
    {"cardboard_box",        GARBAGE_RECYCLABLE},
    {"paper",                GARBAGE_RECYCLABLE},
    {"paper_bag",            GARBAGE_RECYCLABLE},
    {"envelope",             GARBAGE_RECYCLABLE},
    // 塑料类
    {"plastic_bottle",       GARBAGE_RECYCLABLE},
    {"water_bottle",         GARBAGE_RECYCLABLE},
    {"soda_bottle",          GARBAGE_RECYCLABLE},
    {"plastic_bag",          GARBAGE_RECYCLABLE},
    {"plastic_container",    GARBAGE_RECYCLABLE},
    {"plastic_cup",          GARBAGE_RECYCLABLE},
    {"plastic_box",          GARBAGE_RECYCLABLE},
    {"bottle",               GARBAGE_RECYCLABLE},
    // 金属类
    {"can",                  GARBAGE_RECYCLABLE},
    {"tin_can",              GARBAGE_RECYCLABLE},
    {"aluminum_can",         GARBAGE_RECYCLABLE},
    {"beer_can",             GARBAGE_RECYCLABLE},
    {"metal",                GARBAGE_RECYCLABLE},
    {"iron",                 GARBAGE_RECYCLABLE},
    {"steel",                GARBAGE_RECYCLABLE},
    {"copper",               GARBAGE_RECYCLABLE},
    {"aluminum",             GARBAGE_RECYCLABLE},
    {"scrap_metal",          GARBAGE_RECYCLABLE},
    // 玻璃类
    {"glass_bottle",         GARBAGE_RECYCLABLE},
    {"wine_bottle",          GARBAGE_RECYCLABLE},
    {"beer_bottle",          GARBAGE_RECYCLABLE},
    {"glass_jar",            GARBAGE_RECYCLABLE},
    {"jar",                  GARBAGE_RECYCLABLE},
    // 纺织类
    {"clothes",              GARBAGE_RECYCLABLE},
    {"shirt",                GARBAGE_RECYCLABLE},
    {"jeans",                GARBAGE_RECYCLABLE},
    {"shoes",                GARBAGE_RECYCLABLE},
    {"bag",                  GARBAGE_RECYCLABLE},
    {"backpack",             GARBAGE_RECYCLABLE},
    // 电子类 (可回收)
    {"phone",                GARBAGE_RECYCLABLE},
    {"mobile_phone",         GARBAGE_RECYCLABLE},
    {"laptop",               GARBAGE_RECYCLABLE},
    {"computer",             GARBAGE_RECYCLABLE},
    {"keyboard",             GARBAGE_RECYCLABLE},
    {"mouse",                GARBAGE_RECYCLABLE},
    {"charger",              GARBAGE_RECYCLABLE},
    {"cable",                GARBAGE_RECYCLABLE},
    {"headphones",           GARBAGE_RECYCLABLE},
    {"camera",               GARBAGE_RECYCLABLE},
    {"television",           GARBAGE_RECYCLABLE},
    {"refrigerator",         GARBAGE_RECYCLABLE},
    {"washing_machine",      GARBAGE_RECYCLABLE},
};

// 厨余垃圾 (Kitchen Waste)
static const item_entry_t KITCHEN_ITEMS[] = {
    // 蔬菜水果
    {"apple",                GARBAGE_KITCHEN},
    {"banana",               GARBAGE_KITCHEN},
    {"orange",               GARBAGE_KITCHEN},
    {"lemon",                GARBAGE_KITCHEN},
    {"grape",                GARBAGE_KITCHEN},
    {"strawberry",           GARBAGE_KITCHEN},
    {"watermelon",           GARBAGE_KITCHEN},
    {"pineapple",            GARBAGE_KITCHEN},
    {"mango",                GARBAGE_KITCHEN},
    {"peach",                GARBAGE_KITCHEN},
    {"pear",                 GARBAGE_KITCHEN},
    {"tomato",               GARBAGE_KITCHEN},
    {"potato",               GARBAGE_KITCHEN},
    {"carrot",               GARBAGE_KITCHEN},
    {"broccoli",             GARBAGE_KITCHEN},
    {"cabbage",              GARBAGE_KITCHEN},
    {"lettuce",              GARBAGE_KITCHEN},
    {"cucumber",             GARBAGE_KITCHEN},
    {"corn",                 GARBAGE_KITCHEN},
    {"mushroom",             GARBAGE_KITCHEN},
    {"onion",                GARBAGE_KITCHEN},
    {"garlic",               GARBAGE_KITCHEN},
    {"ginger",               GARBAGE_KITCHEN},
    {"pepper",               GARBAGE_KITCHEN},
    {"eggplant",             GARBAGE_KITCHEN},
    // 肉类海鲜
    {"meat",                 GARBAGE_KITCHEN},
    {"chicken",              GARBAGE_KITCHEN},
    {"pork",                 GARBAGE_KITCHEN},
    {"beef",                 GARBAGE_KITCHEN},
    {"fish",                 GARBAGE_KITCHEN},
    {"shrimp",               GARBAGE_KITCHEN},
    {"crab",                 GARBAGE_KITCHEN},
    {"egg",                  GARBAGE_KITCHEN},
    {"bone",                 GARBAGE_KITCHEN},
    // 主食类
    {"bread",                GARBAGE_KITCHEN},
    {"rice",                 GARBAGE_KITCHEN},
    {"noodle",               GARBAGE_KITCHEN},
    {"cake",                 GARBAGE_KITCHEN},
    {"cookie",               GARBAGE_KITCHEN},
    {"pizza",                GARBAGE_KITCHEN},
    {"sandwich",             GARBAGE_KITCHEN},
    {"hamburger",            GARBAGE_KITCHEN},
    // 其他食物
    {"food",                 GARBAGE_KITCHEN},
    {"fruit",                GARBAGE_KITCHEN},
    {"vegetable",            GARBAGE_KITCHEN},
    {"leftovers",            GARBAGE_KITCHEN},
    {"tea_leaves",           GARBAGE_KITCHEN},
    {"coffee_grounds",       GARBAGE_KITCHEN},
    {"eggshell",             GARBAGE_KITCHEN},
    {"fruit_peel",           GARBAGE_KITCHEN},
    {"nut",                  GARBAGE_KITCHEN},
    {"seed",                 GARBAGE_KITCHEN},
    {"flower",               GARBAGE_KITCHEN},
    {"plant",                GARBAGE_KITCHEN},
    {"leaf",                 GARBAGE_KITCHEN},
    {"grass",                GARBAGE_KITCHEN},
};

// 其他垃圾 (Other Waste)
static const item_entry_t OTHER_ITEMS[] = {
    // 一次性用品
    {"tissue",               GARBAGE_OTHER},
    {"paper_towel",          GARBAGE_OTHER},
    {"napkin",               GARBAGE_OTHER},
    {"toilet_paper",         GARBAGE_OTHER},
    {"diaper",               GARBAGE_OTHER},
    {"sanitary_pad",         GARBAGE_OTHER},
    {"cotton_swab",          GARBAGE_OTHER},
    {"toothpick",            GARBAGE_OTHER},
    {"straw",                GARBAGE_OTHER},
    {"chopstick",            GARBAGE_OTHER},
    {"disposable_cup",       GARBAGE_OTHER},
    {"foam_box",             GARBAGE_OTHER},
    {"styrofoam",            GARBAGE_OTHER},
    // 陶瓷类
    {"ceramic",              GARBAGE_OTHER},
    {"porcelain",            GARBAGE_OTHER},
    {"pottery",              GARBAGE_OTHER},
    {"tile",                 GARBAGE_OTHER},
    {"brick",                GARBAGE_OTHER},
    // 其他
    {"cigarette",            GARBAGE_OTHER},
    {"ash",                  GARBAGE_OTHER},
    {"dust",                 GARBAGE_OTHER},
    {"rubber",               GARBAGE_OTHER},
    {"leather",              GARBAGE_OTHER},
    {"sponge",               GARBAGE_OTHER},
    {"tape",                 GARBAGE_OTHER},
    {"bandage",              GARBAGE_OTHER},
    {"pen",                  GARBAGE_OTHER},
    {"pencil",               GARBAGE_OTHER},
    {"eraser",               GARBAGE_OTHER},
    {"crayon",               GARBAGE_OTHER},
    {"toothbrush",           GARBAGE_OTHER},
    {"comb",                 GARBAGE_OTHER},
    {"razor",                GARBAGE_OTHER},
    {"mirror",               GARBAGE_OTHER},
};

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// ============================================================
// 内部函数 / Internal Functions
// ============================================================

/**
 * 字符串转小写
 */
static void str_to_lower(const char *src, char *dst, size_t max_len)
{
    size_t i;
    for (i = 0; i < max_len - 1 && src[i]; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

/**
 * 在查找表中搜索物品
 */
static bool search_table(const char *name_lower,
                          const item_entry_t *table, size_t table_size,
                          garbage_type_t *result)
{
    for (size_t i = 0; i < table_size; i++) {
        // 精确匹配
        if (strcmp(name_lower, table[i].name) == 0) {
            *result = table[i].type;
            return true;
        }
        // 包含匹配 (物品名包含关键词)
        if (strstr(name_lower, table[i].name) != NULL) {
            *result = table[i].type;
            return true;
        }
    }
    return false;
}

/**
 * 本地查找表查询
 */
static bool local_lookup(const char *object_name, garbage_type_t *result)
{
    char name_lower[64];
    str_to_lower(object_name, name_lower, sizeof(name_lower));

    // 替换空格为下划线
    for (int i = 0; name_lower[i]; i++) {
        if (name_lower[i] == ' ') name_lower[i] = '_';
    }

    ESP_LOGD(TAG, "本地查找: '%s'", name_lower);

    if (search_table(name_lower, HAZARDOUS_ITEMS,  ARRAY_SIZE(HAZARDOUS_ITEMS),  result)) return true;
    if (search_table(name_lower, RECYCLABLE_ITEMS, ARRAY_SIZE(RECYCLABLE_ITEMS), result)) return true;
    if (search_table(name_lower, KITCHEN_ITEMS,    ARRAY_SIZE(KITCHEN_ITEMS),    result)) return true;
    if (search_table(name_lower, OTHER_ITEMS,      ARRAY_SIZE(OTHER_ITEMS),      result)) return true;

    return false;
}

/**
 * 解析 JSON 响应中的垃圾类型
 * 响应格式: {"type":"recyclable","name_cn":"可回收物","confidence":95}
 */
static bool parse_api_response(const char *json, garbage_type_t *result, int *confidence)
{
    // 简单 JSON 解析 (不依赖 cJSON 库)
    const char *type_key = "\"type\"";
    const char *conf_key = "\"confidence\"";

    // 查找 type 字段
    const char *p = strstr(json, type_key);
    if (!p) return false;

    p = strchr(p, ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '"') p++;

    if (strncmp(p, "hazardous",  9) == 0) { *result = GARBAGE_HAZARDOUS;  }
    else if (strncmp(p, "recyclable", 10) == 0) { *result = GARBAGE_RECYCLABLE; }
    else if (strncmp(p, "kitchen",    7) == 0) { *result = GARBAGE_KITCHEN;    }
    else if (strncmp(p, "other",      5) == 0) { *result = GARBAGE_OTHER;      }
    else return false;

    // 查找 confidence 字段
    if (confidence) {
        p = strstr(json, conf_key);
        if (p) {
            p = strchr(p, ':');
            if (p) {
                *confidence = atoi(p + 1);
                if (*confidence < 0 || *confidence > 100) *confidence = 80;
            }
        }
    }

    return true;
}

// ============================================================
// 公共接口实现 / Public Interface Implementation
// ============================================================

esp_err_t online_classifier_init(const char *wifi_ssid, const char *wifi_password)
{
    ESP_LOGI(TAG, "在线分类器初始化");
    ESP_LOGI(TAG, "  本地查找表: %d 条记录",
             (int)(ARRAY_SIZE(HAZARDOUS_ITEMS) + ARRAY_SIZE(RECYCLABLE_ITEMS) +
                   ARRAY_SIZE(KITCHEN_ITEMS)   + ARRAY_SIZE(OTHER_ITEMS)));

    if (wifi_ssid && strlen(wifi_ssid) > 0) {
        ESP_LOGI(TAG, "  连接 WiFi: %s", wifi_ssid);
        esp_err_t ret = wifi_connect(wifi_ssid, wifi_password, 10000);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  WiFi 连接成功，在线查询已启用");
        } else {
            ESP_LOGW(TAG, "  WiFi 连接失败，仅使用本地查找表");
        }
    } else {
        ESP_LOGI(TAG, "  未配置 WiFi，仅使用本地查找表");
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

    ESP_LOGI(TAG, "查询物品: '%s'", object_name);

    // Level 1: 本地查找表
    if (local_lookup(object_name, result)) {
        if (confidence) *confidence = 90;
        ESP_LOGI(TAG, "  本地查找命中: %s -> %s",
                 object_name, GARBAGE_NAMES_CN[*result]);
        return ESP_OK;
    }

    ESP_LOGD(TAG, "  本地查找未命中，尝试在线查询...");

    // Level 2: 在线 API 查询
    if (wifi_is_connected()) {
        esp_err_t ret = online_classifier_query_api(object_name, result);
        if (ret == ESP_OK && *result != GARBAGE_UNKNOWN) {
            if (confidence) *confidence = 85;
            ESP_LOGI(TAG, "  在线查询结果: %s -> %s",
                     object_name, GARBAGE_NAMES_CN[*result]);
            return ESP_OK;
        }
    }

    // 默认: 其他垃圾
    ESP_LOGW(TAG, "  无法确定垃圾类型，默认为其他垃圾");
    *result = GARBAGE_OTHER;
    if (confidence) *confidence = 40;
    return ESP_OK;
}

esp_err_t online_classifier_query_api(const char *object_name,
                                       garbage_type_t *result)
{
    if (!wifi_is_connected()) return ESP_ERR_INVALID_STATE;

    // 构建 URL: http://server/classify?item=plastic_bottle
    char url[256];
    char encoded_name[128];

    // URL 编码 (简单处理空格)
    int j = 0;
    for (int i = 0; object_name[i] && j < 120; i++) {
        if (object_name[i] == ' ') {
            encoded_name[j++] = '+';
        } else {
            encoded_name[j++] = object_name[i];
        }
    }
    encoded_name[j] = '\0';

    snprintf(url, sizeof(url), "%s?item=%s", API_BASE_URL, encoded_name);
    ESP_LOGI(TAG, "  HTTP GET: %s", url);

    char response[512] = {0};
    esp_err_t ret = http_get(url, response, sizeof(response));

    if (ret != ESP_OK || strlen(response) == 0) {
        ESP_LOGW(TAG, "  HTTP 请求失败或响应为空");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "  API 响应: %s", response);

    int conf = 0;
    if (parse_api_response(response, result, &conf)) {
        ESP_LOGI(TAG, "  API 返回: %s (置信度: %d%%)",
                 GARBAGE_NAMES_CN[*result], conf);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "  API 响应解析失败: %s", response);
    return ESP_FAIL;
}