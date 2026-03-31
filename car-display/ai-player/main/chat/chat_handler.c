/**
 * @file chat_handler.c
 * @brief 蓝牙聊天实现 (BLE NimBLE GATT Server)
 * 
 * GATT服务UUID: 0x1234
 * - TX特征值 (手机->设备): 0x1235, Write
 * - RX特征值 (设备->手机): 0x1236, Notify
 * 
 * 推荐使用 "nRF Connect" 或 "BLE调试助手" APP连接测试
 */

#include "chat_handler.h"
#include "config.h"
#include "empathy_engine.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "nvs_flash.h"

/* NimBLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "chat_ble";

/* ============================================================
 * BLE GATT 服务定义
 * ============================================================ */

#define CHAT_SERVICE_UUID   0x1234
#define CHAT_TX_CHAR_UUID   0x1235  /* 手机写入（发消息给设备） */
#define CHAT_RX_CHAR_UUID   0x1236  /* 设备通知（发消息给手机） */

static bool s_ble_connected = false;
static uint16_t s_conn_handle = 0;
static uint16_t s_rx_char_handle = 0;
static chat_receive_callback_t s_receive_callback = NULL;
static char s_peer_name[64] = {0};
static QueueHandle_t s_send_queue = NULL;

/* ============================================================
 * GATT 特征值访问回调
 * ============================================================ */

static int chat_tx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len >= BLE_MAX_MSG_LEN) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    char *buf = malloc(len + 1);
    if (buf == NULL) return BLE_ATT_ERR_INSUFFICIENT_RES;

    uint16_t out_len = 0;
    ble_hs_mbuf_to_flat(ctxt->om, buf, len, &out_len);
    buf[out_len] = '\0';

    ESP_LOGI(TAG, "收到消息: %s", buf);

    /* 构建聊天消息并触发回调 */
    chat_message_t msg = {
        .direction = CHAT_MSG_FROM_USER,
        .timestamp = xTaskGetTickCount(),
    };
    snprintf(msg.text, sizeof(msg.text), "%s", buf);
    free(buf);

    if (s_receive_callback) {
        s_receive_callback(&msg);
    }

    /* 生成共情回复并发送 */
    char reply[512];
    empathy_engine_chat(msg.text, reply, sizeof(reply));
    chat_handler_send(reply);

    return 0;
}

static int chat_rx_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0;
}

/* ============================================================
 * GATT 服务表
 * ============================================================ */

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(CHAT_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(CHAT_TX_CHAR_UUID),
                .access_cb = chat_tx_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(CHAT_RX_CHAR_UUID),
                .access_cb = chat_rx_access_cb,
                .val_handle = &s_rx_char_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },
    { 0 }
};

/* ============================================================
 * GAP 事件处理
 * ============================================================ */

static void start_advertising(void);

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_ble_connected = true;
                s_conn_handle = event->connect.conn_handle;
                snprintf(s_peer_name, sizeof(s_peer_name), "手机用户");
                ESP_LOGI(TAG, "BLE已连接，句柄: %d", s_conn_handle);
                /* 延迟发送欢迎消息，等待GATT服务就绪 */
                vTaskDelay(pdMS_TO_TICKS(800));
                chat_handler_send("你好！我是你的驾驶小伙伴 (^_^) 有什么想聊的吗？");
            } else {
                ESP_LOGW(TAG, "BLE连接失败: %d", event->connect.status);
                s_ble_connected = false;
                start_advertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            s_ble_connected = false;
            s_conn_handle = 0;
            memset(s_peer_name, 0, sizeof(s_peer_name));
            ESP_LOGI(TAG, "BLE已断开，原因: %d", event->disconnect.reason);
            start_advertising();
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU更新: conn=%d mtu=%d",
                     event->mtu.conn_handle, event->mtu.value);
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "订阅状态变化: attr=%d notify=%d",
                     event->subscribe.attr_handle,
                     event->subscribe.cur_notify);
            break;

        default:
            break;
    }
    return 0;
}

/* ============================================================
 * BLE 广播
 * ============================================================ */

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    const char *name = BLE_DEVICE_NAME;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "设置广播数据失败: %d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                            &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "开始广播失败: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "BLE广播中: \"%s\"", BLE_DEVICE_NAME);
}

/* ============================================================
 * BLE 主机回调
 * ============================================================ */

static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "BLE主机同步完成");
    start_advertising();
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE主机重置，原因: %d", reason);
    s_ble_connected = false;
}

/* ============================================================
 * NimBLE 主机任务
 * ============================================================ */

static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE主机任务启动");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ============================================================
 * 消息发送任务
 * ============================================================ */

static void send_task(void *arg)
{
    static char msg[BLE_MAX_MSG_LEN];

    while (1) {
        if (xQueueReceive(s_send_queue, msg, portMAX_DELAY) == pdTRUE) {
            if (s_ble_connected && s_rx_char_handle != 0) {
                struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, strlen(msg));
                if (om != NULL) {
                    int rc = ble_gatts_notify_custom(s_conn_handle,
                                                      s_rx_char_handle, om);
                    if (rc != 0) {
                        ESP_LOGW(TAG, "发送通知失败: %d", rc);
                    } else {
                        ESP_LOGI(TAG, "已发送: %.80s%s", msg,
                                 strlen(msg) > 80 ? "..." : "");
                    }
                }
            } else {
                ESP_LOGD(TAG, "BLE未连接，消息丢弃: %.40s", msg);
            }
        }
    }
}

/* ============================================================
 * 公共接口实现
 * ============================================================ */

esp_err_t chat_handler_init(void)
{
    ESP_LOGI(TAG, "初始化BLE聊天");

    /* NVS初始化（BLE需要） */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS擦除并重新初始化");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 创建发送队列 */
    s_send_queue = xQueueCreate(10, BLE_MAX_MSG_LEN);
    if (s_send_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* 初始化NimBLE */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 配置BLE主机 */
    ble_hs_cfg.sync_cb  = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    /* 设置设备名称 */
    ble_svc_gap_device_name_set(BLE_DEVICE_NAME);

    /* 初始化GATT服务 */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT服务计数失败: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT服务添加失败: %d", rc);
        return ESP_FAIL;
    }

    /* 启动NimBLE主机任务 */
    nimble_port_freertos_init(nimble_host_task);

    /* 启动消息发送任务 */
    xTaskCreate(send_task, "ble_send", 3072, NULL, 5, NULL);

    ESP_LOGI(TAG, "BLE聊天初始化完成，设备名: \"%s\"", BLE_DEVICE_NAME);
    return ESP_OK;
}

void chat_handler_register_callback(chat_receive_callback_t cb)
{
    s_receive_callback = cb;
}

esp_err_t chat_handler_send(const char *message)
{
    if (message == NULL) return ESP_ERR_INVALID_ARG;
    if (s_send_queue == NULL) return ESP_ERR_INVALID_STATE;

    /* 将消息放入发送队列（非阻塞） */
    static char buf[BLE_MAX_MSG_LEN];
    snprintf(buf, sizeof(buf), "%s", message);

    if (xQueueSend(s_send_queue, buf, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "发送队列已满，消息丢弃");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool chat_handler_is_connected(void)
{
    return s_ble_connected;
}

void chat_handler_get_peer_name(char *name, size_t len)
{
    if (name == NULL || len == 0) return;
    snprintf(name, len, "%s", s_ble_connected ? s_peer_name : "未连接");
}

void chat_handler_notify_mood(const char *mood_name, const char *message)
{
    if (!s_ble_connected) return;

    char notify_buf[BLE_MAX_MSG_LEN];
    snprintf(notify_buf, sizeof(notify_buf),
             "[状态:%s] %s", mood_name, message);
    chat_handler_send(notify_buf);
}