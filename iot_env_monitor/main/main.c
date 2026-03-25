/**
 * @file main.c
 * @brief 工业级 IoT 环境监测节点 — 主程序入口
 *
 * 启动流程：
 *  1. NVS / 事件循环 / 网络接口初始化
 *  2. GPIO / LED / 蜂鸣器初始化
 *  3. 传感器管理器初始化（I2C + UART）
 *  4. 报警管理器初始化
 *  5. 数据记录器初始化（SPIFFS）
 *  6. OTA 管理器初始化
 *  7. WiFi 连接（STA 模式，断线自动重连）
 *  8. NTP 时间同步
 *  9. MQTT 连接
 * 10. Web 服务器启动
 * 11. Modbus RTU 从站初始化
 * 12. OLED 显示初始化
 * 13. 创建所有 FreeRTOS 任务
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "esp_task_wdt.h"

#include "config.h"
#include "sensors/sensor_manager.h"
#include "comm/mqtt_handler.h"
#include "comm/modbus_rtu.h"
#include "comm/web_server.h"
#include "storage/data_logger.h"
#include "alarm/alarm_manager.h"
#include "ota/ota_manager.h"
#include "display/display_manager.h"

/* ================================================================
 * WiFi 事件组
 * ================================================================ */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_evt_grp;
static int                s_wifi_retry = 0;
static bool               s_ntp_synced = false;

static esp_err_t validate_output_pin(const char *name, gpio_num_t pin)
{
    if (GPIO_IS_VALID_OUTPUT_GPIO(pin)) return ESP_OK;
    ESP_LOGE(TAG_MAIN, "%s uses invalid output GPIO %d for %s", CONFIG_IDF_TARGET, pin, name);
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t validate_input_pin(const char *name, gpio_num_t pin)
{
    if (GPIO_IS_VALID_GPIO(pin)) return ESP_OK;
    ESP_LOGE(TAG_MAIN, "%s uses invalid GPIO %d for %s", CONFIG_IDF_TARGET, pin, name);
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t board_pinmap_validate(void)
{
    esp_err_t ret = ESP_OK;

    ret |= validate_output_pin("I2C_SCL_PIN", I2C_SCL_PIN);
    ret |= validate_output_pin("I2C_SDA_PIN", I2C_SDA_PIN);
    ret |= validate_output_pin("MHZ19_TX_PIN", MHZ19_TX_PIN);
    ret |= validate_input_pin("MHZ19_RX_PIN", MHZ19_RX_PIN);
    ret |= validate_output_pin("PMS5003_TX_PIN", PMS5003_TX_PIN);
    ret |= validate_input_pin("PMS5003_RX_PIN", PMS5003_RX_PIN);
    ret |= validate_output_pin("MODBUS_TX_PIN", MODBUS_TX_PIN);
    ret |= validate_input_pin("MODBUS_RX_PIN", MODBUS_RX_PIN);
    ret |= validate_output_pin("MODBUS_DE_PIN", MODBUS_DE_PIN);
    ret |= validate_output_pin("LED_STATUS_PIN", LED_STATUS_PIN);
    ret |= validate_output_pin("LED_ALARM_PIN", LED_ALARM_PIN);
    ret |= validate_output_pin("BUZZER_PIN", BUZZER_PIN);
    ret |= validate_output_pin("PMS5003_SET_PIN", PMS5003_SET_PIN);
    ret |= validate_input_pin("BTN_RESET_PIN", BTN_RESET_PIN);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG_MAIN,
                 "Board pin map: I2C SCL=%d SDA=%d, PMS TX=%d RX=%d SET=%d",
                 I2C_SCL_PIN, I2C_SDA_PIN,
                 PMS5003_TX_PIN, PMS5003_RX_PIN, PMS5003_SET_PIN);
    }

    return ret == ESP_OK ? ESP_OK : ESP_ERR_INVALID_ARG;
}

/* ================================================================
 * 报警回调 — 触发蜂鸣器
 * ================================================================ */
static void on_alarm_event(const alarm_event_t *event)
{
    if (event->active && event->severity >= 2) {
        /* 严重报警：蜂鸣器鸣响 */
        gpio_set_level(BUZZER_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(BUZZER_PIN, 0);
    }
    /* 通过 MQTT 发布报警事件 */
    mqtt_publish_alarm(event);
}

/* ================================================================
 * WiFi 事件处理
 * ================================================================ */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        gpio_set_level(LED_STATUS_PIN, 0);

    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        gpio_set_level(LED_STATUS_PIN, 0);
        if (s_wifi_retry < WIFI_MAX_RETRY) {
            s_wifi_retry++;
            ESP_LOGW(TAG_MAIN, "WiFi 断开，第 %d 次重连...", s_wifi_retry);
            vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG_MAIN, "WiFi 连接失败，已达最大重试次数");
            xEventGroupSetBits(s_wifi_evt_grp, WIFI_FAIL_BIT);
        }

    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG_MAIN, "WiFi 已连接，IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        s_wifi_retry = 0;
        gpio_set_level(LED_STATUS_PIN, 1);
        xEventGroupSetBits(s_wifi_evt_grp, WIFI_CONNECTED_BIT);
    }
}

/* ================================================================
 * NTP 同步回调
 * ================================================================ */
static void ntp_sync_callback(struct timeval *tv)
{
    s_ntp_synced = true;
    char buf[32];
    struct tm timeinfo;
    localtime_r(&tv->tv_sec, &timeinfo);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG_MAIN, "NTP 时间同步成功: %s", buf);
}

/* ================================================================
 * GPIO 初始化
 * ================================================================ */
static void gpio_init(void)
{
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << LED_STATUS_PIN) |
                        (1ULL << LED_ALARM_PIN)  |
                        (1ULL << BUZZER_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);
    gpio_set_level(LED_STATUS_PIN, 0);
    gpio_set_level(LED_ALARM_PIN,  0);
    gpio_set_level(BUZZER_PIN,     0);

    /* 开机自检：LED 闪烁 3 次 */
    for (int i = 0; i < 3; i++) {
        gpio_set_level(LED_STATUS_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(LED_STATUS_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG_MAIN, "GPIO 初始化完成");
}

/* ================================================================
 * WiFi 初始化（STA 模式）
 * ================================================================ */
static esp_err_t wifi_init_sta(void)
{
    /* 从 NVS 读取 WiFi 配置 */
    char ssid[WIFI_SSID_MAX_LEN] = "";
    char pass[WIFI_PASS_MAX_LEN] = "";

    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len;
        len = sizeof(ssid); nvs_get_str(h, "ssid", ssid, &len);
        len = sizeof(pass); nvs_get_str(h, "pass", pass, &len);
        nvs_close(h);
    }

    /* 若 NVS 中无配置，使用编译时默认值（可通过 menuconfig 设置） */
    if (strlen(ssid) == 0) {
        ESP_LOGW(TAG_MAIN, "未找到 WiFi 配置，使用默认 SSID");
        strlcpy(ssid, CONFIG_WIFI_SSID_DEFAULT, sizeof(ssid));
        strlcpy(pass, CONFIG_WIFI_PASS_DEFAULT, sizeof(pass));
    }

    s_wifi_evt_grp = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &inst_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_got_ip));

    wifi_config_t wifi_cfg = { 0 };
    strlcpy((char *)wifi_cfg.sta.ssid,     ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable    = true;
    wifi_cfg.sta.pmf_cfg.required   = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_MAIN, "WiFi 初始化完成，正在连接 SSID: %s", ssid);

    /* 等待连接结果（最多 30s） */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_evt_grp,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(30000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_MAIN, "WiFi 连接成功");
        return ESP_OK;
    }
    ESP_LOGW(TAG_MAIN, "WiFi 连接失败，将在后台继续重试");
    return ESP_FAIL;
}

/* ================================================================
 * NTP 初始化
 * ================================================================ */
static void ntp_init(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "cn.pool.ntp.org");
    sntp_set_time_sync_notification_cb(ntp_sync_callback);
    esp_sntp_init();

    /* 设置时区为中国标准时间 */
    setenv("TZ", "CST-8", 1);
    tzset();

    ESP_LOGI(TAG_MAIN, "NTP 客户端已启动");
}

/* ================================================================
 * 看门狗喂狗任务
 * ================================================================ */
static void watchdog_task(void *arg)
{
    ESP_LOGI(TAG_MAIN, "看门狗任务启动");
    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_FEED_MS));
    }
}

/* ================================================================
 * 状态 LED 闪烁任务
 * ================================================================ */
static void led_task(void *arg)
{
    while (1) {
        EventBits_t bits = xEventGroupGetBits(s_wifi_evt_grp);
        if (bits & WIFI_CONNECTED_BIT) {
            /* WiFi 已连接：慢闪（1Hz） */
            gpio_set_level(LED_STATUS_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(LED_STATUS_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(900));
        } else {
            /* WiFi 未连接：快闪（5Hz） */
            gpio_set_level(LED_STATUS_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(LED_STATUS_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/* ================================================================
 * 主函数
 * ================================================================ */
void app_main(void)
{
    ESP_LOGI(TAG_MAIN, "========================================");
    ESP_LOGI(TAG_MAIN, "  IEM-3200 工业环境监测节点 v%s", FW_VERSION_STR);
    ESP_LOGI(TAG_MAIN, "  %s @ %d MHz", CONFIG_IDF_TARGET, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    ESP_LOGI(TAG_MAIN, "========================================");

    ESP_ERROR_CHECK(board_pinmap_validate());

    /* ---- 1. NVS 初始化 ---- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG_MAIN, "NVS 分区损坏，执行擦除...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG_MAIN, "[1/12] NVS 初始化完成");

    /* ---- 2. GPIO 初始化 ---- */
    gpio_init();
    ESP_LOGI(TAG_MAIN, "[2/12] GPIO 初始化完成");

    /* ---- 3. 传感器管理器初始化 ---- */
    ret = sensor_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "[3/12] 传感器初始化失败，继续运行（部分功能不可用）");
    } else {
        ESP_LOGI(TAG_MAIN, "[3/12] 传感器管理器初始化完成");
    }

    /* ---- 4. 报警管理器初始化 ---- */
    ESP_ERROR_CHECK(alarm_manager_init());
    alarm_manager_register_callback(on_alarm_event);
    ESP_LOGI(TAG_MAIN, "[4/12] 报警管理器初始化完成");

    /* ---- 5. 数据记录器初始化 ---- */
    ret = data_logger_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_MAIN, "[5/12] 数据记录器初始化失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG_MAIN, "[5/12] 数据记录器初始化完成 (已有 %lu 条记录)",
                 data_logger_get_count());
    }

    /* ---- 6. OTA 管理器初始化 ---- */
    ESP_ERROR_CHECK(ota_manager_init());
    ESP_LOGI(TAG_MAIN, "[6/12] OTA 管理器初始化完成");

    /* ---- 7. WiFi 连接 ---- */
    wifi_init_sta();
    ESP_LOGI(TAG_MAIN, "[7/12] WiFi 初始化完成");

    /* ---- 8. NTP 时间同步 ---- */
    ntp_init();
    ESP_LOGI(TAG_MAIN, "[8/12] NTP 客户端已启动");

    /* ---- 9. MQTT 初始化 ---- */
    ret = mqtt_handler_init();
    if (ret == ESP_OK) {
        mqtt_handler_start();
        ESP_LOGI(TAG_MAIN, "[9/12] MQTT 客户端已启动");
    } else {
        ESP_LOGW(TAG_MAIN, "[9/12] MQTT 初始化失败");
    }

    /* ---- 10. Web 服务器 ---- */
    ret = web_server_start();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG_MAIN, "[10/12] Web 服务器已启动 (port=%d)", WEB_SERVER_PORT);
    } else {
        ESP_LOGW(TAG_MAIN, "[10/12] Web 服务器启动失败");
    }

    /* ---- 11. Modbus RTU ---- */
    ret = modbus_rtu_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG_MAIN, "[11/12] Modbus RTU 从站已初始化");
    } else {
        ESP_LOGW(TAG_MAIN, "[11/12] Modbus RTU 初始化失败");
    }

    /* ---- 12. OLED 显示 ---- */
    ret = display_manager_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG_MAIN, "[12/12] OLED 显示初始化完成");
    } else {
        ESP_LOGW(TAG_MAIN, "[12/12] OLED 初始化失败（无显示器）");
    }

    ESP_LOGI(TAG_MAIN, "所有模块初始化完成，创建 FreeRTOS 任务...");

    /* ================================================================
     * 创建 FreeRTOS 任务
     * ================================================================ */

    /* 传感器采样任务 */
    xTaskCreatePinnedToCore(sensor_task, "sensor",
                            TASK_STACK_SENSOR, NULL,
                            TASK_PRIO_SENSOR, NULL, 1);

    /* 报警检查任务 */
    xTaskCreatePinnedToCore(alarm_task, "alarm",
                            TASK_STACK_ALARM, NULL,
                            TASK_PRIO_ALARM, NULL, 1);

    /* MQTT 发布任务 */
    xTaskCreatePinnedToCore(mqtt_task, "mqtt",
                            TASK_STACK_MQTT, NULL,
                            TASK_PRIO_MQTT, NULL, 0);

    /* Modbus RTU 从站任务 */
    xTaskCreatePinnedToCore(modbus_task, "modbus",
                            TASK_STACK_MODBUS, NULL,
                            TASK_PRIO_MODBUS, NULL, 1);

    /* 数据记录任务 */
    xTaskCreatePinnedToCore(data_logger_task, "datalog",
                            TASK_STACK_DATALOG, NULL,
                            TASK_PRIO_DATALOG, NULL, 0);

    /* OTA 任务 */
    xTaskCreatePinnedToCore(ota_task, "ota",
                            TASK_STACK_OTA, NULL,
                            TASK_PRIO_OTA, NULL, 0);

    /* 显示任务 */
    xTaskCreatePinnedToCore(display_task, "display",
                            TASK_STACK_DISPLAY, NULL,
                            TASK_PRIO_DISPLAY, NULL, 0);

    /* 状态 LED 任务 */
    xTaskCreate(led_task, "led", 1024, NULL, 1, NULL);

    /* 看门狗喂狗任务 */
    xTaskCreate(watchdog_task, "wdt", 1024, NULL, configMAX_PRIORITIES - 1, NULL);

    ESP_LOGI(TAG_MAIN, "所有任务已创建，系统运行中...");
    ESP_LOGI(TAG_MAIN, "可用堆内存: %lu KB", esp_get_free_heap_size() / 1024);
}
