#include "web_server.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
    config.max_uri_handlers = 8;
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

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_index));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_stream));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_snapshot));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_control));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_status));

    ESP_LOGI(TAG, "web server ready");
    return ESP_OK;
}
