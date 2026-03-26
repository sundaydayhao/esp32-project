/**
 * 故事播放器模块
 * 处理服务器消息，协调显示和音频播放
 *
 * 消息处理流程:
 * display     -> 更新屏幕显示
 * illustration -> 显示故事插图
 * story_text  -> 追加故事文字到屏幕
 * audio_chunk -> 解码并播放音频
 * story_complete -> 通知主任务故事结束
 * error       -> 显示错误信息
 */

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "cJSON.h"

#include "config.h"
#include "story_player.h"
#include "display.h"
#include "audio_output.h"

static const char *TAG = "STORY_PLAYER";

static QueueHandle_t s_msg_queue = NULL;
static bool s_is_playing = false;

// 音频播放队列
#define AUDIO_QUEUE_SIZE    8
#define MAX_AUDIO_CHUNK     (32 * 1024)  // 32KB 每块

typedef struct {
    uint8_t *data;
    size_t   len;
    int      index;
    bool     is_last;
} audio_chunk_t;

static QueueHandle_t s_audio_queue = NULL;
static TaskHandle_t  s_audio_task_handle = NULL;

/**
 * 音频播放任务
 */
static void audio_play_task(void *pvParameters) {
    audio_chunk_t chunk;

    while (1) {
        if (xQueueReceive(s_audio_queue, &chunk, pdMS_TO_TICKS(5000)) == pdTRUE) {
            if (chunk.data && chunk.len > 0) {
                ESP_LOGI(TAG, "播放音频块 #%d，大小: %d bytes", chunk.index, chunk.len);

                // 播放 MP3 音频
                audio_output_play_mp3(chunk.data, chunk.len);

                free(chunk.data);
                chunk.data = NULL;

                if (chunk.is_last) {
                    ESP_LOGI(TAG, "最后一个音频块播放完成");
                    s_is_playing = false;

                    // 通知主任务故事完成
                    if (s_msg_queue) {
                        device_message_t msg = {
                            .type = MSG_STORY_COMPLETE,
                            .data = NULL,
                            .data_len = 0
                        };
                        xQueueSend(s_msg_queue, &msg, pdMS_TO_TICKS(100));
                    }
                }
            }
        }
    }
}

esp_err_t story_player_init(QueueHandle_t msg_queue) {
    s_msg_queue = msg_queue;

    // 创建音频队列
    s_audio_queue = xQueueCreate(AUDIO_QUEUE_SIZE, sizeof(audio_chunk_t));
    if (!s_audio_queue) {
        ESP_LOGE(TAG, "创建音频队列失败");
        return ESP_ERR_NO_MEM;
    }

    // 创建音频播放任务
    BaseType_t ret = xTaskCreate(
        audio_play_task,
        "audio_play",
        TASK_AUDIO_OUT_STACK,
        NULL,
        TASK_AUDIO_OUT_PRIORITY,
        &s_audio_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建音频播放任务失败");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "故事播放器初始化完成");
    return ESP_OK;
}

/**
 * 处理 display 消息
 */
static void handle_display_message(cJSON *root) {
    cJSON *screen = cJSON_GetObjectItem(root, "screen");
    if (!screen || !cJSON_IsString(screen)) return;

    const char *screen_name = screen->valuestring;

    if (strcmp(screen_name, "welcome") == 0) {
        display_show_welcome();
    } else if (strcmp(screen_name, "listening") == 0) {
        display_show_listening();
    } else if (strcmp(screen_name, "generating") == 0) {
        cJSON *subtext = cJSON_GetObjectItem(root, "subtext");
        const char *keywords = subtext ? subtext->valuestring : "...";
        display_show_status("AI 正在创作故事", keywords, DISPLAY_ACCENT_COLOR);
    } else if (strcmp(screen_name, "recognizing") == 0) {
        display_show_processing();
    } else if (strcmp(screen_name, "sleeping") == 0) {
        display_show_sleeping();
    }
}

/**
 * 处理 illustration 消息（显示插图）
 */
static void handle_illustration_message(cJSON *root) {
    cJSON *image_b64 = cJSON_GetObjectItem(root, "image");
    cJSON *theme = cJSON_GetObjectItem(root, "theme");

    if (!image_b64 || !cJSON_IsString(image_b64)) {
        ESP_LOGW(TAG, "插图数据无效");
        return;
    }

    const char *b64_str = image_b64->valuestring;
    size_t b64_len = strlen(b64_str);

    // 计算解码后大小
    size_t decoded_len = (b64_len / 4) * 3 + 4;
    uint8_t *decoded = (uint8_t *)malloc(decoded_len);
    if (!decoded) {
        ESP_LOGE(TAG, "分配插图缓冲区失败");
        return;
    }

    size_t out_len = 0;
    int ret = mbedtls_base64_decode(
        decoded, decoded_len, &out_len,
        (const unsigned char *)b64_str, b64_len
    );

    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 解码失败: %d", ret);
        free(decoded);
        return;
    }

    ESP_LOGI(TAG, "显示插图，主题: %s，大小: %d bytes",
             theme ? theme->valuestring : "unknown", out_len);

    // 显示 PNG 图片
    // 注意：直接显示 PNG 需要 PNG 解码器
    // 简化方案：后端发送 RGB565 格式数据
    // 这里假设收到的是 RGB565 数据
    display_show_image(decoded, out_len, TFT_WIDTH, TFT_HEIGHT);

    free(decoded);
}

/**
 * 处理 story_text 消息
 */
static void handle_story_text_message(cJSON *root) {
    cJSON *text = cJSON_GetObjectItem(root, "text");
    cJSON *is_last = cJSON_GetObjectItem(root, "is_last");

    if (text && cJSON_IsString(text) && strlen(text->valuestring) > 0) {
        display_append_story_text(text->valuestring);
    }

    if (is_last && cJSON_IsTrue(is_last)) {
        ESP_LOGI(TAG, "故事文字接收完成");
    }
}

/**
 * 处理 audio_chunk 消息
 */
static void handle_audio_chunk_message(cJSON *root) {
    cJSON *data_b64 = cJSON_GetObjectItem(root, "data");
    cJSON *index = cJSON_GetObjectItem(root, "index");
    cJSON *is_last = cJSON_GetObjectItem(root, "is_last");

    if (!data_b64 || !cJSON_IsString(data_b64)) {
        ESP_LOGW(TAG, "音频数据无效");
        return;
    }

    const char *b64_str = data_b64->valuestring;
    size_t b64_len = strlen(b64_str);

    // 解码 Base64
    size_t decoded_len = (b64_len / 4) * 3 + 4;
    uint8_t *decoded = (uint8_t *)malloc(decoded_len);
    if (!decoded) {
        ESP_LOGE(TAG, "分配音频缓冲区失败，大小: %d", decoded_len);
        return;
    }

    size_t out_len = 0;
    int ret = mbedtls_base64_decode(
        decoded, decoded_len, &out_len,
        (const unsigned char *)b64_str, b64_len
    );

    if (ret != 0) {
        ESP_LOGE(TAG, "音频 Base64 解码失败: %d", ret);
        free(decoded);
        return;
    }

    // 加入播放队列
    audio_chunk_t chunk = {
        .data = decoded,
        .len = out_len,
        .index = index ? (int)index->valuedouble : 0,
        .is_last = is_last ? cJSON_IsTrue(is_last) : false
    };

    if (xQueueSend(s_audio_queue, &chunk, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "音频队列已满，丢弃音频块 #%d", chunk.index);
        free(decoded);
    } else {
        ESP_LOGI(TAG, "音频块 #%d 加入队列，大小: %d bytes", chunk.index, out_len);
        s_is_playing = true;
    }
}

/**
 * 处理 story_complete 消息
 */
static void handle_story_complete_message(cJSON *root) {
    cJSON *keywords = cJSON_GetObjectItem(root, "keywords");
    cJSON *story_length = cJSON_GetObjectItem(root, "story_length");

    ESP_LOGI(TAG, "故事生成完成，长度: %d 字",
             story_length ? (int)story_length->valuedouble : 0);

    // 如果没有音频队列中的数据，直接通知完成
    if (uxQueueMessagesWaiting(s_audio_queue) == 0 && !s_is_playing) {
        if (s_msg_queue) {
            device_message_t msg = {
                .type = MSG_STORY_COMPLETE,
                .data = NULL,
                .data_len = 0
            };
            xQueueSend(s_msg_queue, &msg, pdMS_TO_TICKS(100));
        }
    }
}

/**
 * 处理 error 消息
 */
static void handle_error_message(cJSON *root) {
    cJSON *message = cJSON_GetObjectItem(root, "message");
    const char *err_msg = message ? message->valuestring : "未知错误";

    ESP_LOGE(TAG, "服务器错误: %s", err_msg);
    display_show_error("服务器错误", err_msg);

    s_is_playing = false;

    if (s_msg_queue) {
        device_message_t msg = {
            .type = MSG_ERROR,
            .data = NULL,
            .data_len = 0
        };
        xQueueSend(s_msg_queue, &msg, pdMS_TO_TICKS(100));
    }
}

void story_player_handle_message(const char *data, size_t len) {
    if (!data || len == 0) return;

    // 解析 JSON
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGE(TAG, "JSON 解析失败: %.50s", data);
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!type || !cJSON_IsString(type)) {
        ESP_LOGW(TAG, "消息缺少 type 字段");
        cJSON_Delete(root);
        return;
    }

    const char *msg_type = type->valuestring;
    ESP_LOGD(TAG, "收到消息类型: %s", msg_type);

    if (strcmp(msg_type, "display") == 0) {
        handle_display_message(root);
    } else if (strcmp(msg_type, "illustration") == 0) {
        handle_illustration_message(root);
    } else if (strcmp(msg_type, "story_text") == 0) {
        handle_story_text_message(root);
    } else if (strcmp(msg_type, "audio_chunk") == 0) {
        handle_audio_chunk_message(root);
    } else if (strcmp(msg_type, "story_complete") == 0) {
        handle_story_complete_message(root);
    } else if (strcmp(msg_type, "error") == 0) {
        handle_error_message(root);
    } else if (strcmp(msg_type, "pong") == 0) {
        ESP_LOGD(TAG, "收到 pong");
    } else if (strcmp(msg_type, "vad_result") == 0) {
        cJSON *has_voice = cJSON_GetObjectItem(root, "has_voice");
        if (has_voice && !cJSON_IsTrue(has_voice)) {
            ESP_LOGW(TAG, "未检测到语音");
            display_show_status("未检测到声音", "请重试", DISPLAY_ACCENT_COLOR);
        }
    } else if (strcmp(msg_type, "no_keywords") == 0) {
        cJSON *text = cJSON_GetObjectItem(root, "text");
        ESP_LOGW(TAG, "未识别关键词: %s", text ? text->valuestring : "");
        display_show_status("未识别关键词", "请说恐龙/太空/公主", DISPLAY_ACCENT_COLOR);
    } else {
        ESP_LOGD(TAG, "未处理的消息类型: %s", msg_type);
    }

    cJSON_Delete(root);
}

void story_player_stop(void) {
    s_is_playing = false;
    audio_output_stop();

    // 清空音频队列
    audio_chunk_t chunk;
    while (xQueueReceive(s_audio_queue, &chunk, 0) == pdTRUE) {
        if (chunk.data) {
            free(chunk.data);
        }
    }

    ESP_LOGI(TAG, "故事播放已停止");
}

bool story_player_is_playing(void) {
    return s_is_playing;
}

void story_player_deinit(void) {
    story_player_stop();

    if (s_audio_task_handle) {
        vTaskDelete(s_audio_task_handle);
        s_audio_task_handle = NULL;
    }

    if (s_audio_queue) {
        vQueueDelete(s_audio_queue);
        s_audio_queue = NULL;
    }
}