#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_spiffs.h"
#include "config.h"
#include "content_store.h"
#include "wifi_manager.h"
#include "display_manager.h"
#include "web_server.h"

static const char *TAG = "main";

// ============================================================
// 回调：Web 界面更新了菜单内容
// ============================================================
static void on_content_updated(const menu_content_t *content) {
    display_manager_update(content);
    ESP_LOGI(TAG, "Display updated: %s", content->restaurant_name);
}

// ============================================================
// 回调：Web 界面提交了新的 WiFi 配置
// 注意：在单独任务中执行，避免阻塞 HTTP 响应
// ============================================================
static char s_pending_ssid[MAX_WIFI_SSID_LEN];
static char s_pending_pass[MAX_WIFI_PASS_LEN];

static void wifi_connect_task(void *arg) {
    ESP_LOGI(TAG, "Connecting to new WiFi: %s", s_pending_ssid);

    // 显示连接中画面
    display_manager_show_connecting(s_pending_ssid);

    bool ok = wifi_manager_connect(s_pending_ssid, s_pending_pass);
    if (ok) {
        ESP_LOGI(TAG, "WiFi connected, restarting web server...");
        web_server_stop();
        vTaskDelay(pdMS_TO_TICKS(500));
        web_server_start();

        // 恢复菜单显示
        menu_content_t content;
        content_store_load(&content);
        display_manager_update(&content);
    } else {
        ESP_LOGW(TAG, "WiFi connection failed");
        wifi_status_t status;
        wifi_manager_get_status(&status);
        display_manager_show_wifi_setup(status.ssid, status.ip_addr);
    }
    vTaskDelete(NULL);
}

static void on_wifi_config(const char *ssid, const char *password) {
    strncpy(s_pending_ssid, ssid, MAX_WIFI_SSID_LEN - 1);
    strncpy(s_pending_pass, password, MAX_WIFI_PASS_LEN - 1);
    xTaskCreate(wifi_connect_task, "wifi_connect", 4096, NULL, 3, NULL);
}

// ============================================================
// 初始化 SPIFFS（存储中文字库）
// ============================================================
static void init_spiffs(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed (%s), Chinese font unavailable",
                 esp_err_to_name(ret));
    } else {
        size_t total = 0, used = 0;
        esp_spiffs_info("storage", &total, &used);
        ESP_LOGI(TAG, "SPIFFS: %d/%d bytes used", used, total);
    }
}

// ============================================================
// 主入口
// ============================================================
void app_main(void) {
    ESP_LOGI(TAG, "=== %s %s ===", PROJECT_NAME, FIRMWARE_VERSION);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    // 1. 初始化 NVS 存储
    content_store_init();

    // 2. 初始化 SPIFFS（中文字库）
    init_spiffs();

    // 3. 初始化显示（LCD + 显示任务）
    display_manager_init();
    display_manager_show_splash();

    // 4. 加载保存的菜单内容
    menu_content_t content;
    content_store_load(&content);
    display_manager_update(&content);

    // 5. 初始化 WiFi
    wifi_status_t wifi_status;
    wifi_manager_init();
    wifi_manager_get_status(&wifi_status);

    // 6. 根据 WiFi 状态显示对应界面
    if (wifi_manager_is_ap_mode()) {
        // AP 配置模式：显示 WiFi 设置引导
        display_manager_show_wifi_setup(wifi_status.ssid, wifi_status.ip_addr);
        ESP_LOGI(TAG, "AP mode: SSID=%s, IP=%s", wifi_status.ssid, wifi_status.ip_addr);
    } else {
        // 已连接：显示菜单
        display_manager_update(&content);
        ESP_LOGI(TAG, "STA mode: IP=%s", wifi_status.ip_addr);
    }

    // 7. 启动 Web 服务器
    web_server_set_content_callback(on_content_updated);
    web_server_set_wifi_callback(on_wifi_config);
    web_server_start();

    ESP_LOGI(TAG, "System ready!");
    ESP_LOGI(TAG, "Access: http://menu-display.local or http://%s", wifi_status.ip_addr);

    // 主任务退出（显示任务和 Web 服务器在后台运行）
    vTaskDelete(NULL);
}