/**
 * @file openai_client.c
 * @brief OpenAI API client: Whisper STT, GPT-4 teacher chat, TTS
 *
 * Pipeline:
 *   WAV audio -> Whisper API -> transcript
 *   transcript -> GPT API (teacher persona) -> JSON {response, correction, score, encouragement}
 *   response text -> TTS API -> PCM audio (24kHz, 16-bit, mono)
 *
 * System prompt makes GPT act as a warm, encouraging English foreign teacher
 * for children aged 5-10, providing pronunciation scoring and gentle corrections.
 */

#include "openai_client.h"
#include "sdkconfig.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "openai_client";

/* OpenAI API endpoints */
#define OPENAI_WHISPER_URL  "https://api.openai.com/v1/audio/transcriptions"
#define OPENAI_CHAT_URL     "https://api.openai.com/v1/chat/completions"
#define OPENAI_TTS_URL      "https://api.openai.com/v1/audio/speech"

/* Multipart boundary for Whisper upload */
#define MULTIPART_BOUNDARY  "----ESP32AudioBoundary7a3f9b"

/* HTTP response accumulation buffer */
#define HTTP_RESP_BUF_SIZE  4096

/* ------------------------------------------------------------------ */
/*  HTTP Response Accumulator                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   capacity;
    bool     is_psram;
} http_resp_t;

static esp_err_t http_resp_init(http_resp_t *r, size_t initial_cap, bool use_psram)
{
    r->is_psram = use_psram;
    r->capacity = initial_cap;
    r->len      = 0;

    if (use_psram) {
        r->buf = heap_caps_malloc(initial_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    } else {
        r->buf = malloc(initial_cap);
    }

    if (!r->buf) {
        ESP_LOGE(TAG, "Failed to allocate HTTP response buffer (%u bytes)", (unsigned)initial_cap);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t http_resp_append(http_resp_t *r, const uint8_t *data, size_t data_len)
{
    if (r->len + data_len > r->capacity) {
        /* Grow buffer */
        size_t new_cap = r->capacity * 2;
        while (new_cap < r->len + data_len) new_cap *= 2;

        uint8_t *new_buf;
        if (r->is_psram) {
            new_buf = heap_caps_realloc(r->buf, new_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        } else {
            new_buf = realloc(r->buf, new_cap);
        }

        if (!new_buf) {
            ESP_LOGE(TAG, "Failed to grow HTTP response buffer to %u bytes", (unsigned)new_cap);
            return ESP_ERR_NO_MEM;
        }
        r->buf      = new_buf;
        r->capacity = new_cap;
    }

    memcpy(r->buf + r->len, data, data_len);
    r->len += data_len;
    return ESP_OK;
}

static void http_resp_free(http_resp_t *r)
{
    if (r->buf) {
        if (r->is_psram) {
            heap_caps_free(r->buf);
        } else {
            free(r->buf);
        }
        r->buf = NULL;
        r->len = 0;
    }
}

/* ------------------------------------------------------------------ */
/*  HTTP Event Handler                                                  */
/* ------------------------------------------------------------------ */

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_resp_t *resp = (http_resp_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (resp && evt->data_len > 0) {
                http_resp_append(resp, (const uint8_t *)evt->data, evt->data_len);
            }
            break;
        case HTTP_EVENT_ERROR:
            ESP_LOGW(TAG, "HTTP event error");
            break;
        default:
            break;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  GPT System Prompt                                                   */
/* ------------------------------------------------------------------ */

static const char *TEACHER_SYSTEM_PROMPT =
    "You are Miss Lily, a warm, enthusiastic, and encouraging English foreign teacher "
    "for young children aged 5-10 years old. Your personality is fun, patient, and very positive.\n\n"
    "When a child speaks to you:\n"
    "1. Respond naturally and warmly to what they said\n"
    "2. Gently correct any pronunciation or grammar mistakes (if any)\n"
    "3. Give a pronunciation score from 1 to 10 (be generous and encouraging)\n"
    "4. Always end with enthusiastic encouragement\n\n"
    "IMPORTANT RULES:\n"
    "- Use VERY simple vocabulary suitable for young children\n"
    "- Keep sentences SHORT (max 15 words each)\n"
    "- Be VERY positive and encouraging, never discouraging\n"
    "- If pronunciation was good, celebrate it!\n"
    "- If there were mistakes, correct them gently with examples\n\n"
    "You MUST respond in this exact JSON format (no markdown, pure JSON):\n"
    "{\n"
    "  \"response\": \"Your friendly reply to what the child said\",\n"
    "  \"correction\": \"Gentle correction if needed, or empty string if perfect\",\n"
    "  \"score\": 8,\n"
    "  \"encouragement\": \"Enthusiastic praise message\"\n"
    "}";

/* ------------------------------------------------------------------ */
/*  Public API: Init / Deinit                                           */
/* ------------------------------------------------------------------ */

esp_err_t openai_client_init(void)
{
    ESP_LOGI(TAG, "OpenAI client initialized (model: %s, voice: %s)",
             CONFIG_OPENAI_GPT_MODEL, CONFIG_OPENAI_TTS_VOICE);
    return ESP_OK;
}

void openai_client_deinit(void)
{
    ESP_LOGI(TAG, "OpenAI client deinitialized");
}

/* ------------------------------------------------------------------ */
/*  Whisper STT                                                         */
/* ------------------------------------------------------------------ */

esp_err_t openai_whisper_transcribe(const uint8_t *wav_data, size_t wav_size,
                                     char *transcript, size_t transcript_len)
{
    ESP_LOGI(TAG, "Whisper: transcribing %u bytes of audio...", (unsigned)wav_size);

    /* Build Authorization header */
    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", CONFIG_OPENAI_API_KEY);

    /* Build multipart/form-data body:
     *   --boundary\r\n
     *   Content-Disposition: form-data; name="file"; filename="audio.wav"\r\n
     *   Content-Type: audio/wav\r\n
     *   \r\n
     *   [WAV binary data]
     *   \r\n
     *   --boundary\r\n
     *   Content-Disposition: form-data; name="model"\r\n
     *   \r\n
     *   whisper-1\r\n
     *   --boundary\r\n
     *   Content-Disposition: form-data; name="language"\r\n
     *   \r\n
     *   en\r\n
     *   --boundary--\r\n
     */
    const char *part1_fmt =
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n";

    const char *part2 =
        "\r\n"
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n"
        "\r\n"
        "whisper-1\r\n"
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"language\"\r\n"
        "\r\n"
        "en\r\n"
        "--" MULTIPART_BOUNDARY "--\r\n";

    size_t part1_len = strlen(part1_fmt);
    size_t part2_len = strlen(part2);
    size_t total_len = part1_len + wav_size + part2_len;

    /* Allocate multipart body in PSRAM */
    uint8_t *body = heap_caps_malloc(total_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) {
        ESP_LOGE(TAG, "Failed to allocate multipart body (%u bytes)", (unsigned)total_len);
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;
    memcpy(body + offset, part1_fmt, part1_len); offset += part1_len;
    memcpy(body + offset, wav_data,  wav_size);  offset += wav_size;
    memcpy(body + offset, part2,     part2_len); offset += part2_len;

    /* Prepare response accumulator */
    http_resp_t resp = {0};
    if (http_resp_init(&resp, HTTP_RESP_BUF_SIZE, false) != ESP_OK) {
        heap_caps_free(body);
        return ESP_ERR_NO_MEM;
    }

    /* Configure HTTP client */
    esp_http_client_config_t config = {
        .url                    = OPENAI_WHISPER_URL,
        .method                 = HTTP_METHOD_POST,
        .event_handler          = http_event_handler,
        .user_data              = &resp,
        .transport_type         = HTTP_TRANSPORT_OVER_SSL,
        .skip_cert_common_name_check = true,
        .timeout_ms             = 30000,
        .buffer_size            = 2048,
        .buffer_size_tx         = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        http_resp_free(&resp);
        heap_caps_free(body);
        return ESP_FAIL;
    }

    /* Set headers */
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type",
                                "multipart/form-data; boundary=" MULTIPART_BOUNDARY);

    /* Set POST body */
    esp_http_client_set_post_field(client, (const char *)body, (int)total_len);

    /* Perform request */
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);
    heap_caps_free(body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Whisper HTTP request failed: %s", esp_err_to_name(err));
        http_resp_free(&resp);
        return ESP_FAIL;
    }

    if (status != 200) {
        ESP_LOGE(TAG, "Whisper API error (HTTP %d): %.*s", status,
                 (int)resp.len, (char *)resp.buf);
        http_resp_free(&resp);
        return ESP_FAIL;
    }

    /* Null-terminate response */
    http_resp_append(&resp, (const uint8_t *)"\0", 1);

    /* Parse JSON response: {"text": "..."} */
    cJSON *root = cJSON_Parse((const char *)resp.buf);
    http_resp_free(&resp);

    if (!root) {
        ESP_LOGE(TAG, "Failed to parse Whisper JSON response");
        return ESP_FAIL;
    }

    cJSON *text_item = cJSON_GetObjectItem(root, "text");
    if (!text_item || !cJSON_IsString(text_item)) {
        ESP_LOGE(TAG, "Whisper response missing 'text' field");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strncpy(transcript, text_item->valuestring, transcript_len - 1);
    transcript[transcript_len - 1] = '\0';
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Whisper transcript: \"%s\"", transcript);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  GPT Teacher Chat                                                    */
/* ------------------------------------------------------------------ */

esp_err_t openai_gpt_teacher_chat(const char *transcript,
                                   openai_teacher_response_t *response)
{
    ESP_LOGI(TAG, "GPT: processing transcript: \"%s\"", transcript);

    /* Build JSON request body */
    cJSON *root    = cJSON_CreateObject();
    cJSON *messages = cJSON_CreateArray();

    /* System message */
    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", TEACHER_SYSTEM_PROMPT);
    cJSON_AddItemToArray(messages, sys_msg);

    /* User message */
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", transcript);
    cJSON_AddItemToArray(messages, user_msg);

    cJSON_AddStringToObject(root, "model", CONFIG_OPENAI_GPT_MODEL);
    cJSON_AddItemToObject(root, "messages", messages);
    cJSON_AddNumberToObject(root, "temperature", 0.7);
    cJSON_AddNumberToObject(root, "max_tokens", 300);

    /* Response format: JSON object */
    cJSON *resp_format = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_format, "type", "json_object");
    cJSON_AddItemToObject(root, "response_format", resp_format);

    char *body_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!body_str) {
        ESP_LOGE(TAG, "Failed to serialize GPT request JSON");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "GPT request: %s", body_str);

    /* Build Authorization header */
    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", CONFIG_OPENAI_API_KEY);

    /* Prepare response accumulator */
    http_resp_t resp = {0};
    if (http_resp_init(&resp, HTTP_RESP_BUF_SIZE, false) != ESP_OK) {
        free(body_str);
        return ESP_ERR_NO_MEM;
    }

    /* Configure HTTP client */
    esp_http_client_config_t config = {
        .url                    = OPENAI_CHAT_URL,
        .method                 = HTTP_METHOD_POST,
        .event_handler          = http_event_handler,
        .user_data              = &resp,
        .transport_type         = HTTP_TRANSPORT_OVER_SSL,
        .skip_cert_common_name_check = true,
        .timeout_ms             = 30000,
        .buffer_size            = 2048,
        .buffer_size_tx         = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(body_str);
        http_resp_free(&resp);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body_str, (int)strlen(body_str));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);
    free(body_str);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "GPT API error (HTTP %d): %s", status, esp_err_to_name(err));
        if (resp.len > 0) {
            http_resp_append(&resp, (const uint8_t *)"\0", 1);
            ESP_LOGE(TAG, "Response: %s", (char *)resp.buf);
        }
        http_resp_free(&resp);
        return ESP_FAIL;
    }

    /* Null-terminate */
    http_resp_append(&resp, (const uint8_t *)"\0", 1);

    /* Parse outer GPT response: choices[0].message.content */
    cJSON *outer = cJSON_Parse((const char *)resp.buf);
    http_resp_free(&resp);

    if (!outer) {
        ESP_LOGE(TAG, "Failed to parse GPT outer JSON");
        return ESP_FAIL;
    }

    cJSON *choices = cJSON_GetObjectItem(outer, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        ESP_LOGE(TAG, "GPT response missing choices");
        cJSON_Delete(outer);
        return ESP_FAIL;
    }

    cJSON *choice0  = cJSON_GetArrayItem(choices, 0);
    cJSON *message  = cJSON_GetObjectItem(choice0, "message");
    cJSON *content  = cJSON_GetObjectItem(message, "content");

    if (!content || !cJSON_IsString(content)) {
        ESP_LOGE(TAG, "GPT response missing content");
        cJSON_Delete(outer);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "GPT content: %s", content->valuestring);

    /* Parse inner teacher JSON */
    cJSON *inner = cJSON_Parse(content->valuestring);
    cJSON_Delete(outer);

    if (!inner) {
        ESP_LOGE(TAG, "Failed to parse teacher JSON from GPT content");
        return ESP_FAIL;
    }

    /* Extract fields with safe defaults */
    memset(response, 0, sizeof(*response));

    cJSON *j_response     = cJSON_GetObjectItem(inner, "response");
    cJSON *j_correction   = cJSON_GetObjectItem(inner, "correction");
    cJSON *j_score        = cJSON_GetObjectItem(inner, "score");
    cJSON *j_encouragement = cJSON_GetObjectItem(inner, "encouragement");

    if (j_response && cJSON_IsString(j_response)) {
        strncpy(response->response, j_response->valuestring, sizeof(response->response) - 1);
    } else {
        strncpy(response->response, "That was great! Keep going!", sizeof(response->response) - 1);
    }

    if (j_correction && cJSON_IsString(j_correction)) {
        strncpy(response->correction, j_correction->valuestring, sizeof(response->correction) - 1);
    }

    if (j_score && cJSON_IsNumber(j_score)) {
        response->score = (int)j_score->valuedouble;
        if (response->score < 1)  response->score = 1;
        if (response->score > 10) response->score = 10;
    } else {
        response->score = 7;
    }

    if (j_encouragement && cJSON_IsString(j_encouragement)) {
        strncpy(response->encouragement, j_encouragement->valuestring,
                sizeof(response->encouragement) - 1);
    } else {
        strncpy(response->encouragement, "You are doing amazing!", sizeof(response->encouragement) - 1);
    }

    cJSON_Delete(inner);

    ESP_LOGI(TAG, "Teacher response: score=%d, response=\"%s\"",
             response->score, response->response);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  TTS Synthesis                                                       */
/* ------------------------------------------------------------------ */

esp_err_t openai_tts_synthesize(const char *text,
                                 uint8_t **audio_out,
                                 size_t *audio_len_out)
{
    ESP_LOGI(TAG, "TTS: synthesizing: \"%s\"", text);

    /* Build JSON request */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "tts-1");
    cJSON_AddStringToObject(root, "input", text);
    cJSON_AddStringToObject(root, "voice", CONFIG_OPENAI_TTS_VOICE);
    cJSON_AddStringToObject(root, "response_format", "pcm");  /* raw 16-bit PCM, 24kHz */
    cJSON_AddNumberToObject(root, "speed",
                             strtod(CONFIG_OPENAI_TTS_SPEED, NULL));

    char *body_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!body_str) {
        return ESP_FAIL;
    }

    /* Build Authorization header */
    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", CONFIG_OPENAI_API_KEY);

    /* Prepare response accumulator in PSRAM (audio data can be large) */
    http_resp_t resp = {0};
    if (http_resp_init(&resp, 64 * 1024, true) != ESP_OK) {  /* start with 64KB */
        free(body_str);
        return ESP_ERR_NO_MEM;
    }

    /* Configure HTTP client */
    esp_http_client_config_t config = {
        .url                    = OPENAI_TTS_URL,
        .method                 = HTTP_METHOD_POST,
        .event_handler          = http_event_handler,
        .user_data              = &resp,
        .transport_type         = HTTP_TRANSPORT_OVER_SSL,
        .skip_cert_common_name_check = true,
        .timeout_ms             = 30000,
        .buffer_size            = 4096,
        .buffer_size_tx         = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(body_str);
        http_resp_free(&resp);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body_str, (int)strlen(body_str));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    esp_http_client_cleanup(client);
    free(body_str);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "TTS API error (HTTP %d): %s", status, esp_err_to_name(err));
        http_resp_free(&resp);
        return ESP_FAIL;
    }

    if (resp.len == 0) {
        ESP_LOGE(TAG, "TTS returned empty audio");
        http_resp_free(&resp);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "TTS received %u bytes of PCM audio", (unsigned)resp.len);

    /* Transfer ownership of buffer to caller */
    *audio_out     = resp.buf;
    *audio_len_out = resp.len;
    /* Don't free resp.buf - caller owns it now */

    return ESP_OK;
}