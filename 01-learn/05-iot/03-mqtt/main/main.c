/**
 * @file main.c
 * @brief MQTT 客户端示例
 *
 * 功能：
 *   - 连接 MQTT Broker（使用免费的 broker.emqx.io）
 *   - 订阅主题并接收消息
 *   - 发布传感器数据
 *
 * MQTT 主题：
 *   订阅: esp32/cmd      (接收命令)
 *   发布: esp32/sensor   (发送传感器数据)
 *   发布: esp32/status   (发送设备状态)
 *
 * 测试工具：MQTTX (https://mqttx.app/)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "driver/gpio.h"

static const char *TAG = "MQTT";

#define WIFI_SSID       "your_wifi_ssid"
#define WIFI_PASS       "your_wifi_password"

/* MQTT Broker 配置（使用免费公共 Broker） */
#define MQTT_BROKER_URI "mqtt://broker.emqx.io:1883"
#define MQTT_CLIENT_ID  "esp32_learner_001"

/* MQTT 主题 */
#define TOPIC_CMD       "esp32/cmd"
#define TOPIC_SENSOR    "esp32/sensor"
#define TOPIC_STATUS    "esp32/status"

#define LED_GPIO        GPIO_NUM_2
#define WIFI_CONNECTED_BIT  BIT0

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;

/**
 * @brief MQTT 事件处理函数
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "✅ MQTT 已连接到 Broker");
        s_mqtt_connected = true;

        /* 订阅命令主题 */
        esp_mqtt_client_subscribe(s_mqtt_client, TOPIC_CMD, 1);
        ESP_LOGI(TAG, "已订阅主题: %s", TOPIC_CMD);

        /* 发布上线消息 */
        esp_mqtt_client_publish(s_mqtt_client, TOPIC_STATUS,
                                "{\"status\":\"online\",\"device\":\"ESP32\"}",
                                0, 1, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT 已断开");
        s_mqtt_connected = false;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "订阅成功，msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "发布成功，msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        /* 接收到消息 */
        ESP_LOGI(TAG, "收到消息:");
        ESP_LOGI(TAG, "  主题: %.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "  数据: %.*s", event->data_len, event->data);

        /* 处理 LED 控制命令 */
        if (strncmp(event->topic, TOPIC_CMD, event->topic_len) == 0) {
            if (strncmp(event->data, "led_on", event->data_len) == 0) {
                gpio_set_level(LED_GPIO, 1);
                ESP_LOGI(TAG, "LED ON");
                esp_mqtt_client_publish(s_mqtt_client, TOPIC_STATUS,
                                        "{\"led\":true}", 0, 0, 0);
            } else if (strncmp(event->data, "led_off", event->data_len) == 0) {
                gpio_set_level(LED_GPIO, 0);
                ESP_LOGI(TAG, "LED OFF");
                esp_mqtt_client_publish(s_mqtt_client, TOPIC_STATUS,
                                        "{\"led\":false}", 0, 0, 0);
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT 错误");
        break;

    default:
        break;
    }
}

/**
 * @brief 数据发布任务
 */
static void mqtt_publish_task(void *pvParameters)
{
    /* 等待 MQTT 连接 */
    while (!s_mqtt_connected) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    int count = 0;
    while (1) {
        if (s_mqtt_connected) {
            /* 模拟传感器数据 */
            float temperature = 25.0f + (count % 10) * 0.5f;
            float humidity = 60.0f + (count % 20) * 0.5f;

            char payload[128];
            snprintf(payload, sizeof(payload),
                     "{\"temp\":%.1f,\"humi\":%.1f,\"count\":%d,\"uptime\":%lld}",
                     temperature, humidity, count,
                     esp_timer_get_time() / 1000000);

            int msg_id = esp_mqtt_client_publish(s_mqtt_client, TOPIC_SENSOR,
                                                  payload, 0, 0, 0);
            ESP_LOGI(TAG, "发布传感器数据 (msg_id=%d): %s", msg_id, payload);
            count++;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));  // 每 5 秒发布一次
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) { esp_wifi_connect(); s_retry_num++; }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "MQTT 示例启动");
    ESP_LOGI(TAG, "Broker: %s", MQTT_BROKER_URI);

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

    /* 连接 WiFi */
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {.ssid = WIFI_SSID, .password = WIFI_PASS},
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* 等待 WiFi 连接 */
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);

    /* 初始化 MQTT 客户端 */
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.client_id = MQTT_CLIENT_ID,
    };
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID,
                                    mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);

    /* 创建数据发布任务 */
    xTaskCreate(mqtt_publish_task, "mqtt_pub", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "MQTT 客户端已启动");
    ESP_LOGI(TAG, "使用 MQTTX 连接 broker.emqx.io 订阅 esp32/sensor");
}