#include "content_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "content_store";

// ============================================================
// 默认内容
// ============================================================
static void fill_default_content(menu_content_t *content) {
    memset(content, 0, sizeof(menu_content_t));
    strncpy(content->restaurant_name, "我的餐厅", MAX_RESTAURANT_NAME_LEN - 1);
    content->theme = 0;
    strncpy(content->promo_text, "欢迎光临！今日特价，扫码点餐享优惠", MAX_PROMO_TEXT_LEN - 1);
    content->item_count = 8;

    const char *names[] = {"红烧肉饭", "鱼香肉丝饭", "番茄鸡蛋饭", "宫保鸡丁饭",
                           "麻婆豆腐饭", "回锅肉饭",   "蛋炒饭",     "扬州炒饭"};
    const char *prices[] = {"18", "16", "12", "18", "14", "20", "10", "15"};

    for (int i = 0; i < 8; i++) {
        strncpy(content->items[i].name, names[i], MAX_ITEM_NAME_LEN - 1);
        strncpy(content->items[i].price, prices[i], MAX_ITEM_PRICE_LEN - 1);
    }
}

// ============================================================
// 初始化
// ============================================================
bool content_store_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "NVS initialized");
    return true;
}

// ============================================================
// 菜单内容存储
// ============================================================
void content_store_load(menu_content_t *content) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved content yet, using defaults");
        fill_default_content(content);
        return;
    }

    // 读取 JSON 字符串
    size_t required_size = 0;
    err = nvs_get_str(handle, NVS_KEY_CONTENT, NULL, &required_size);
    if (err != ESP_OK || required_size == 0) {
        nvs_close(handle);
        fill_default_content(content);
        return;
    }

    char *json_buf = malloc(required_size);
    if (!json_buf) {
        nvs_close(handle);
        fill_default_content(content);
        return;
    }

    err = nvs_get_str(handle, NVS_KEY_CONTENT, json_buf, &required_size);
    nvs_close(handle);

    if (err == ESP_OK) {
        if (!content_from_json(json_buf, content)) {
            ESP_LOGW(TAG, "JSON parse failed, using defaults");
            fill_default_content(content);
        } else {
            ESP_LOGI(TAG, "Content loaded: %s (%d items)", content->restaurant_name, content->item_count);
        }
    } else {
        fill_default_content(content);
    }
    free(json_buf);
}

bool content_store_save(const menu_content_t *content) {
    // 序列化为 JSON
    char *json_buf = malloc(4096);
    if (!json_buf) return false;

    int len = content_to_json(content, json_buf, 4096);
    if (len <= 0) {
        free(json_buf);
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        free(json_buf);
        return false;
    }

    err = nvs_set_str(handle, NVS_KEY_CONTENT, json_buf);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    free(json_buf);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Content saved: %s", content->restaurant_name);
        return true;
    }
    ESP_LOGE(TAG, "Save failed: %s", esp_err_to_name(err));
    return false;
}

// ============================================================
// WiFi 配置存储
// ============================================================
void content_store_load_wifi(wifi_config_store_t *wifi_cfg) {
    memset(wifi_cfg, 0, sizeof(wifi_config_store_t));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        wifi_cfg->configured = false;
        return;
    }

    size_t ssid_size = MAX_WIFI_SSID_LEN;
    size_t pass_size = MAX_WIFI_PASS_LEN;

    bool ok = (nvs_get_str(handle, NVS_KEY_WIFI_SSID, wifi_cfg->ssid, &ssid_size) == ESP_OK) &&
              (nvs_get_str(handle, NVS_KEY_WIFI_PASS, wifi_cfg->password, &pass_size) == ESP_OK);

    nvs_close(handle);
    wifi_cfg->configured = ok && (strlen(wifi_cfg->ssid) > 0);

    if (wifi_cfg->configured) {
        ESP_LOGI(TAG, "WiFi config loaded: SSID=%s", wifi_cfg->ssid);
    }
}

bool content_store_save_wifi(const wifi_config_store_t *wifi_cfg) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;

    err = nvs_set_str(handle, NVS_KEY_WIFI_SSID, wifi_cfg->ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, NVS_KEY_WIFI_PASS, wifi_cfg->password);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi config saved: SSID=%s", wifi_cfg->ssid);
        return true;
    }
    return false;
}

void content_store_erase_all(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "All data erased");
    }
}

// ============================================================
// JSON 序列化 / 反序列化
// ============================================================
int content_to_json(const menu_content_t *content, char *buf, size_t buf_size) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;

    cJSON_AddStringToObject(root, "restaurant_name", content->restaurant_name);
    cJSON_AddNumberToObject(root, "theme", content->theme);
    cJSON_AddStringToObject(root, "promo_text", content->promo_text);

    cJSON *items = cJSON_CreateArray();
    for (int i = 0; i < content->item_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", content->items[i].name);
        cJSON_AddStringToObject(item, "price", content->items[i].price);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "items", items);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) return -1;

    int len = strlen(json_str);
    if ((size_t)len >= buf_size) {
        free(json_str);
        return -1;
    }
    memcpy(buf, json_str, len + 1);
    free(json_str);
    return len;
}

bool content_from_json(const char *json, menu_content_t *content) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    memset(content, 0, sizeof(menu_content_t));

    cJSON *name = cJSON_GetObjectItem(root, "restaurant_name");
    if (cJSON_IsString(name)) {
        strncpy(content->restaurant_name, name->valuestring, MAX_RESTAURANT_NAME_LEN - 1);
    }

    cJSON *theme = cJSON_GetObjectItem(root, "theme");
    if (cJSON_IsNumber(theme)) {
        content->theme = (uint8_t)theme->valueint;
        if (content->theme > 3) content->theme = 0;
    }

    cJSON *promo = cJSON_GetObjectItem(root, "promo_text");
    if (cJSON_IsString(promo)) {
        strncpy(content->promo_text, promo->valuestring, MAX_PROMO_TEXT_LEN - 1);
    }

    cJSON *items = cJSON_GetObjectItem(root, "items");
    if (cJSON_IsArray(items)) {
        int count = cJSON_GetArraySize(items);
        if (count > MAX_MENU_ITEMS) count = MAX_MENU_ITEMS;
        content->item_count = count;

        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(items, i);
            cJSON *iname = cJSON_GetObjectItem(item, "name");
            cJSON *iprice = cJSON_GetObjectItem(item, "price");
            if (cJSON_IsString(iname)) {
                strncpy(content->items[i].name, iname->valuestring, MAX_ITEM_NAME_LEN - 1);
            }
            if (cJSON_IsString(iprice)) {
                strncpy(content->items[i].price, iprice->valuestring, MAX_ITEM_PRICE_LEN - 1);
            }
        }
    }

    cJSON_Delete(root);
    return true;
}
