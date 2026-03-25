/**
 * @file mqtt_handler.c
 * @brief MQTT 通信模块实现
 */
#include "mqtt_handler.h"
#include "config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---- 事件位 ---- */
#define MQTT_CONNECTED_BIT  BIT0
#define MQTT_FAIL_BIT       BIT1

static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_state_t             s_state  = MQTT_STATE_DISCONNECTED;
static EventGroupHandle_t       s_evt_grp;
static char s_device_id[32];
static char s_broker_url[128];
static char s_username[64];
static char s_password[64];

/* ---- 主题缓冲区 ---- */
static char s_topic_telemetry[64];
static char s_topic_alarm[64];
static char s_topic_status[64];
static char s_topic_cmd[64];

/* ---- 从 NVS 加载配置 ---- */
static void load_config_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(MQTT_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len;
        len = sizeof(s_broker_url);
        if (nvs_get_str(h, "broker", s_broker_url, &len) != ESP_OK)
            strlcpy(s_broker_url, MQTT_BROKER_DEFAULT, sizeof(s_broker_url));
        len = sizeof(s_username);
        nvs_get_str(h, "username", s_username, &len);
        len = sizeof(s_password);
        nvs_get_str(h, "password", s_password, &len);
        nvs_close(h);
    } else {
        strlcpy(s_broker_url, MQTT_BROKER_DEFAULT, sizeof(s_broker_url));
    }
}

/* ---- 生成设备 ID（基于 MAC 地址后 3 字节） ---- */
static void gen_device_id(void)
{
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(s_device_id, sizeof(s_device_id), "%s%02X%02X%02X",
             MQTT_CLIENT_ID_PREFIX, mac[3], mac[4], mac[5]);
}

/* ---- MQTT 事件处理 ---- */
static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_MQTT, "已连接到 Broker: %s", s_broker_url);
        s_state = MQTT_STATE_CONNECTED;
        xEventGroupSetBits(s_evt_grp, MQTT_CONNECTED_BIT);

        /* 订阅命令主题 */
        esp_mqtt_client_subscribe(s_client, s_topic_cmd, 1);
        ESP_LOGI(TAG_MQTT, "订阅命令主题: %s", s_topic_cmd);

        /* 发布上线状态 */
        mqtt_publish_status(true);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG_MQTT, "与 Broker 断开连接");
        s_state = MQTT_STATE_DISCONNECTED;
        xEventGroupClearBits(s_evt_grp, MQTT_CONNECTED_BIT);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG_MQTT, "收到命令 [%.*s]: %.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        /* 命令解析（简化示例） */
        if (strncmp(event->data, "reboot", event->data_len) == 0) {
            ESP_LOGW(TAG_MQTT, "收到远程重启命令");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG_MQTT, "MQTT 错误");
        s_state = MQTT_STATE_ERROR;
        xEventGroupSetBits(s_evt_grp, MQTT_FAIL_BIT);
        break;

    default:
        break;
    }
}

esp_err_t mqtt_handler_init(void)
{
    s_evt_grp = xEventGroupCreate();
    configASSERT(s_evt_grp);

    gen_device_id();
    load_config_from_nvs();

    /* 构建主题字符串 */
    snprintf(s_topic_telemetry, sizeof(s_topic_telemetry), MQTT_TOPIC_TELEMETRY, s_device_id);
    snprintf(s_topic_alarm,     sizeof(s_topic_alarm),     MQTT_TOPIC_ALARM,     s_device_id);
    snprintf(s_topic_status,    sizeof(s_topic_status),    MQTT_TOPIC_STATUS,    s_device_id);
    snprintf(s_topic_cmd,       sizeof(s_topic_cmd),       MQTT_TOPIC_CMD,       s_device_id);

    ESP_LOGI(TAG_MQTT, "设备 ID: %s", s_device_id);
    ESP_LOGI(TAG_MQTT, "Broker: %s", s_broker_url);
    return ESP_OK;
}

esp_err_t mqtt_handler_start(void)
{
    /* LWT 消息 */
    char lwt_payload[64];
    snprintf(lwt_payload, sizeof(lwt_payload),
             "{\"device\":\"%s\",\"online\":false}", s_device_id);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri         = s_broker_url,
        .credentials.client_id      = s_device_id,
        .credentials.username       = s_username[0] ? s_username : NULL,
        .credentials.authentication.password = s_password[0] ? s_password : NULL,
        .session.keepalive           = MQTT_KEEPALIVE_S,
        .session.last_will.topic     = s_topic_status,
        .session.last_will.msg       = lwt_payload,
        .session.last_will.msg_len   = strlen(lwt_payload),
        .session.last_will.qos       = 1,
        .session.last_will.retain    = 1,
        .network.timeout_ms          = 10000,
        .task.stack_size             = TASK_STACK_MQTT,
        .task.priority               = TASK_PRIO_MQTT,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG_MQTT, "MQTT 客户端初始化失败");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    s_state = MQTT_STATE_CONNECTING;
    return esp_mqtt_client_start(s_client);
}

esp_err_t mqtt_handler_stop(void)
{
    if (s_client) {
        mqtt_publish_status(false);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_state = MQTT_STATE_DISCONNECTED;
    return ESP_OK;
}

esp_err_t mqtt_publish_telemetry(const env_data_t *data)
{
    if (!data || s_state != MQTT_STATE_CONNECTED) return ESP_ERR_INVALID_STATE;

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON_AddStringToObject(root, "device",      s_device_id);
    cJSON_AddStringToObject(root, "model",       DEVICE_MODEL);
    cJSON_AddStringToObject(root, "fw_ver",      FW_VERSION_STR);
    cJSON_AddNumberToObject(root, "ts",          (double)data->unix_time);
    cJSON_AddNumberToObject(root, "temperature", (double)data->temperature);
    cJSON_AddNumberToObject(root, "humidity",    (double)data->humidity);
    cJSON_AddNumberToObject(root, "pressure",    (double)data->pressure);
    cJSON_AddNumberToObject(root, "altitude",    (double)data->altitude);
    cJSON_AddNumberToObject(root, "co2_ppm",     (double)data->co2_ppm);
    cJSON_AddNumberToObject(root, "pm1_0",       (double)data->pm1_0);
    cJSON_AddNumberToObject(root, "pm2_5",       (double)data->pm2_5);
    cJSON_AddNumberToObject(root, "pm10",        (double)data->pm10);
    cJSON_AddNumberToObject(root, "aqi_level",   (double)data->aqi_level);

    /* 传感器健康状态 */
    cJSON *health = cJSON_AddObjectToObject(root, "health");
    cJSON_AddNumberToObject(health, "sht31",   data->status_sht31);
    cJSON_AddNumberToObject(health, "bme280",  data->status_bme280);
    cJSON_AddNumberToObject(health, "mhz19",   data->status_mhz19);
    cJSON_AddNumberToObject(health, "pms5003", data->status_pms5003);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return ESP_ERR_NO_MEM;

    int msg_id = esp_mqtt_client_publish(s_client, s_topic_telemetry,
                                          json_str, strlen(json_str),
                                          MQTT_QOS_TELEMETRY, 0);
    free(json_str);

    if (msg_id < 0) {
        ESP_LOGE(TAG_MQTT, "遥测数据发布失败");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG_MQTT, "遥测数据已发布 (msg_id=%d)", msg_id);
    return ESP_OK;
}

esp_err_t mqtt_publish_alarm(const alarm_event_t *event)
{
    if (!event || s_state != MQTT_STATE_CONNECTED) return ESP_ERR_INVALID_STATE;

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON_AddStringToObject(root, "device",    s_device_id);
    cJSON_AddNumberToObject(root, "ts",        (double)event->timestamp);
    cJSON_AddNumberToObject(root, "alarm_id",  (double)event->alarm_id);
    cJSON_AddStringToObject(root, "param",     event->param_name);
    cJSON_AddNumberToObject(root, "value",     (double)event->value);
    cJSON_AddNumberToObject(root, "threshold", (double)event->threshold);
    cJSON_AddBoolToObject(root,   "active",    event->active);
    cJSON_AddStringToObject(root, "severity",
                            event->severity == 2 ? "critical" :
                            event->severity == 1 ? "warning" : "info");

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return ESP_ERR_NO_MEM;

    int msg_id = esp_mqtt_client_publish(s_client, s_topic_alarm,
                                          json_str, strlen(json_str),
                                          MQTT_QOS_ALARM, 0);
    free(json_str);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_publish_status(bool online)
{
    if (!s_client) return ESP_ERR_INVALID_STATE;

    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"device\":\"%s\",\"online\":%s,\"fw_ver\":\"%s\"}",
             s_device_id, online ? "true" : "false", FW_VERSION_STR);

    esp_mqtt_client_publish(s_client, s_topic_status,
                             payload, strlen(payload), 1, 1);
    return ESP_OK;
}

mqtt_state_t mqtt_handler_get_state(void)
{
    return s_state;
}

esp_err_t mqtt_handler_set_config(const char *broker_url,
                                   const char *username,
                                   const char *password)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(MQTT_NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;

    if (broker_url) {
        nvs_set_str(h, "broker", broker_url);
        strlcpy(s_broker_url, broker_url, sizeof(s_broker_url));
    }
    if (username) {
        nvs_set_str(h, "username", username);
        strlcpy(s_username, username, sizeof(s_username));
    }
    if (password) {
        nvs_set_str(h, "password", password);
        strlcpy(s_password, password, sizeof(s_password));
    }
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

void mqtt_task(void *arg)
{
    ESP_LOGI(TAG_MQTT, "MQTT 发布任务启动");
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        if (s_state == MQTT_STATE_CONNECTED) {
            env_data_t data;
            sensor_manager_get_latest(&data);
            if (data.valid) {
                mqtt_publish_telemetry(&data);
            }
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MQTT_PUBLISH_MS));
    }
}