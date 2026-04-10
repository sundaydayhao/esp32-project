/*
 * WiFi CSI 数据采集模块实现
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "wifi_csi.h"
#include "motion_gate.h"

static const char *TAG = "WIFI_CSI";

/* WiFi CSI 模块状态 */
static struct {
    bool initialized;
    bool running;
    wifi_csi_config_t config;
    QueueHandle_t csi_queue;
    csi_data_callback_t callback;
    void *callback_ctx;
    uint32_t total_packets;
    uint32_t dropped_packets;
    csi_data_t latest_data;
} s_csi_ctx = {0};

/* CSI 工作任务句柄 */
static TaskHandle_t s_csi_worker_task = NULL;

/**
 * @brief WiFi CSI 回调函数
 * 在 WiFi 驱动中断上下文中调用，需要快速返回
 */
static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!s_csi_ctx.running || info == NULL) {
        return;
    }

    // 快速检查数据有效性
    if (info->len == 0 || info->len > CSI_MAX_SUBCARRIERS * 2) {
        s_csi_ctx.dropped_packets++;
        return;
    }

    // 构造 CSI 数据包
    csi_data_t csi_data = {0};
    csi_data.timestamp_us = esp_timer_get_time();
    csi_data.rssi = info->rx_ctrl.rssi;
    csi_data.rate = info->rx_ctrl.rate;
    csi_data.sig_mode = info->rx_ctrl.sig_mode;
    csi_data.mcs = info->rx_ctrl.mcs;
    csi_data.cwb = info->rx_ctrl.cwb;
    csi_data.len = info->len;
    
    // 复制 CSI 数据
    memcpy(csi_data.data, info->buf, info->len);

    // 尝试入队，如果队列满则丢弃
    if (xQueueSend(s_csi_ctx.csi_queue, &csi_data, 0) != pdTRUE) {
        s_csi_ctx.dropped_packets++;
    } else {
        s_csi_ctx.total_packets++;
    }
}

/**
 * @brief CSI 数据处理任务
 */
static void csi_worker_task(void *pvParameters)
{
    csi_data_t csi_data;
    
    ESP_LOGI(TAG, "CSI 工作任务启动");
    
    while (s_csi_ctx.running) {
        // 从队列获取 CSI 数据
        if (xQueueReceive(s_csi_ctx.csi_queue, &csi_data, pdMS_TO_TICKS(100)) == pdTRUE) {
            // 保存最新数据
            memcpy(&s_csi_ctx.latest_data, &csi_data, sizeof(csi_data_t));
            
            // 调用用户回调
            if (s_csi_ctx.callback != NULL) {
                s_csi_ctx.callback(&csi_data, s_csi_ctx.callback_ctx);
            }
            
            // 将数据传递给运动门控模块
            motion_gate_process_csi(&csi_data);
        }
    }
    
    ESP_LOGI(TAG, "CSI 工作任务退出");
    vTaskDelete(NULL);
}

/**
 * @brief 初始化 WiFi
 */
static esp_err_t init_wifi(const wifi_csi_config_t *config)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // 配置 WiFi 连接参数
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    strncpy((char *)wifi_config.sta.ssid, config->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, config->password, sizeof(wifi_config.sta.password) - 1);
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // 设置信道
    ESP_ERROR_CHECK(esp_wifi_set_channel(config->channel, WIFI_SECOND_CHAN_NONE));
    
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi 初始化完成 - SSID: %s, 信道: %d", config->ssid, config->channel);
    
    return ESP_OK;
}

/**
 * @brief 配置 CSI
 */
static esp_err_t config_csi(void)
{
    // 配置 CSI 参数
    wifi_csi_config_t csi_config = {
        .lltf_en = true,           // 启用 Legacy Long Training Field
        .htltf_en = true,          // 启用 HT Long Training Field  
        .stbc_htltf2_en = true,    // 启用 STBC HT-LTF2
        .ltf_merge_en = true,      // 启用 LTF 合并
        .channel_filter_en = false, // 禁用信道滤波
        .manu_scale = false,       // 自动缩放
        .shift = 0,                // 不移位
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    
    // 注册 CSI 回调
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL));
    
    // 启用 CSI
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
    
    ESP_LOGI(TAG, "CSI 配置完成");
    
    return ESP_OK;
}

esp_err_t wifi_csi_init(const wifi_csi_config_t *config)
{
    if (s_csi_ctx.initialized) {
        ESP_LOGW(TAG, "WiFi CSI 已初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (config == NULL) {
        ESP_LOGE(TAG, "配置参数为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 保存配置
    memcpy(&s_csi_ctx.config, config, sizeof(wifi_csi_config_t));
    
    // 创建 CSI 数据队列
    s_csi_ctx.csi_queue = xQueueCreate(CSI_BUFFER_SIZE, sizeof(csi_data_t));
    if (s_csi_ctx.csi_queue == NULL) {
        ESP_LOGE(TAG, "创建 CSI 队列失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化 WiFi
    esp_err_t ret = init_wifi(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 初始化失败");
        vQueueDelete(s_csi_ctx.csi_queue);
        return ret;
    }
    
    // 配置 CSI
    if (config->enable_csi) {
        ret = config_csi();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "CSI 配置失败");
            esp_wifi_stop();
            vQueueDelete(s_csi_ctx.csi_queue);
            return ret;
        }
    }
    
    s_csi_ctx.initialized = true;
    ESP_LOGI(TAG, "WiFi CSI 模块初始化成功");
    
    return ESP_OK;
}

esp_err_t wifi_csi_start(void)
{
    if (!s_csi_ctx.initialized) {
        ESP_LOGE(TAG, "WiFi CSI 未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_csi_ctx.running) {
        ESP_LOGW(TAG, "CSI 采集已在运行");
        return ESP_OK;
    }
    
    s_csi_ctx.running = true;
    s_csi_ctx.total_packets = 0;
    s_csi_ctx.dropped_packets = 0;
    
    // 创建 CSI 工作任务
    BaseType_t ret = xTaskCreate(
        csi_worker_task,
        "csi_worker",
        4096,
        NULL,
        5,  // 较高优先级
        &s_csi_worker_task
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建 CSI 工作任务失败");
        s_csi_ctx.running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "CSI 数据采集已启动");
    
    return ESP_OK;
}

esp_err_t wifi_csi_stop(void)
{
    if (!s_csi_ctx.running) {
        return ESP_OK;
    }
    
    s_csi_ctx.running = false;
    
    // 等待任务退出
    if (s_csi_worker_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(200));
        s_csi_worker_task = NULL;
    }
    
    ESP_LOGI(TAG, "CSI 数据采集已停止");
    
    return ESP_OK;
}

esp_err_t wifi_csi_register_callback(csi_data_callback_t callback, void *user_ctx)
{
    s_csi_ctx.callback = callback;
    s_csi_ctx.callback_ctx = user_ctx;
    
    ESP_LOGI(TAG, "CSI 回调已注册");
    
    return ESP_OK;
}

esp_err_t wifi_csi_get_latest(csi_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_csi_ctx.running) {
        return ESP_ERR_INVALID_STATE;
    }
    
    memcpy(data, &s_csi_ctx.latest_data, sizeof(csi_data_t));
    
    return ESP_OK;
}

void wifi_csi_get_stats(uint32_t *total_packets, uint32_t *dropped_packets)
{
    if (total_packets != NULL) {
        *total_packets = s_csi_ctx.total_packets;
    }
    
    if (dropped_packets != NULL) {
        *dropped_packets = s_csi_ctx.dropped_packets;
    }
}