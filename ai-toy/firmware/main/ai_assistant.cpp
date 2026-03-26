/**
 * @file ai_assistant.cpp
 * @brief AI助手核心状态机
 * 
 * 负责协调：唤醒检测 → 语音采集 → WebSocket传输 → TTS播放
 * 
 * 通信协议 (JSON over WebSocket):
 * 
 * 客户端 → 服务器:
 *   {"type": "hello", "device_id": "esp32s3_xxxx", "version": "1.0"}
 *   {"type": "listen_start"}
 *   {"type": "listen_stop"}
 *   {"type": "abort"}
 *   [binary] PCM音频数据 (16bit, 16kHz, 单声道)
 * 
 * 服务器 → 客户端:
 *   {"type": "hello", "session_id": "xxx"}
 *   {"type": "asr_result", "text": "识别的文字"}
 *   {"type": "llm_response", "text": "AI回复文字", "done": false}
 *   {"type": "tts_start", "sample_rate": 16000}
 *   [binary] TTS PCM音频数据
 *   {"type": "tts_end"}
 *   {"type": "error", "message": "错误信息"}
 */

#include "ai_assistant.h"
#include "app_config.h"
#include "audio_codec.h"
#include "websocket_client.h"
#include "led_indicator.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "AI";

// 状态机
static volatile ai_state_t s_state = AI_STATE_IDLE;

// 事件队列
static QueueHandle_t s_event_queue = NULL;

// 音频采集队列
static QueueHandle_t s_audio_queue = NULL;

// 任务句柄
static TaskHandle_t s_assistant_task_handle = NULL;
static TaskHandle_t s_vad_task_handle = NULL;

// VAD 状态
static volatile bool s_speech_detected = false;
static volatile uint32_t s_silence_start_ms = 0;
static volatile uint32_t s_speech_start_ms = 0;

// 设备ID
static char s_device_id[32] = {0};

// 会话ID
static char s_session_id[64] = {0};

// VAD 阈值 (RMS电平)
#define VAD_SPEECH_THRESHOLD    800     // 判定为有声音的阈值
#define VAD_SILENCE_THRESHOLD   400     // 判定为静音的阈值

// ============================================================
// 内部函数
// ============================================================

/**
 * @brief 生成设备ID (基于MAC地址)
 */
static void generate_device_id(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id),
             "esp32s3_%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "设备ID: %s", s_device_id);
}

/**
 * @brief 发送JSON消息到服务器
 */
static esp_err_t send_json(const char *type, cJSON *extra)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(root, "device_id", s_device_id);

    if (extra) {
        // 合并extra字段
        cJSON *item = extra->child;
        while (item) {
            cJSON *dup = cJSON_Duplicate(item, true);
            cJSON_AddItemToObject(root, item->string, dup);
            item = item->next;
        }
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) return ESP_ERR_NO_MEM;

    esp_err_t ret = ws_client_send_text(json_str);
    free(json_str);
    return ret;
}

/**
 * @brief 切换AI状态
 */
static void set_state(ai_state_t new_state)
{
    if (s_state == new_state) return;

    ESP_LOGI(TAG, "状态切换: %d -> %d", s_state, new_state);
    s_state = new_state;

    // 同步更新LED状态
    switch (new_state) {
    case AI_STATE_IDLE:
        led_indicator_set_state(LED_STATE_CONNECTED);
        break;
    case AI_STATE_WAKEUP:
        led_indicator_set_state(LED_STATE_WAKEUP);
        break;
    case AI_STATE_LISTENING:
        led_indicator_set_state(LED_STATE_LISTENING);
        break;
    case AI_STATE_PROCESSING:
        led_indicator_set_state(LED_STATE_PROCESSING);
        break;
    case AI_STATE_SPEAKING:
        led_indicator_set_state(LED_STATE_SPEAKING);
        break;
    case AI_STATE_ERROR:
        led_indicator_set_state(LED_STATE_ERROR);
        break;
    default:
        break;
    }
}

/**
 * @brief 处理服务器发来的JSON消息
 */
static void handle_server_message(const char *json_str, size_t len)
{
    // 确保字符串以null结尾
    char *buf = (char *)malloc(len + 1);
    if (!buf) return;
    memcpy(buf, json_str, len);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        ESP_LOGW(TAG, "JSON解析失败");
        return;
    }

    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    if (!type_item || !cJSON_IsString(type_item)) {
        cJSON_Delete(root);
        return;
    }

    const char *type = type_item->valuestring;
    ESP_LOGI(TAG, "收到服务器消息: type=%s", type);

    if (strcmp(type, "hello") == 0) {
        // 握手响应
        cJSON *session = cJSON_GetObjectItem(root, "session_id");
        if (session && cJSON_IsString(session)) {
            strncpy(s_session_id, session->valuestring, sizeof(s_session_id) - 1);
            ESP_LOGI(TAG, "会话ID: %s", s_session_id);
        }

    } else if (strcmp(type, "asr_result") == 0) {
        // 语音识别结果
        cJSON *text = cJSON_GetObjectItem(root, "text");
        if (text && cJSON_IsString(text)) {
            ESP_LOGI(TAG, "识别结果: %s", text->valuestring);
        }

    } else if (strcmp(type, "llm_response") == 0) {
        // LLM回复 (流式)
        cJSON *text = cJSON_GetObjectItem(root, "text");
        cJSON *done = cJSON_GetObjectItem(root, "done");
        if (text && cJSON_IsString(text)) {
            ESP_LOGI(TAG, "AI回复: %s%s", text->valuestring,
                     (done && cJSON_IsTrue(done)) ? " [完成]" : "");
        }

    } else if (strcmp(type, "tts_start") == 0) {
        // TTS开始
        ESP_LOGI(TAG, "TTS开始播放");
        set_state(AI_STATE_SPEAKING);

    } else if (strcmp(type, "tts_end") == 0) {
        // TTS结束
        ESP_LOGI(TAG, "TTS播放完成");
        // 等待音频播放完毕后回到空闲
        vTaskDelay(pdMS_TO_TICKS(500));
        if (s_state == AI_STATE_SPEAKING) {
            set_state(AI_STATE_IDLE);
        }

    } else if (strcmp(type, "error") == 0) {
        // 错误
        cJSON *msg = cJSON_GetObjectItem(root, "message");
        ESP_LOGE(TAG, "服务器错误: %s",
                 (msg && cJSON_IsString(msg)) ? msg->valuestring : "未知错误");
        set_state(AI_STATE_ERROR);
        vTaskDelay(pdMS_TO_TICKS(2000));
        set_state(AI_STATE_IDLE);
    }

    cJSON_Delete(root);
}

/**
 * @brief WebSocket事件回调
 */
static void ws_event_callback(ws_status_t status, const ws_message_t *msg)
{
    switch (status) {
    case WS_STATUS_CONNECTED:
        if (msg == NULL) {
            // 连接建立，发送握手
            ESP_LOGI(TAG, "WebSocket连接成功，发送握手");
            cJSON *extra = cJSON_CreateObject();
            cJSON_AddStringToObject(extra, "version", "1.0");
            send_json("hello", extra);
            cJSON_Delete(extra);
        } else {
            // 收到数据
            if (msg->type == WS_MSG_TEXT) {
                handle_server_message((const char *)msg->data, msg->size);
            } else if (msg->type == WS_MSG_BINARY) {
                // 收到TTS音频数据，直接播放
                if (s_state == AI_STATE_SPEAKING || s_state == AI_STATE_PROCESSING) {
                    audio_codec_play(msg->data, msg->size);
                }
            }
        }
        break;

    case WS_STATUS_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket断开，尝试重连...");
        if (s_state != AI_STATE_IDLE) {
            set_state(AI_STATE_IDLE);
        }
        led_indicator_set_state(LED_STATE_CONNECTING);
        break;

    case WS_STATUS_ERROR:
        ESP_LOGE(TAG, "WebSocket错误");
        set_state(AI_STATE_ERROR);
        break;

    default:
        break;
    }
}

/**
 * @brief VAD (语音活动检测) 任务
 * 
 * 持续监测音频电平，检测语音开始和结束
 */
static void vad_task(void *arg)
{
    ESP_LOGI(TAG, "VAD任务启动");

    uint32_t now_ms;
    bool was_speaking = false;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(20));  // 20ms检测间隔

        if (s_state != AI_STATE_LISTENING) {
            was_speaking = false;
            s_speech_detected = false;
            continue;
        }

        now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        int16_t level = audio_codec_get_level();

        if (level > VAD_SPEECH_THRESHOLD) {
            // 检测到声音
            if (!was_speaking) {
                ESP_LOGD(TAG, "VAD: 检测到语音 (level=%d)", level);
                was_speaking = true;
                s_speech_detected = true;
                s_speech_start_ms = now_ms;
            }
            s_silence_start_ms = now_ms;  // 重置静音计时

        } else if (was_speaking && level < VAD_SILENCE_THRESHOLD) {
            // 静音检测
            uint32_t silence_duration = now_ms - s_silence_start_ms;

            if (silence_duration >= VAD_SILENCE_MS) {
                // 静音超时，检查是否有足够长的语音
                uint32_t speech_duration = s_silence_start_ms - s_speech_start_ms;

                if (speech_duration >= VAD_MIN_SPEECH_MS) {
                    ESP_LOGI(TAG, "VAD: 语音结束 (时长=%dms)", speech_duration);
                    was_speaking = false;
                    s_speech_detected = false;

                    // 发送语音结束事件
                    ai_event_t event = AI_EVENT_SPEECH_END;
                    xQueueSend(s_event_queue, &event, 0);
                } else {
                    ESP_LOGD(TAG, "VAD: 语音太短，忽略 (%dms)", speech_duration);
                    was_speaking = false;
                    s_speech_detected = false;
                }
            }
        }
    }
}

/**
 * @brief 音频转发任务 (录音时将音频发送到服务器)
 */
static void audio_forward_task(void *arg)
{
    ESP_LOGI(TAG, "音频转发任务启动");

    audio_packet_t packet;

    while (1) {
        if (s_state != AI_STATE_LISTENING) {
            // 清空队列
            while (xQueueReceive(s_audio_queue, &packet, 0) == pdTRUE) {
                free(packet.data);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (xQueueReceive(s_audio_queue, &packet, pdMS_TO_TICKS(50)) == pdTRUE) {
            // 发送音频数据到服务器
            if (ws_client_get_status() == WS_STATUS_CONNECTED) {
                ws_client_send_binary(packet.data, packet.size);
            }
            free(packet.data);
        }
    }
}

/**
 * @brief AI助手主任务 (状态机)
 */
static void ai_assistant_task(void *arg)
{
    ESP_LOGI(TAG, "AI助手任务启动");

    ai_event_t event;

    while (1) {
        // 等待事件
        if (xQueueReceive(s_event_queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        ESP_LOGI(TAG, "收到事件: %d (当前状态: %d)", event, s_state);

        switch (event) {
        case AI_EVENT_WAKEUP:
            if (s_state == AI_STATE_IDLE) {
                // 唤醒：开始录音
                ESP_LOGI(TAG, "唤醒！开始录音...");
                set_state(AI_STATE_WAKEUP);

                // 短暂唤醒动画
                vTaskDelay(pdMS_TO_TICKS(300));

                // 通知服务器开始监听
                send_json("listen_start", NULL);

                // 开始音频采集
                audio_codec_start_capture();
                set_state(AI_STATE_LISTENING);

            } else if (s_state == AI_STATE_SPEAKING) {
                // 打断当前播放
                ESP_LOGI(TAG, "打断当前播放");
                audio_codec_stop_play();
                send_json("abort", NULL);
                set_state(AI_STATE_IDLE);
            }
            break;

        case AI_EVENT_SPEECH_END:
            if (s_state == AI_STATE_LISTENING) {
                ESP_LOGI(TAG, "语音结束，停止录音，等待AI回复...");

                // 停止采集
                audio_codec_stop_capture();

                // 通知服务器停止监听
                send_json("listen_stop", NULL);

                set_state(AI_STATE_PROCESSING);
            }
            break;

        case AI_EVENT_INTERRUPT:
            if (s_state == AI_STATE_SPEAKING || s_state == AI_STATE_PROCESSING) {
                ESP_LOGI(TAG, "打断AI回复");
                audio_codec_stop_play();
                send_json("abort", NULL);
                set_state(AI_STATE_IDLE);
            } else if (s_state == AI_STATE_LISTENING) {
                // 取消录音
                audio_codec_stop_capture();
                send_json("abort", NULL);
                set_state(AI_STATE_IDLE);
            }
            break;

        case AI_EVENT_RESET:
            ESP_LOGI(TAG, "重置AI助手");
            audio_codec_stop_capture();
            audio_codec_stop_play();
            if (s_state != AI_STATE_IDLE) {
                send_json("abort", NULL);
            }
            set_state(AI_STATE_IDLE);
            break;

        default:
            break;
        }
    }
}

// ============================================================
// 公共接口实现
// ============================================================

esp_err_t ai_assistant_init(void)
{
    ESP_LOGI(TAG, "初始化AI助手");

    // 生成设备ID
    generate_device_id();

    // 创建事件队列
    s_event_queue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(ai_event_t));
    if (!s_event_queue) {
        ESP_LOGE(TAG, "创建事件队列失败");
        return ESP_ERR_NO_MEM;
    }

    // 创建音频采集队列
    s_audio_queue = xQueueCreate(AUDIO_QUEUE_SIZE, sizeof(audio_packet_t));
    if (!s_audio_queue) {
        ESP_LOGE(TAG, "创建音频队列失败");
        return ESP_ERR_NO_MEM;
    }

    // 初始化音频编解码器
    esp_err_t ret = audio_codec_init(s_audio_queue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化音频编解码器失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化WebSocket客户端
    ret = ws_client_init(SERVER_WS_URI, ws_event_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化WebSocket客户端失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 连接WebSocket服务器
    ret = ws_client_connect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WebSocket连接启动失败 (将自动重试): %s", esp_err_to_name(ret));
    }

    // 创建VAD任务
    xTaskCreatePinnedToCore(vad_task, "vad_task", 2048, NULL,
                             TASK_PRIORITY_AI_ASSISTANT, &s_vad_task_handle, 0);

    // 创建音频转发任务
    TaskHandle_t fwd_handle;
    xTaskCreatePinnedToCore(audio_forward_task, "audio_fwd", 4096, NULL,
                             TASK_PRIORITY_WEBSOCKET, &fwd_handle, 0);

    // 创建AI助手主任务
    xTaskCreatePinnedToCore(ai_assistant_task, "ai_task", TASK_STACK_AI_ASSISTANT, NULL,
                             TASK_PRIORITY_AI_ASSISTANT, &s_assistant_task_handle, 0);

    set_state(AI_STATE_IDLE);

    ESP_LOGI(TAG, "AI助手初始化完成");
    ESP_LOGI(TAG, "  服务器: %s", SERVER_WS_URI);
    ESP_LOGI(TAG, "  设备ID: %s", s_device_id);

    return ESP_OK;
}

void ai_assistant_send_event(ai_event_t event)
{
    if (s_event_queue) {
        if (xQueueSend(s_event_queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "事件队列满，丢弃事件: %d", event);
        }
    }
}

ai_state_t ai_assistant_get_state(void)
{
    return s_state;
}

void ai_assistant_deinit(void)
{
    if (s_assistant_task_handle) {
        vTaskDelete(s_assistant_task_handle);
        s_assistant_task_handle = NULL;
    }
    if (s_vad_task_handle) {
        vTaskDelete(s_vad_task_handle);
        s_vad_task_handle = NULL;
    }

    ws_client_deinit();
    audio_codec_deinit();

    if (s_event_queue) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }
    if (s_audio_queue) {
        audio_packet_t packet;
        while (xQueueReceive(s_audio_queue, &packet, 0) == pdTRUE) {
            free(packet.data);
        }
        vQueueDelete(s_audio_queue);
        s_audio_queue = NULL;
    }

    ESP_LOGI(TAG, "AI助手已释放");
}