/*
 * ESP32-S3 人体跌倒检测系统 - 主程序
 * 基于 WiFi CSI 实现非接触式跌倒监测
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "wifi_csi.h"
#include "motion_gate.h"
#include "event_detector.h"
#include "data_uplink.h"

static const char *TAG = "FALL_DETECT";

/* WiFi 配置 */
#define WIFI_SSID      CONFIG_WIFI_SSID
#define WIFI_PASS      CONFIG_WIFI_PASSWORD
#define WIFI_CHANNEL   CONFIG_WIFI_CHANNEL

/* 系统状态事件组 */
static EventGroupHandle_t s_system_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define CSI_READY_BIT      BIT1

/**
 * @brief WiFi 事件处理器
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi 断开连接，尝试重连...");
        xEventGroupClearBits(s_system_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "获得IP地址:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_system_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief 初始化 NVS
 */
static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

/**
 * @brief 初始化网络接口
 */
static void init_network(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
}

/**
 * @brief 系统监控任务
 */
static void system_monitor_task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        // 每10秒输出一次系统状态
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(10000));
        
        ESP_LOGI(TAG, "=== 系统状态 ===");
        ESP_LOGI(TAG, "空闲堆内存: %lu bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "最小空闲堆: %lu bytes", esp_get_minimum_free_heap_size());
        
        // 获取各模块状态
        motion_state_t motion_state = motion_gate_get_state();
        const char *motion_str[] = {"空闲", "活动中", "未知"};
        ESP_LOGI(TAG, "运动状态: %s", motion_str[motion_state]);
        
        event_state_t event_state = event_detector_get_state();
        const char *event_str[] = {"正常", "疑似跌倒", "确认跌倒"};
        ESP_LOGI(TAG, "事件状态: %s", event_str[event_state]);
    }
}

/**
 * @brief 应用程序主入口
 */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32-S3 人体跌倒检测系统 V1.0");
    ESP_LOGI(TAG, "  基于 WiFi CSI 非接触式监测");
    ESP_LOGI(TAG, "========================================");
    
    // 创建系统事件组
    s_system_event_group = xEventGroupCreate();
    
    // 1. 初始化 NVS
    ESP_LOGI(TAG, "[1/6] 初始化 NVS...");
    init_nvs();
    
    // 2. 初始化网络
    ESP_LOGI(TAG, "[2/6] 初始化网络接口...");
    init_network();
    
    // 3. 注册事件处理器
    ESP_LOGI(TAG, "[3/6] 注册事件处理器...");
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                                &wifi_event_handler, NULL));
    
    // 4. 初始化 WiFi CSI
    ESP_LOGI(TAG, "[4/6] 初始化 WiFi CSI 模块...");
    wifi_csi_config_t csi_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS,
        .channel = WIFI_CHANNEL,
        .enable_csi = true,
    };
    ESP_ERROR_CHECK(wifi_csi_init(&csi_config));
    
    // 等待 WiFi 连接
    ESP_LOGI(TAG, "等待 WiFi 连接...");
    xEventGroupWaitBits(s_system_event_group, WIFI_CONNECTED_BIT, 
                        pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi 已连接");
    
    // 5. 初始化运动门控
    ESP_LOGI(TAG, "[5/6] 初始化运动门控模块...");
    motion_gate_config_t motion_config = {
        .threshold = 100.0f,
        .window_size = 50,
    };
    ESP_ERROR_CHECK(motion_gate_init(&motion_config));
    
    // 6. 初始化事件检测器
    ESP_LOGI(TAG, "[6/6] 初始化事件检测器...");
    event_detector_config_t event_config = {
        .fall_threshold = 0.7f,
        .confirm_duration_ms = 5000,
    };
    ESP_ERROR_CHECK(event_detector_init(&event_config));
    
    // 7. 初始化数据上报
    ESP_LOGI(TAG, "初始化数据上报模块...");
    data_uplink_config_t uplink_config = {
        .uart_num = UART_NUM_0,
        .baud_rate = 115200,
        .enable_udp = false,
    };
    ESP_ERROR_CHECK(data_uplink_init(&uplink_config));
    
    // 启动 WiFi CSI 采集
    ESP_LOGI(TAG, "启动 CSI 数据采集...");
    ESP_ERROR_CHECK(wifi_csi_start());
    xEventGroupSetBits(s_system_event_group, CSI_READY_BIT);
    
    // 创建系统监控任务
    xTaskCreate(system_monitor_task, "sys_monitor", 4096, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  系统初始化完成，开始监测");
    ESP_LOGI(TAG, "========================================");
    
    // 主循环 - 保持系统运行
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}