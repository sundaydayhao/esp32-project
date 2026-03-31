/**
 * @file main.c
 * @brief CarPal - 汽车小摆件主程序
 * 
 * 功能：
 *   - 实时显示车速（GPS或模拟）
 *   - 根据车速/转向/加速度提供共情响应（TTS语音播报）
 *   - 语音指令交互（HLK-V20唤醒词 + 指令识别）
 *   - 驾驶行为分析（急刹、急转、超速等）
 * 
 * 硬件：
 *   - ESP32-S3 开发板
 *   - ST7789 240x240 SPI LCD
 *   - SYN6288 TTS语音合成模块（UART2 TX）
 *   - HLK-V20 离线语音识别模块（UART2 RX）
 *   - MPU6050 IMU（可选，I2C）
 *   - GPS模块（可选，UART1）
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

#include "config.h"
#include "display.h"
#include "ui_manager.h"
#include "speed_sensor.h"
#include "imu_sensor.h"
#include "empathy_engine.h"
#include "voice_handler.h"

static const char *TAG = "main";

/* ============================================================
 * 语音指令处理
 * ============================================================ */

/**
 * @brief 处理语音识别指令
 * 根据指令ID生成对应的语音回复和UI更新
 */
static void on_asr_command(asr_command_t cmd)
{
    ESP_LOGI(TAG, "语音指令: 0x%02X", cmd);

    /* 获取当前驾驶状态 */
    speed_data_t speed = {0};
    speed_sensor_get_data(&speed);

    switch (cmd) {
        case ASR_CMD_WAKE:
            /* 唤醒响应已在voice_handler内部处理 */
            ui_manager_update_voice_status(true, false);
            break;

        case ASR_CMD_SPEED:
            /* 播报当前速度 */
            voice_handler_speakf("当前速度%.0f公里每小时", speed.speed_kmh);
            break;

        case ASR_CMD_TIRED:
            /* 疲劳提醒 */
            voice_handler_speak("感觉累了吗？找个安全的地方停车休息一下吧，安全最重要！");
            {
                empathy_response_t resp = {
                    .mood = MOOD_WORRIED,
                    .event = EVENT_LONG_DRIVE,
                    .message = "感觉累了？停车休息一下吧~",
                    .emoji = "(>_<;)",
                    .display_duration_ms = 5000,
                };
                ui_manager_update_empathy(&resp);
            }
            break;

        case ASR_CMD_TRAFFIC_JAM:
            /* 堵车共情 */
            voice_handler_speak("堵车了，别着急，深呼吸，我陪着你~");
            {
                empathy_response_t resp = {
                    .mood = MOOD_CHEERING,
                    .event = EVENT_TRAFFIC_JAM,
                    .message = "堵车别烦躁，我陪你~",
                    .emoji = "(^o^)",
                    .display_duration_ms = 5000,
                };
                ui_manager_update_empathy(&resp);
            }
            break;

        case ASR_CMD_WEATHER:
            voice_handler_speak("我没有联网，不知道天气，不过开车要注意安全哦！");
            break;

        case ASR_CMD_JOKE: {
            /* 随机讲笑话 */
            const char *jokes[] = {
                "为什么程序员不喜欢户外活动？因为外面有太多的bug！",
                "司机问导航：前方怎么走？导航说：你已经到达目的地。司机：我还在路上啊！",
                "堵车的时候，时间过得最慢，但油耗过得最快！",
                "开车要专心，不然你的车险就不是保险了，是必险！",
            };
            int idx = (int)(esp_timer_get_time() / 1000000) % 4;
            voice_handler_speak(jokes[idx]);
            break;
        }

        case ASR_CMD_HELLO:
            voice_handler_speak("你好！我是小车伴，很高兴陪你一起开车！");
            break;

        case ASR_CMD_THANKS:
            voice_handler_speak("不客气！能陪伴你是我的荣幸，一路平安！");
            break;

        case ASR_CMD_QUIET:
            voice_handler_stop();
            ESP_LOGI(TAG, "用户要求安静，停止播报");
            break;

        case ASR_CMD_LOUDER:
            {
                static uint8_t vol = 5;
                if (vol < 9) vol++;
                voice_handler_set_volume(vol);
                voice_handler_speakf("音量已调大到%d", vol);
            }
            break;

        case ASR_CMD_SOFTER:
            {
                static uint8_t vol = 5;
                if (vol > 1) vol--;
                voice_handler_set_volume(vol);
                voice_handler_speakf("音量已调小到%d", vol);
            }
            break;

        case ASR_CMD_MUSIC:
            voice_handler_speak("我没有音乐播放功能，不过你可以打开手机音乐哦！");
            break;

        case ASR_CMD_NAVIGATE:
            voice_handler_speak("导航功能需要手机配合，请打开手机导航应用！");
            break;

        case ASR_CMD_TRIP_RESET:
            speed_sensor_reset_trip();
            voice_handler_speak("行程数据已重置，新的旅程开始啦！");
            break;

        case ASR_CMD_STATUS:
            /* 播报当前状态 */
            {
                driving_behavior_t beh = {0};
                imu_sensor_get_behavior(&beh);
                empathy_mood_t mood = empathy_engine_get_mood();
                voice_handler_speakf(
                    "当前速度%.0f公里，心情%s，行驶里程%.1f公里",
                    speed.speed_kmh,
                    empathy_mood_to_name(mood),
                    speed.distance_m / 1000.0f
                );
            }
            break;

        default:
            ESP_LOGW(TAG, "未知语音指令: 0x%02X", cmd);
            break;
    }
}

/* ============================================================
 * 传感器回调函数
 * ============================================================ */

static void on_speed_updated(const speed_data_t *data)
{
    driving_behavior_t behavior = {0};
    imu_sensor_get_behavior(&behavior);

    /* 更新共情引擎 */
    empathy_engine_update(data, &behavior);

    /* 更新UI */
    ui_manager_update_speed(data);
}

static void on_imu_updated(const driving_behavior_t *behavior)
{
    speed_data_t speed = {0};
    speed_sensor_get_data(&speed);
    empathy_engine_update(&speed, behavior);
}

/**
 * @brief 共情响应回调 - 同时更新UI和语音播报
 */
static void on_empathy_response(const empathy_response_t *response)
{
    /* 更新UI */
    ui_manager_update_empathy(response);

    /* 语音播报共情消息（仅在非播报状态时） */
    if (!voice_handler_is_speaking()) {
        voice_handler_speak(response->message);
    }

    ESP_LOGI(TAG, "共情: [%s] %s",
             empathy_mood_to_name(response->mood),
             response->message);
}

/* ============================================================
 * 系统监控任务
 * ============================================================ */

static void monitor_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        speed_data_t speed = {0};
        speed_sensor_get_data(&speed);
        empathy_mood_t mood = empathy_engine_get_mood();

        ESP_LOGI(TAG, "=== 系统状态 ===");
        ESP_LOGI(TAG, "速度: %.1f km/h [%s]",
                 speed.speed_kmh, speed.is_simulated ? "模拟" : "GPS");
        ESP_LOGI(TAG, "情感: %s %s",
                 empathy_mood_to_name(mood), empathy_mood_to_emoji(mood));
        ESP_LOGI(TAG, "语音: 唤醒=%s 播报=%s",
                 voice_handler_is_awake() ? "是" : "否",
                 voice_handler_is_speaking() ? "是" : "否");
        ESP_LOGI(TAG, "行程: 最高%.0f 均速%.0f 里程%.1fkm",
                 speed.max_speed_kmh, speed.avg_speed_kmh,
                 speed.distance_m / 1000.0f);
        ESP_LOGI(TAG, "堆内存: %lu bytes",
                 (unsigned long)esp_get_free_heap_size());
        ESP_LOGI(TAG, "================");
    }
}

/* ============================================================
 * 主函数
 * ============================================================ */

void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  CarPal - 驾驶陪伴小摆件 (语音版)");
    ESP_LOGI(TAG, "  ESP32-S3 + ST7789 + SYN6288 + HLK-V20");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "堆内存: %lu bytes",
             (unsigned long)esp_get_free_heap_size());

    /* ---- 1. 初始化显示屏 ---- */
    ESP_LOGI(TAG, "[1/5] 初始化显示屏...");
    esp_err_t ret = display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "显示屏初始化失败，重启");
        esp_restart();
    }

    /* ---- 2. 初始化UI ---- */
    ESP_LOGI(TAG, "[2/5] 初始化UI...");
    ret = ui_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UI初始化失败，重启");
        esp_restart();
    }
    ui_manager_show_notification("CarPal", "Initializing...", 0);
    vTaskDelay(pdMS_TO_TICKS(300));

    /* ---- 3. 初始化传感器 ---- */
    ESP_LOGI(TAG, "[3/5] 初始化传感器...");
    speed_sensor_init();
    speed_sensor_register_callback(on_speed_updated);

    imu_sensor_init();
    imu_sensor_register_callback(on_imu_updated);

    /* ---- 4. 初始化共情引擎 ---- */
    ESP_LOGI(TAG, "[4/5] 初始化共情引擎...");
    ret = empathy_engine_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "共情引擎初始化失败，重启");
        esp_restart();
    }
    empathy_engine_register_callback(on_empathy_response);

    /* ---- 5. 初始化语音模块 ---- */
    ESP_LOGI(TAG, "[5/5] 初始化语音模块...");
    ret = voice_handler_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "语音模块初始化失败，语音功能不可用");
    } else {
        voice_handler_register_asr_callback(on_asr_command);
    }

    /* ---- 初始化完成 ---- */
    ESP_LOGI(TAG, "所有模块初始化完成！");

    ui_manager_show_notification("CarPal Ready",
                                  "Say: Xiao Che Ban",
                                  4000);

    /* 显示欢迎情感 */
    empathy_response_t welcome = {
        .mood = MOOD_HAPPY,
        .event = EVENT_TRIP_START,
        .message = "CarPal Ready! Say: Xiao Che Ban",
        .emoji = "(^_^)",
        .display_duration_ms = 5000,
    };
    ui_manager_update_empathy(&welcome);

    /* 启动监控任务 */
    xTaskCreate(monitor_task, "monitor", 3072, NULL, 2, NULL);

    ESP_LOGI(TAG, "CarPal 运行中...");
    ESP_LOGI(TAG, "唤醒词: 小车伴");
    ESP_LOGI(TAG, "支持指令: 速度多少/我累了/堵车了/讲个笑话/状态报告...");

    /* 主循环：定期更新语音状态显示 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));
        ui_manager_update_voice_status(
            voice_handler_is_awake(),
            voice_handler_is_speaking()
        );
    }
}