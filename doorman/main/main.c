/**
 * @file main.c
 * @brief ESP32-S3 门卫系统主程序
 *
 * 功能：
 *  - 连接家庭 WiFi
 *  - 通过 PIR 传感器和摄像头帧差法检测门口活动
 *  - 检测到活动时自动拍照
 *  - 通过内置 HTTP 服务器提供图像访问
 *  - 通过 Server酱 推送微信通知（含图像链接）
 *  - 检测快递包裹是否留在门口
 *
 * 硬件要求：
 *  - ESP32-S3 开发板（带 PSRAM）
 *  - OV2640 摄像头模块
 *  - PIR 人体红外传感器（可选，提高检测精度）
 *  - 状态 LED（可选）
 *
 * 使用方法：
 *  1. 运行 `idf.py menuconfig` 配置 WiFi 和 Server酱 SendKey
 *  2. 运行 `idf.py build flash monitor` 编译烧录
 *  3. 设备上线后，通过微信接收通知，点击链接查看图像
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "camera_manager.h"
#include "motion_detector.h"
#include "image_server.h"
#include "notification.h"

static const char *TAG = "doorman_main";

/* 系统状态 */
typedef enum {
    SYS_STATE_INIT,         /* 初始化中 */
    SYS_STATE_CONNECTING,   /* 连接 WiFi */
    SYS_STATE_READY,        /* 就绪，监控中 */
    SYS_STATE_CAPTURING,    /* 正在拍照 */
    SYS_STATE_NOTIFYING,    /* 正在发送通知 */
    SYS_STATE_ERROR,        /* 错误状态 */
} system_state_t;

static volatile system_state_t s_sys_state = SYS_STATE_INIT;

/* ============================================================
 * 运动检测回调
 * ============================================================ */

/**
 * @brief 运动事件处理回调
 * 在检测到运动时被调用，执行拍照和通知
 */
static void on_motion_detected(const motion_event_t *event, void *user_data)
{
    if (s_sys_state == SYS_STATE_CAPTURING) {
        ESP_LOGD(TAG, "正在处理上一次事件，跳过本次");
        return;
    }

    s_sys_state = SYS_STATE_CAPTURING;

    const char *event_name = "";
    notify_type_t notify_type = NOTIFY_TYPE_PERSON;

    switch (event->type) {
        case MOTION_EVENT_PERSON:
            event_name   = "人员活动";
            notify_type  = NOTIFY_TYPE_PERSON;
            ESP_LOGI(TAG, "🚨 检测到门口有人！帧差: %.1f%%", event->diff_ratio * 100.0f);
            break;
        case MOTION_EVENT_PACKAGE:
            event_name   = "快递到达";
            notify_type  = NOTIFY_TYPE_PACKAGE;
            ESP_LOGI(TAG, "📦 检测到门口有快递！差异: %.1f%%", event->diff_ratio * 100.0f);
            break;
        case MOTION_EVENT_CLEAR:
            event_name   = "门口恢复正常";
            s_sys_state  = SYS_STATE_READY;
            return;
        default:
            s_sys_state = SYS_STATE_READY;
            return;
    }

    /* 拍摄照片 */
    doorman_frame_t frame = {0};
    esp_err_t ret = camera_manager_capture(&frame);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "拍照成功: %ux%u, %zu 字节", frame.width, frame.height, frame.len);

        /* 更新图像服务器 */
        image_server_update_image(&frame, event_name);

        /* 获取图像 URL */
        char image_url[64];
        image_server_get_url(image_url, sizeof(image_url));

        /* 构建附加信息 */
        char extra_info[128];
        snprintf(extra_info, sizeof(extra_info),
                 "图像大小: %zu 字节，帧差: %.1f%%",
                 frame.len, event->diff_ratio * 100.0f);

        /* 发送微信通知 */
#if CONFIG_DOORMAN_ENABLE_WECHAT_NOTIFY
        s_sys_state = SYS_STATE_NOTIFYING;
        notification_send_wechat(notify_type, image_url, extra_info);
#endif

        /* 释放帧内存 */
        camera_manager_free_frame(&frame);
    } else {
        ESP_LOGE(TAG, "拍照失败: %s", esp_err_to_name(ret));

        /* 即使拍照失败，也发送文字通知 */
#if CONFIG_DOORMAN_ENABLE_WECHAT_NOTIFY
        char image_url[64];
        image_server_get_url(image_url, sizeof(image_url));
        notification_send_wechat(notify_type, image_url, "拍照失败，请检查摄像头");
#endif
    }

    s_sys_state = SYS_STATE_READY;
}

/* ============================================================
 * 系统监控任务
 * ============================================================ */

/**
 * @brief 系统状态监控任务
 * 定期打印系统状态，检测 WiFi 断线并重连
 */
static void system_monitor_task(void *pvParameters)
{
    uint32_t loop_count = 0;
    char ip_buf[20];

    ESP_LOGI(TAG, "系统监控任务启动");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000)); /* 每 30 秒检查一次 */
        loop_count++;

        /* 检查 WiFi 连接 */
        if (!wifi_manager_is_connected()) {
            ESP_LOGW(TAG, "WiFi 断线，尝试重连...");
            wifi_manager_connect();
            esp_err_t ret = wifi_manager_wait_connected(30000);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "WiFi 重连失败");
            }
        }

        /* 打印系统状态 */
        if (wifi_manager_get_ip(ip_buf, sizeof(ip_buf)) == ESP_OK) {
            ESP_LOGI(TAG, "系统状态 [%lu]: IP=%s, 图像数=%d, 运行时间=%llu 秒",
                     (unsigned long)loop_count,
                     ip_buf,
                     image_server_get_image_count(),
                     (unsigned long long)(esp_timer_get_time() / 1000000));
        }

        /* 内存状态 */
        ESP_LOGI(TAG, "空闲堆内存: %lu 字节, 最小堆: %lu 字节",
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
    }
}

/* ============================================================
 * 主函数
 * ============================================================ */

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32-S3 门卫系统 v1.0");
    ESP_LOGI(TAG, "  家门口智能监控 + 微信推送");
    ESP_LOGI(TAG, "========================================");

    /* ---- 1. 初始化 NVS Flash ---- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区需要擦除，正在重新初始化...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✅ NVS 初始化完成");

    /* ---- 2. 初始化摄像头 ---- */
    ESP_LOGI(TAG, "正在初始化摄像头...");
    ret = camera_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 摄像头初始化失败: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "请检查摄像头连接和引脚配置（menuconfig -> 摄像头引脚配置）");
        /* 摄像头失败不阻止系统运行，PIR 检测仍可工作 */
    } else {
        ESP_LOGI(TAG, "✅ 摄像头初始化完成");
    }

    /* ---- 3. 初始化 WiFi ---- */
    ESP_LOGI(TAG, "正在初始化 WiFi...");
    s_sys_state = SYS_STATE_CONNECTING;
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(wifi_manager_connect());

    ESP_LOGI(TAG, "等待 WiFi 连接（SSID: %s）...", CONFIG_DOORMAN_WIFI_SSID);
    ret = wifi_manager_wait_connected(30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "⚠️  WiFi 连接超时，系统将继续运行（无网络通知功能）");
    } else {
        char ip_buf[20];
        wifi_manager_get_ip(ip_buf, sizeof(ip_buf));
        ESP_LOGI(TAG, "✅ WiFi 连接成功，IP: %s", ip_buf);
    }

    /* ---- 4. 初始化图像 HTTP 服务器 ---- */
    ESP_LOGI(TAG, "正在启动图像服务器...");
    ret = image_server_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 图像服务器启动失败: %s", esp_err_to_name(ret));
    } else {
        char url_buf[64];
        image_server_get_url(url_buf, sizeof(url_buf));
        ESP_LOGI(TAG, "✅ 图像服务器已启动: %s", url_buf);
    }

    /* ---- 5. 初始化通知模块 ---- */
    ESP_LOGI(TAG, "正在初始化通知模块...");
    ret = notification_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 通知模块初始化失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "✅ 通知模块初始化完成");
    }

    /* ---- 6. 初始化运动检测器 ---- */
    ESP_LOGI(TAG, "正在初始化运动检测器...");
    ret = motion_detector_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 运动检测器初始化失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "✅ 运动检测器初始化完成 (PIR GPIO: %d)", CONFIG_DOORMAN_PIR_GPIO);
    }

    /* 注册运动检测回调 */
    motion_detector_register_callback(on_motion_detected, NULL);

    /* ---- 7. 启动运动检测 ---- */
    ret = motion_detector_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 运动检测器启动失败");
    } else {
        ESP_LOGI(TAG, "✅ 运动检测器已启动");
    }

    /* ---- 8. 启动系统监控任务 ---- */
    xTaskCreate(system_monitor_task, "sys_monitor", 4096, NULL, 2, NULL);

    /* ---- 9. 发送上线通知 ---- */
    s_sys_state = SYS_STATE_READY;

    if (wifi_manager_is_connected()) {
        char ip_buf[20];
        char url_buf[64];
        wifi_manager_get_ip(ip_buf, sizeof(ip_buf));
        image_server_get_url(url_buf, sizeof(url_buf));

        char extra_info[128];
        snprintf(extra_info, sizeof(extra_info),
                 "设备 IP: %s | 监控页面: %s", ip_buf, url_buf);

        vTaskDelay(pdMS_TO_TICKS(2000)); /* 等待网络稳定 */
        notification_send_wechat(NOTIFY_TYPE_ONLINE, url_buf, extra_info);
    }

    /* ---- 系统就绪 ---- */
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  🏠 门卫系统已就绪，开始监控...");
    ESP_LOGI(TAG, "  📡 PIR 传感器: GPIO %d", CONFIG_DOORMAN_PIR_GPIO);
    ESP_LOGI(TAG, "  📷 摄像头: %s", camera_manager_is_ready() ? "就绪" : "未就绪");
    ESP_LOGI(TAG, "  🌐 WiFi: %s", wifi_manager_is_connected() ? "已连接" : "未连接");
    ESP_LOGI(TAG, "  🔔 微信通知: %s", notification_is_available() ? "已启用" : "未配置");
    ESP_LOGI(TAG, "========================================");

    /* 主循环：保持任务运行 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));

        /* 每分钟打印一次心跳日志 */
        ESP_LOGD(TAG, "💓 系统心跳 | 运行时间: %llu 分钟",
                 (unsigned long long)(esp_timer_get_time() / 60000000));
    }
}