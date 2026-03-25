#include "image_server.h"
#include "wifi_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "image_server";

#define MAX_IMAGES      CONFIG_DOORMAN_MAX_STORED_IMAGES
#define SERVER_PORT     CONFIG_DOORMAN_HTTP_SERVER_PORT

/* 存储的图像记录 */
typedef struct {
    uint8_t  *data;         /* JPEG 数据 */
    size_t    len;          /* 数据长度 */
    uint32_t  timestamp;    /* 时间戳（毫秒） */
    char      event_type[32]; /* 事件类型 */
    uint32_t  index;        /* 图像序号 */
} stored_image_t;

static httpd_handle_t  s_server = NULL;
static stored_image_t  s_images[MAX_IMAGES];
static int             s_image_count = 0;
static int             s_latest_idx  = -1;
static uint32_t        s_image_seq   = 0;
static SemaphoreHandle_t s_mutex     = NULL;

/* ============================================================
 * HTML 页面模板
 * ============================================================ */
static const char *HTML_INDEX_HEADER =
    "<!DOCTYPE html><html lang='zh-CN'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta http-equiv='refresh' content='10'>"
    "<title>ESP32-S3 门卫系统</title>"
    "<style>"
    "body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:20px}"
    "h1{color:#e94560;text-align:center;margin-bottom:5px}"
    ".subtitle{text-align:center;color:#aaa;margin-bottom:20px;font-size:14px}"
    ".card{background:#16213e;border-radius:10px;padding:15px;margin:10px 0;box-shadow:0 4px 6px rgba(0,0,0,0.3)}"
    ".img-container{text-align:center;margin:10px 0}"
    "img{max-width:100%;border-radius:8px;border:2px solid #e94560}"
    ".badge{display:inline-block;padding:3px 10px;border-radius:12px;font-size:12px;font-weight:bold}"
    ".badge-person{background:#e94560;color:white}"
    ".badge-package{background:#f5a623;color:white}"
    ".time{color:#aaa;font-size:12px;margin-top:5px}"
    ".status{text-align:center;padding:10px;background:#0f3460;border-radius:8px;margin-bottom:15px}"
    ".status.online{border-left:4px solid #4caf50}"
    "a{color:#e94560;text-decoration:none}"
    "a:hover{text-decoration:underline}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:15px}"
    "</style></head><body>"
    "<h1>🏠 ESP32-S3 门卫系统</h1>"
    "<p class='subtitle'>家门口实时监控 | 自动刷新: 10秒</p>";

static const char *HTML_INDEX_FOOTER =
    "<p style='text-align:center;color:#555;font-size:12px;margin-top:30px'>"
    "ESP32-S3 Doorman &copy; 2024 | 页面每10秒自动刷新</p>"
    "</body></html>";

/* ============================================================
 * HTTP 处理函数
 * ============================================================ */

/* 主页：显示最新图像和历史记录 */
static esp_err_t handler_index(httpd_req_t *req)
{
    char ip_buf[20] = "unknown";
    wifi_manager_get_ip(ip_buf, sizeof(ip_buf));

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, HTML_INDEX_HEADER);

    /* 状态栏 */
    char status_buf[256];
    snprintf(status_buf, sizeof(status_buf),
             "<div class='status online'>"
             "✅ 设备在线 | IP: %s | 已捕获图像: %d 张"
             "</div>",
             ip_buf, s_image_count);
    httpd_resp_sendstr_chunk(req, status_buf);

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
        if (s_latest_idx >= 0 && s_images[s_latest_idx].data != NULL) {
            /* 最新图像 */
            stored_image_t *latest = &s_images[s_latest_idx];
            char buf[512];

            snprintf(buf, sizeof(buf),
                     "<div class='card'>"
                     "<h2>📸 最新抓拍</h2>"
                     "<span class='badge %s'>%s</span>"
                     "<div class='img-container'>"
                     "<img src='/image/%lu' alt='最新图像' loading='lazy'>"
                     "</div>"
                     "<p class='time'>⏰ 时间戳: %lu ms | 大小: %zu 字节</p>"
                     "</div>",
                     strcmp(latest->event_type, "快递到达") == 0 ? "badge-package" : "badge-person",
                     latest->event_type,
                     (unsigned long)latest->index,
                     (unsigned long)latest->timestamp,
                     latest->len);
            httpd_resp_sendstr_chunk(req, buf);
        } else {
            httpd_resp_sendstr_chunk(req,
                "<div class='card'><p style='text-align:center;color:#aaa'>"
                "📷 暂无图像，等待门口有活动...</p></div>");
        }

        /* 历史图像网格 */
        if (s_image_count > 1) {
            httpd_resp_sendstr_chunk(req, "<h2>📂 历史记录</h2><div class='grid'>");
            for (int i = 0; i < MAX_IMAGES; i++) {
                int idx = (s_latest_idx - i + MAX_IMAGES) % MAX_IMAGES;
                if (idx == s_latest_idx && i > 0) continue;
                if (s_images[idx].data == NULL) continue;
                if (i == 0) continue; /* 跳过最新的（已显示） */

                char buf[512];
                snprintf(buf, sizeof(buf),
                         "<div class='card'>"
                         "<span class='badge %s'>%s</span>"
                         "<div class='img-container'>"
                         "<a href='/image/%lu' target='_blank'>"
                         "<img src='/image/%lu' alt='历史图像' loading='lazy'>"
                         "</a></div>"
                         "<p class='time'>⏰ %lu ms | %zu 字节</p>"
                         "</div>",
                         strcmp(s_images[idx].event_type, "快递到达") == 0 ? "badge-package" : "badge-person",
                         s_images[idx].event_type,
                         (unsigned long)s_images[idx].index,
                         (unsigned long)s_images[idx].index,
                         (unsigned long)s_images[idx].timestamp,
                         s_images[idx].len);
                httpd_resp_sendstr_chunk(req, buf);
            }
            httpd_resp_sendstr_chunk(req, "</div>");
        }

        xSemaphoreGive(s_mutex);
    }

    httpd_resp_sendstr_chunk(req, HTML_INDEX_FOOTER);
    httpd_resp_sendstr_chunk(req, NULL); /* 结束分块传输 */
    return ESP_OK;
}

/* 获取指定序号的图像 */
static esp_err_t handler_image(httpd_req_t *req)
{
    /* 从 URI 中提取图像序号 */
    const char *uri = req->uri;
    const char *idx_str = strrchr(uri, '/');
    if (idx_str == NULL) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
        return ESP_FAIL;
    }
    idx_str++;

    uint32_t req_index = (uint32_t)atol(idx_str);

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Busy");
        return ESP_FAIL;
    }

    /* 查找对应序号的图像 */
    stored_image_t *found = NULL;
    for (int i = 0; i < MAX_IMAGES; i++) {
        if (s_images[i].data != NULL && s_images[i].index == req_index) {
            found = &s_images[i];
            break;
        }
    }

    if (found == NULL) {
        xSemaphoreGive(s_mutex);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Image Not Found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    esp_err_t ret = httpd_resp_send(req, (const char *)found->data, found->len);

    xSemaphoreGive(s_mutex);
    return ret;
}

/* 获取最新图像（直接重定向） */
static esp_err_t handler_latest(httpd_req_t *req)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Busy");
        return ESP_FAIL;
    }

    if (s_latest_idx < 0 || s_images[s_latest_idx].data == NULL) {
        xSemaphoreGive(s_mutex);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No Image Available");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    esp_err_t ret = httpd_resp_send(req,
                                    (const char *)s_images[s_latest_idx].data,
                                    s_images[s_latest_idx].len);
    xSemaphoreGive(s_mutex);
    return ret;
}

/* JSON 状态接口 */
static esp_err_t handler_status(httpd_req_t *req)
{
    char ip_buf[20] = "unknown";
    wifi_manager_get_ip(ip_buf, sizeof(ip_buf));

    char json_buf[256];
    snprintf(json_buf, sizeof(json_buf),
             "{\"status\":\"online\",\"ip\":\"%s\","
             "\"image_count\":%d,\"latest_index\":%lu,"
             "\"uptime_ms\":%llu}",
             ip_buf,
             s_image_count,
             (unsigned long)(s_latest_idx >= 0 ? s_images[s_latest_idx].index : 0),
             (unsigned long long)(esp_timer_get_time() / 1000));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, json_buf);
}

/* ============================================================
 * 公共接口实现
 * ============================================================ */

esp_err_t image_server_init(void)
{
    /* 创建互斥锁 */
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "创建互斥锁失败");
        return ESP_ERR_NO_MEM;
    }

    /* 初始化图像存储 */
    memset(s_images, 0, sizeof(s_images));

    /* 配置 HTTP 服务器 */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = SERVER_PORT;
    config.max_uri_handlers = 8;
    config.stack_size       = 8192;
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动 HTTP 服务器失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 注册 URI 处理器 */
    httpd_uri_t uri_index = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = handler_index,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_index);

    httpd_uri_t uri_latest = {
        .uri      = "/latest",
        .method   = HTTP_GET,
        .handler  = handler_latest,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_latest);

    httpd_uri_t uri_image = {
        .uri      = "/image/*",
        .method   = HTTP_GET,
        .handler  = handler_image,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_image);

    httpd_uri_t uri_status = {
        .uri      = "/status",
        .method   = HTTP_GET,
        .handler  = handler_status,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_status);

    ESP_LOGI(TAG, "图像 HTTP 服务器已启动，端口: %d", SERVER_PORT);
    return ESP_OK;
}

void image_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }

    /* 释放所有图像内存 */
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
        for (int i = 0; i < MAX_IMAGES; i++) {
            if (s_images[i].data) {
                free(s_images[i].data);
                s_images[i].data = NULL;
            }
        }
        xSemaphoreGive(s_mutex);
    }
    ESP_LOGI(TAG, "图像服务器已停止");
}

esp_err_t image_server_update_image(const doorman_frame_t *frame, const char *event_type)
{
    if (frame == NULL || frame->data == NULL || frame->len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
        ESP_LOGE(TAG, "获取互斥锁超时");
        return ESP_ERR_TIMEOUT;
    }

    /* 循环使用存储槽 */
    int slot = (s_latest_idx + 1) % MAX_IMAGES;

    /* 释放旧图像 */
    if (s_images[slot].data) {
        free(s_images[slot].data);
        s_images[slot].data = NULL;
    }

    /* 复制新图像 */
    s_images[slot].data = (uint8_t *)malloc(frame->len);
    if (s_images[slot].data == NULL) {
        ESP_LOGE(TAG, "分配图像内存失败");
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    memcpy(s_images[slot].data, frame->data, frame->len);
    s_images[slot].len       = frame->len;
    s_images[slot].timestamp = frame->timestamp;
    s_images[slot].index     = ++s_image_seq;
    strncpy(s_images[slot].event_type,
            event_type ? event_type : "未知事件",
            sizeof(s_images[slot].event_type) - 1);

    s_latest_idx = slot;
    if (s_image_count < MAX_IMAGES) s_image_count++;

    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "图像已更新: 序号=%lu, 类型=%s, 大小=%zu 字节",
             (unsigned long)s_image_seq, event_type, frame->len);
    return ESP_OK;
}

esp_err_t image_server_get_url(char *url_buf, size_t buf_len)
{
    char ip_buf[20] = "0.0.0.0";
    wifi_manager_get_ip(ip_buf, sizeof(ip_buf));

    if (SERVER_PORT == 80) {
        snprintf(url_buf, buf_len, "http://%s/", ip_buf);
    } else {
        snprintf(url_buf, buf_len, "http://%s:%d/", ip_buf, SERVER_PORT);
    }
    return ESP_OK;
}

int image_server_get_image_count(void)
{
    return s_image_count;
}