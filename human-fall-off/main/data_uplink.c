/*
 * 数据上报模块实现
 * 通过 UART 和 UDP 上报数据
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/uart.h"

#include "data_uplink.h"

static const char *TAG = "DATA_UPLINK";

/* 数据上报模块状态 */
static struct {
    bool initialized;
    data_uplink_config_t config;
    uint32_t total_sent;
    uint32_t total_failed;
} s_uplink_ctx = {0};

/**
 * @brief 通过 UART 发送数据
 */
static esp_err_t send_via_uart(const char *data, size_t len)
{
    int written = uart_write_bytes(s_uplink_ctx.config.uart_num, data, len);
    
    if (written < 0) {
        s_uplink_ctx.total_failed++;
        return ESP_FAIL;
    }
    
    s_uplink_ctx.total_sent++;
    return ESP_OK;
}

/**
 * @brief 格式化 CSI 数据为 JSON
 */
static int format_csi_json(char *buffer, size_t buf_size, const csi_data_t *csi_data)
{
    int offset = 0;
    
    offset += snprintf(buffer + offset, buf_size - offset,
                      "{\"type\":\"csi\",\"ts\":%llu,\"rssi\":%d,\"len\":%u,\"data\":[",
                      csi_data->timestamp_us, csi_data->rssi, csi_data->len);
    
    // 只发送前几个子载波的数据，避免数据量过大
    uint32_t sample_count = (csi_data->len / 2) < 16 ? (csi_data->len / 2) : 16;
    
    for (uint32_t i = 0; i < sample_count; i++) {
        int8_t i_val = csi_data->data[i * 2];
        int8_t q_val = csi_data->data[i * 2 + 1];
        
        offset += snprintf(buffer + offset, buf_size - offset,
                          "%s{\"i\":%d,\"q\":%d}",
                          i > 0 ? "," : "", i_val, q_val);
    }
    
    offset += snprintf(buffer + offset, buf_size - offset, "]}\n");
    
    return offset;
}

/**
 * @brief 格式化事件数据为 JSON
 */
static int format_event_json(char *buffer, size_t buf_size, const fall_event_t *event)
{
    const char *state_str[] = {"NORMAL", "SUSPECT_FALL", "CONFIRMED_FALL"};
    const char *motion_str[] = {"IDLE", "ACTIVE", "UNKNOWN"};
    
    return snprintf(buffer, buf_size,
                   "{\"type\":\"event\",\"ts\":%llu,\"state\":\"%s\",\"confidence\":%.2f,"
                   "\"motion_before\":\"%s\",\"motion_after\":\"%s\"}\n",
                   event->timestamp_us,
                   state_str[event->state],
                   event->confidence,
                   motion_str[event->motion_before],
                   motion_str[event->motion_after]);
}

esp_err_t data_uplink_init(const data_uplink_config_t *config)
{
    if (s_uplink_ctx.initialized) {
        ESP_LOGW(TAG, "数据上报模块已初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (config == NULL) {
        ESP_LOGE(TAG, "配置参数为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 保存配置
    memcpy(&s_uplink_ctx.config, config, sizeof(data_uplink_config_t));
    
    // 配置 UART (如果需要)
    if (config->uart_num != UART_NUM_0) {  // UART0 通常已被日志使用
        uart_config_t uart_config = {
            .baud_rate = config->baud_rate,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        
        ESP_ERROR_CHECK(uart_driver_install(config->uart_num, 1024, 0, 0, NULL, 0));
        ESP_ERROR_CHECK(uart_param_config(config->uart_num, &uart_config));
        
        ESP_LOGI(TAG, "UART%d 已配置 - 波特率: %lu", config->uart_num, config->baud_rate);
    }
    
    s_uplink_ctx.total_sent = 0;
    s_uplink_ctx.total_failed = 0;
    s_uplink_ctx.initialized = true;
    
    ESP_LOGI(TAG, "数据上报模块初始化成功");
    
    return ESP_OK;
}

esp_err_t data_uplink_send_csi(const csi_data_t *csi_data)
{
    if (!s_uplink_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (csi_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 格式化为 JSON
    char buffer[512];
    int len = format_csi_json(buffer, sizeof(buffer), csi_data);
    
    if (len <= 0) {
        ESP_LOGE(TAG, "格式化 CSI 数据失败");
        return ESP_FAIL;
    }
    
    // 通过 UART 发送
    return send_via_uart(buffer, len);
}

esp_err_t data_uplink_send_event(const fall_event_t *event)
{
    if (!s_uplink_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 格式化为 JSON
    char buffer[256];
    int len = format_event_json(buffer, sizeof(buffer), event);
    
    if (len <= 0) {
        ESP_LOGE(TAG, "格式化事件数据失败");
        return ESP_FAIL;
    }
    
    // 通过 UART 发送
    esp_err_t ret = send_via_uart(buffer, len);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "事件已上报: %s", buffer);
    }
    
    return ret;
}

esp_err_t data_uplink_send_message(const char *message)
{
    if (!s_uplink_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 格式化为 JSON
    char buffer[256];
    int len = snprintf(buffer, sizeof(buffer),
                      "{\"type\":\"message\",\"ts\":%llu,\"msg\":\"%s\"}\n",
                      esp_timer_get_time(), message);
    
    if (len <= 0) {
        return ESP_FAIL;
    }
    
    return send_via_uart(buffer, len);
}

void data_uplink_get_stats(uint32_t *total_sent, uint32_t *total_failed)
{
    if (total_sent != NULL) {
        *total_sent = s_uplink_ctx.total_sent;
    }
    
    if (total_failed != NULL) {
        *total_failed = s_uplink_ctx.total_failed;
    }
}