/**
 * @file web_dashboard.c
 * @brief HTTP dashboard server for mobile trend viewing
 */
#include "web/web_dashboard.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history/env_history.h"
#include "net/wifi_manager.h"
#include "net/weather_client.h"
#include "sensor/dht11_sensor.h"

static httpd_handle_t s_server = NULL;

extern const char web_dashboard_html_start[] asm("_binary_dashboard_html_start");
extern const char web_dashboard_html_end[] asm("_binary_dashboard_html_end");

static esp_err_t send_chunkf(httpd_req_t *req, const char *fmt, ...)
{
    char buf[512];
    va_list args;
    int written = 0;

    va_start(args, fmt);
    written = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (written < 0) {
        return ESP_FAIL;
    }

    if ((size_t)written >= sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return httpd_resp_sendstr_chunk(req, buf);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    size_t html_len = (size_t)(web_dashboard_html_end - web_dashboard_html_start);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, web_dashboard_html_start, html_len);
}

static esp_err_t api_history_get_handler(httpd_req_t *req)
{
    dht11_data_t indoor = {0};
    weather_data_t outdoor = {0};
    env_history_point_t *points = NULL;
    size_t point_count = 0;
    char ip[20] = "0.0.0.0";
    int64_t uptime_us = esp_timer_get_time();
    bool connected = wifi_manager_is_connected();
    bool indoor_available = false;
    int64_t indoor_age_s = -1;
    int64_t outdoor_age_s = -1;
    int64_t forecast_age_s = -1;
    esp_err_t ret = ESP_OK;

    dht11_sensor_get_data(&indoor);
    weather_client_get_data(&outdoor);
    wifi_manager_get_ip(ip, sizeof(ip));

    indoor_available = indoor.update_time_us > 0;
    if (indoor_available) {
        indoor_age_s = (uptime_us - indoor.update_time_us) / 1000000;
        if (indoor_age_s < 0) {
            indoor_age_s = 0;
        }
    }

    if (outdoor.now_update_time > 0) {
        outdoor_age_s = ((uptime_us / 1000000) - outdoor.now_update_time);
        if (outdoor_age_s < 0) {
            outdoor_age_s = 0;
        }
    }

    if (outdoor.forecast_update_time > 0) {
        forecast_age_s = ((uptime_us / 1000000) - outdoor.forecast_update_time);
        if (forecast_age_s < 0) {
            forecast_age_s = 0;
        }
    }

    points = calloc(ENV_HISTORY_CAPACITY, sizeof(*points));
    if (!points) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_ERR_NO_MEM;
    }
    point_count = env_history_copy_points(points, ENV_HISTORY_CAPACITY);

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    ret = send_chunkf(
        req,
        "{\"device\":\"%s\",\"ip\":\"%s\",\"wifi_connected\":%s,"
        "\"sample_interval_s\":%u,\"capacity\":%u,"
        "\"latest\":{\"indoor\":{\"available\":%s,\"fresh\":%s,\"temp\":%d,\"humidity\":%u,\"age_s\":%" PRId64 ",\"samples\":%lu,\"errors\":%lu},"
        "\"outdoor\":{\"available\":%s,\"forecast_available\":%s,\"temp\":%d,\"humidity\":%u,\"age_s\":%" PRId64 ",\"forecast_age_s\":%" PRId64 ",\"temp_low\":%d,\"temp_high\":%d}},"
        "\"history\":[",
        DEVICE_NAME,
        ip,
        connected ? "true" : "false",
        (unsigned)ENV_HISTORY_INTERVAL_S,
        (unsigned)ENV_HISTORY_CAPACITY,
        indoor_available ? "true" : "false",
        indoor.valid ? "true" : "false",
        indoor.temperature_c,
        indoor.humidity,
        indoor_age_s,
        (unsigned long)indoor.sample_count,
        (unsigned long)indoor.error_count,
        outdoor.now_valid ? "true" : "false",
        outdoor.forecast_valid ? "true" : "false",
        outdoor.temp_now,
        outdoor.humidity,
        outdoor_age_s,
        forecast_age_s,
        outdoor.temp_low,
        outdoor.temp_high);
    if (ret != ESP_OK) {
        goto done;
    }

    for (size_t i = 0; i < point_count; i++) {
        ret = send_chunkf(
            req,
            "%s{\"ts\":%" PRId64 ",\"in_ok\":%s,\"in_t\":%d,\"in_h\":%u,"
            "\"out_ok\":%s,\"out_t\":%d,\"out_h\":%u}",
            (i == 0) ? "" : ",",
            points[i].timestamp_s,
            points[i].indoor_valid ? "true" : "false",
            points[i].indoor_temp_c,
            points[i].indoor_humidity,
            points[i].outdoor_valid ? "true" : "false",
            points[i].outdoor_temp_c,
            points[i].outdoor_humidity);
        if (ret != ESP_OK) {
            goto done;
        }
    }

    ret = httpd_resp_sendstr_chunk(req, "]}");
    if (ret == ESP_OK) {
        ret = httpd_resp_sendstr_chunk(req, NULL);
    }

done:
    free(points);
    return ret;
}

static esp_err_t start_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t api_history = {
        .uri = "/api/history",
        .method = HTTP_GET,
        .handler = api_history_get_handler,
        .user_ctx = NULL,
    };

    config.server_port = WEB_SERVER_PORT;
    config.max_uri_handlers = 8;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        s_server = NULL;
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &api_history));
    return ESP_OK;
}

esp_err_t web_dashboard_init(void)
{
    return ESP_OK;
}

void web_dashboard_task(void *arg)
{
    char last_ip[20] = "";

    (void)arg;

    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(WEB_SERVER_POLL_MS));
    }

    if (start_server() != ESP_OK) {
        ESP_LOGE(TAG_WEB, "Failed to start dashboard server");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG_WEB, "Dashboard server started on port %u", (unsigned)WEB_SERVER_PORT);

    while (1) {
        char ip[20] = "0.0.0.0";

        wifi_manager_get_ip(ip, sizeof(ip));
        if (strcmp(ip, last_ip) != 0 && strcmp(ip, "0.0.0.0") != 0) {
            strlcpy(last_ip, ip, sizeof(last_ip));
            ESP_LOGI(TAG_WEB, "Dashboard ready: http://%s/", ip);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
