#include "web_server.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_intercom.h"
#include "camera_app.h"
#include "motor_control.h"
#include "ultrasonic.h"

static const char *TAG = "web";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

static httpd_handle_t s_server = NULL;

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req,
                           (const char *)index_html_start,
                           (ssize_t)(index_html_end - index_html_start));
}

static esp_err_t snapshot_handler(httpd_req_t *req)
{
    if (!camera_app_is_ready()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not ready");
        return ESP_FAIL;
    }

    camera_fb_t *fb = camera_app_fb_get();
    if (fb == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "capture failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t ret = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    camera_app_fb_return(fb);
    return ret;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    if (!camera_app_is_ready()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not ready");
        return ESP_FAIL;
    }

    static const char *boundary = "--frame\r\n";
    char part_header[96];

    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");

    while (true) {
        camera_fb_t *fb = camera_app_fb_get();
        if (fb == NULL) {
            ESP_LOGW(TAG, "camera frame unavailable");
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int header_len = snprintf(part_header, sizeof(part_header),
                                  "Content-Type: image/jpeg\r\n"
                                  "Content-Length: %u\r\n\r\n",
                                  (unsigned int)fb->len);

        esp_err_t ret = httpd_resp_send_chunk(req, boundary, strlen(boundary));
        if (ret == ESP_OK) {
            ret = httpd_resp_send_chunk(req, part_header, header_len);
        }
        if (ret == ESP_OK) {
            ret = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }
        if (ret == ESP_OK) {
            ret = httpd_resp_send_chunk(req, "\r\n", 2);
        }

        camera_app_fb_return(fb);

        if (ret != ESP_OK) {
            ESP_LOGI(TAG, "stream closed: %s", esp_err_to_name(ret));
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }

    return ESP_OK;
}

static uint8_t parse_speed(const char *value)
{
    if (value == NULL || value[0] == '\0') {
        return CONFIG_CAR_MOTOR_DEFAULT_SPEED;
    }

    int speed = atoi(value);
    if (speed < 0) {
        speed = 0;
    } else if (speed > 100) {
        speed = 100;
    }
    return (uint8_t)speed;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void url_decode_in_place(char *value)
{
    char *src = value;
    char *dst = value;

    while (*src != '\0') {
        if (*src == '%' && hex_value(src[1]) >= 0 && hex_value(src[2]) >= 0) {
            *dst++ = (char)((hex_value(src[1]) << 4) | hex_value(src[2]));
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static bool text_has_any(const char *text, const char *a, const char *b,
                         const char *c, const char *d)
{
    return (a != NULL && strstr(text, a) != NULL) ||
           (b != NULL && strstr(text, b) != NULL) ||
           (c != NULL && strstr(text, c) != NULL) ||
           (d != NULL && strstr(text, d) != NULL);
}

static car_motion_t motion_from_voice_text(const char *text, bool *recognized)
{
    if (recognized != NULL) {
        *recognized = true;
    }

    if (text == NULL || text[0] == '\0') {
        if (recognized != NULL) {
            *recognized = false;
        }
        return CAR_MOTION_STOP;
    }

    if (text_has_any(text, "停止", "停车", "刹车", "stop") ||
        text_has_any(text, "停下", "别动", "不要动", NULL)) {
        return CAR_MOTION_STOP;
    }
    if (text_has_any(text, "原地左", "左旋", "spin left", "rotate left")) {
        return CAR_MOTION_SPIN_LEFT;
    }
    if (text_has_any(text, "原地右", "右旋", "spin right", "rotate right")) {
        return CAR_MOTION_SPIN_RIGHT;
    }
    if (text_has_any(text, "左转", "向左", "往左", "turn left") ||
        text_has_any(text, "左边", "left", NULL, NULL)) {
        return CAR_MOTION_LEFT;
    }
    if (text_has_any(text, "右转", "向右", "往右", "turn right") ||
        text_has_any(text, "右边", "right", NULL, NULL)) {
        return CAR_MOTION_RIGHT;
    }
    if (text_has_any(text, "后退", "倒车", "向后", "backward") ||
        text_has_any(text, "back", NULL, NULL, NULL)) {
        return CAR_MOTION_BACKWARD;
    }
    if (text_has_any(text, "前进", "向前", "往前", "forward") ||
        text_has_any(text, "go ahead", "go", NULL, NULL)) {
        return CAR_MOTION_FORWARD;
    }

    if (recognized != NULL) {
        *recognized = false;
    }
    return CAR_MOTION_STOP;
}

static esp_err_t control_handler(httpd_req_t *req)
{
    char query[128] = {0};
    char cmd[32] = "stop";
    char speed_value[8] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "cmd", cmd, sizeof(cmd));
        httpd_query_key_value(query, "speed", speed_value, sizeof(speed_value));
    }

    car_motion_t motion = car_motion_from_string(cmd);
    uint8_t speed = parse_speed(speed_value);
    esp_err_t drive_ret = motor_control_drive(motion, speed);

    motor_status_t status = {0};
    motor_control_get_status(&status);

    char body[256];
    int len = snprintf(body, sizeof(body),
                       "{\"ok\":%s,\"cmd\":\"%s\",\"speed\":%u,"
                       "\"left\":%d,\"right\":%d,\"blocked\":%s,"
                       "\"distance_cm\":%d}\n",
                       drive_ret == ESP_OK ? "true" : "false",
                       car_motion_to_string(status.motion),
                       status.speed,
                       status.left_percent,
                       status.right_percent,
                       status.obstacle_blocked ? "true" : "false",
                       ultrasonic_get_distance_cm());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, len);
}

static esp_err_t voice_handler(httpd_req_t *req)
{
    char query[256] = {0};
    char text[128] = {0};
    char speed_value[8] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "text", text, sizeof(text));
        httpd_query_key_value(query, "speed", speed_value, sizeof(speed_value));
    }
    url_decode_in_place(text);

    bool recognized = false;
    car_motion_t motion = motion_from_voice_text(text, &recognized);
    uint8_t speed = parse_speed(speed_value);
    esp_err_t drive_ret = ESP_ERR_NOT_FOUND;

    if (recognized) {
        drive_ret = motor_control_drive(motion, speed);
    }

    motor_status_t status = {0};
    motor_control_get_status(&status);

    char body[320];
    int len = snprintf(body, sizeof(body),
                       "{\"ok\":%s,\"recognized\":%s,\"cmd\":\"%s\","
                       "\"speed\":%u,\"left\":%d,\"right\":%d,"
                       "\"blocked\":%s,\"distance_cm\":%d,\"camera\":%s}\n",
                       (recognized && drive_ret == ESP_OK) ? "true" : "false",
                       recognized ? "true" : "false",
                       recognized ? car_motion_to_string(motion) : car_motion_to_string(status.motion),
                       status.speed,
                       status.left_percent,
                       status.right_percent,
                       status.obstacle_blocked ? "true" : "false",
                       ultrasonic_get_distance_cm(),
                       camera_app_is_ready() ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, len);
}

#if CONFIG_HTTPD_WS_SUPPORT && CONFIG_CAR_ENABLE_INTERCOM
static esp_err_t audio_ws_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        esp_err_t ret = audio_intercom_set_ws_client(req->handle, fd);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "audio websocket rejected: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    httpd_ws_frame_t frame = {0};
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        audio_intercom_clear_ws_client(fd);
        ESP_LOGW(TAG, "audio ws frame header failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (frame.len > CONFIG_CAR_AUDIO_MAX_INCOMING_BYTES) {
        audio_intercom_clear_ws_client(fd);
        ESP_LOGW(TAG, "audio ws frame too large: %u", (unsigned int)frame.len);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *payload = NULL;
    if (frame.len > 0) {
        payload = malloc(frame.len);
        if (payload == NULL) {
            return ESP_ERR_NO_MEM;
        }
        frame.payload = payload;
        ret = httpd_ws_recv_frame(req, &frame, frame.len);
        if (ret != ESP_OK) {
            free(payload);
            audio_intercom_clear_ws_client(fd);
            ESP_LOGW(TAG, "audio ws payload failed: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    if (frame.type == HTTPD_WS_TYPE_BINARY && frame.len > 0) {
        ret = audio_intercom_play_pcm(frame.payload, frame.len);
        if (ret == ESP_ERR_TIMEOUT) {
            ret = ESP_OK;
        }
    } else if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        audio_intercom_clear_ws_client(fd);
    }

    free(payload);
    return ret == ESP_ERR_NOT_SUPPORTED ? ESP_OK : ret;
}
#endif

static esp_err_t status_handler(httpd_req_t *req)
{
    motor_status_t status = {0};
    motor_control_get_status(&status);

    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t age_ms = (uint32_t)(esp_timer_get_time() / 1000ULL) - status.last_command_ms;

    char body[320];
    int len = snprintf(body, sizeof(body),
                       "{\"uptime_s\":%lu,\"cmd\":\"%s\",\"speed\":%u,"
                       "\"left\":%d,\"right\":%d,\"blocked\":%s,"
                       "\"distance_cm\":%d,\"camera\":%s,"
                       "\"last_command_age_ms\":%lu}\n",
                       (unsigned long)uptime_s,
                       car_motion_to_string(status.motion),
                       status.speed,
                       status.left_percent,
                       status.right_percent,
                       status.obstacle_blocked ? "true" : "false",
                       ultrasonic_get_distance_cm(),
                       camera_app_is_ready() ? "true" : "false",
                       (unsigned long)age_ms);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body, len);
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 8192;
    config.max_uri_handlers = 9;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "http server start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const httpd_uri_t uri_index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
    };
    const httpd_uri_t uri_stream = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
    };
    const httpd_uri_t uri_snapshot = {
        .uri = "/jpg",
        .method = HTTP_GET,
        .handler = snapshot_handler,
    };
    const httpd_uri_t uri_control = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = control_handler,
    };
    const httpd_uri_t uri_status = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
    };
    const httpd_uri_t uri_voice = {
        .uri = "/voice",
        .method = HTTP_GET,
        .handler = voice_handler,
    };
#if CONFIG_HTTPD_WS_SUPPORT && CONFIG_CAR_ENABLE_INTERCOM
    const httpd_uri_t uri_audio = {
        .uri = "/audio",
        .method = HTTP_GET,
        .handler = audio_ws_handler,
        .is_websocket = true,
    };
#endif

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_index));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_stream));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_snapshot));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_control));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_status));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_voice));
#if CONFIG_HTTPD_WS_SUPPORT && CONFIG_CAR_ENABLE_INTERCOM
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_audio));
#endif

    ESP_LOGI(TAG, "web server ready");
    return ESP_OK;
}
