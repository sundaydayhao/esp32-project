/**
 * @file main.c
 * @brief 蓝牙 BLE GATT 服务器示例
 *
 * 功能：
 *   - 创建 BLE GATT 服务器
 *   - 广播设备名称 "ESP32-BLE"
 *   - 提供一个可读写的特征值（模拟 LED 控制）
 *   - 手机 APP（如 nRF Connect）可以连接并控制
 *
 * 使用方法：
 *   1. 烧录程序
 *   2. 手机安装 nRF Connect APP
 *   3. 扫描并连接 "ESP32-BLE"
 *   4. 找到特征值，写入 0x01 开灯，0x00 关灯
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "driver/gpio.h"

static const char *TAG = "BLE_GATT";

#define LED_GPIO        GPIO_NUM_2
#define DEVICE_NAME     "ESP32-BLE"

/* GATT 服务 UUID */
#define GATTS_SERVICE_UUID  0x00FF
#define GATTS_CHAR_UUID     0xFF01
#define GATTS_NUM_HANDLE    4

#define GATTS_APP_ID        0

static uint8_t char_value[1] = {0};
static uint16_t gatts_if_handle = ESP_GATT_IF_NONE;
static uint16_t conn_id = 0;
static bool is_connected = false;

/* 广播数据 */
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp        = false,
    .include_name        = true,
    .include_txpower     = false,
    .min_interval        = 0x0006,
    .max_interval        = 0x0010,
    .appearance          = 0x00,
    .manufacturer_len    = 0,
    .p_manufacturer_data = NULL,
    .service_data_len    = 0,
    .p_service_data      = NULL,
    .service_uuid_len    = 0,
    .p_service_uuid      = NULL,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

/* 广播参数 */
static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* GAP 事件处理 */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "广播已启动，设备名: %s", DEVICE_NAME);
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG, "广播已停止");
        break;
    default:
        break;
    }
}

/* GATTS 事件处理 */
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                  esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG, "GATT 服务注册，app_id: %d", param->reg.app_id);
        gatts_if_handle = gatts_if;

        /* 设置设备名称 */
        esp_ble_gap_set_device_name(DEVICE_NAME);

        /* 配置广播数据 */
        esp_ble_gap_config_adv_data(&adv_data);

        /* 创建 GATT 服务 */
        esp_gatt_srvc_id_t service_id = {
            .is_primary = true,
            .id.inst_id = 0x00,
            .id.uuid.len = ESP_UUID_LEN_16,
            .id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID,
        };
        esp_ble_gatts_create_service(gatts_if, &service_id, GATTS_NUM_HANDLE);
        break;

    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(TAG, "GATT 服务创建，handle: %d", param->create.service_handle);
        esp_ble_gatts_start_service(param->create.service_handle);

        /* 添加特征值 */
        esp_bt_uuid_t char_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid.uuid16 = GATTS_CHAR_UUID,
        };
        esp_gatt_char_prop_t char_property = ESP_GATT_CHAR_PROP_BIT_READ |
                                              ESP_GATT_CHAR_PROP_BIT_WRITE |
                                              ESP_GATT_CHAR_PROP_BIT_NOTIFY;
        esp_attr_value_t gatts_char_val = {
            .attr_max_len = 1,
            .attr_len     = 1,
            .attr_value   = char_value,
        };
        esp_ble_gatts_add_char(param->create.service_handle, &char_uuid,
                                ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                char_property, &gatts_char_val, NULL);
        break;

    case ESP_GATTS_CONNECT_EVT:
        conn_id = param->connect.conn_id;
        is_connected = true;
        ESP_LOGI(TAG, "设备连接，conn_id: %d, MAC: " ESP_BD_ADDR_STR,
                 conn_id, ESP_BD_ADDR_HEX(param->connect.remote_bda));
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        is_connected = false;
        ESP_LOGI(TAG, "设备断开，重新开始广播");
        esp_ble_gap_start_advertising(&adv_params);
        break;

    case ESP_GATTS_WRITE_EVT:
        ESP_LOGI(TAG, "收到写入，handle: %d, 长度: %d",
                 param->write.handle, param->write.len);
        if (param->write.len == 1) {
            char_value[0] = param->write.value[0];
            gpio_set_level(LED_GPIO, char_value[0] ? 1 : 0);
            ESP_LOGI(TAG, "LED %s", char_value[0] ? "ON" : "OFF");
        }
        break;

    case ESP_GATTS_READ_EVT:
        ESP_LOGI(TAG, "收到读取请求，当前值: 0x%02X", char_value[0]);
        break;

    default:
        break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "BLE GATT 服务器示例启动");

    /* 配置 LED */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    /* 初始化 NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 释放经典蓝牙内存（仅使用 BLE） */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    /* 初始化蓝牙控制器 */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    /* 初始化 Bluedroid */
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    /* 注册回调函数 */
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(GATTS_APP_ID));

    /* 设置 MTU */
    esp_ble_gatt_set_local_mtu(500);

    ESP_LOGI(TAG, "BLE 初始化完成，等待连接...");
    ESP_LOGI(TAG, "使用 nRF Connect APP 搜索 '%s'", DEVICE_NAME);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "BLE 状态: %s", is_connected ? "已连接" : "等待连接");
    }
}