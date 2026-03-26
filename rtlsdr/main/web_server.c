#include "web_server.h"
#include "adsb/adsb_decoder.h"
#include "fm/fm_receiver.h"
#include "spectrum/spectrum_analyzer.h"
#include "ais/ais_decoder.h"
#include "drone/drone_detector.h"
#include "config.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "cJSON.h"

static const char *TAG = "web_server";

static httpd_handle_t s_server = NULL;

// WebSocket client file descriptors
static int s_ws_clients[WS_MAX_CLIENTS];
static int s_ws_client_count = 0;

// ---------------------------------------------------------------------------
// JSON Builders
// ---------------------------------------------------------------------------

static char *build_aircraft_json(void)
{
    aircraft_t *aircraft;
    int count;
    adsb_get_aircraft_list(&aircraft, &count);

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_AddArrayToObject(root, "aircraft");

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    for (int i = 0; i < count; i++) {
        aircraft_t *ac = &aircraft[i];
        cJSON *obj = cJSON_CreateObject();

        char icao_str[7];
        snprintf(icao_str, sizeof(icao_str), "%06X", ac->icao);
        cJSON_AddStringToObject(obj, "icao", icao_str);
        cJSON_AddStringToObject(obj, "callsign",
                                ac->callsign[0] ? ac->callsign : "");
        cJSON_AddBoolToObject(obj,   "pos_valid", ac->position_valid);

        if (ac->position_valid) {
            cJSON_AddNumberToObject(obj, "lat", ac->lat);
            cJSON_AddNumberToObject(obj, "lon", ac->lon);
        }

        cJSON_AddNumberToObject(obj, "alt",      ac->altitude);
        cJSON_AddNumberToObject(obj, "speed",    ac->speed);
        cJSON_AddNumberToObject(obj, "heading",  ac->heading);
        cJSON_AddNumberToObject(obj, "vrate",    ac->vertical_rate);
        cJSON_AddBoolToObject(obj,   "on_ground", ac->on_ground);
        cJSON_AddNumberToObject(obj, "msgs",     ac->msg_count);
        cJSON_AddNumberToObject(obj, "seen",
                                (now_ms - ac->last_seen_ms) / 1000);

        cJSON_AddItemToArray(arr, obj);
    }

    adsb_release_aircraft_list();

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;  // Caller must free()
}

static char *build_stats_json(void)
{
    adsb_stats_t stats;
    adsb_get_stats(&stats);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "total_msgs",    stats.total_messages);
    cJSON_AddNumberToObject(root, "valid_msgs",    stats.valid_messages);
    cJSON_AddNumberToObject(root, "df17_msgs",     stats.df17_messages);
    cJSON_AddNumberToObject(root, "crc_errors",    stats.crc_errors);
    cJSON_AddNumberToObject(root, "aircraft_count", stats.aircraft_count);
    cJSON_AddNumberToObject(root, "uptime_s",
                            (uint32_t)(esp_timer_get_time() / 1000000));

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// ---------------------------------------------------------------------------
// WebSocket Management
// ---------------------------------------------------------------------------

static void ws_add_client(int fd)
{
    for (int i = 0; i < s_ws_client_count; i++) {
        if (s_ws_clients[i] == fd) return;  // Already registered
    }
    if (s_ws_client_count < WS_MAX_CLIENTS) {
        s_ws_clients[s_ws_client_count++] = fd;
        ESP_LOGI(TAG, "WebSocket client connected: fd=%d (total: %d)",
                 fd, s_ws_client_count);
    }
}

static void ws_remove_client(int fd)
{
    for (int i = 0; i < s_ws_client_count; i++) {
        if (s_ws_clients[i] == fd) {
            s_ws_clients[i] = s_ws_clients[--s_ws_client_count];
            ESP_LOGI(TAG, "WebSocket client disconnected: fd=%d (total: %d)",
                     fd, s_ws_client_count);
            return;
        }
    }
}

static void ws_broadcast(const char *data, size_t len)
{
    if (s_server == NULL || s_ws_client_count == 0) return;

    httpd_ws_frame_t ws_pkt = {
        .final   = true,
        .fragmented = false,
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)data,
        .len     = len,
    };

    int i = 0;
    while (i < s_ws_client_count) {
        esp_err_t ret = httpd_ws_send_frame_async(s_server, s_ws_clients[i], &ws_pkt);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send to fd=%d, removing client", s_ws_clients[i]);
            ws_remove_client(s_ws_clients[i]);
            // Don't increment i - the slot was filled by the last client
        } else {
            i++;
        }
    }
}

// ---------------------------------------------------------------------------
// WebSocket Broadcast Task
// ---------------------------------------------------------------------------

static void ws_broadcast_task(void *pvParameters)
{
    ESP_LOGI(TAG, "WebSocket broadcast task started (interval: %dms)", WS_BROADCAST_MS);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(WS_BROADCAST_MS));

        if (s_ws_client_count == 0) continue;

        char *json = build_aircraft_json();
        if (json) {
            ws_broadcast(json, strlen(json));
            free(json);
        }
    }
}

// ---------------------------------------------------------------------------
// HTTP Handlers
// ---------------------------------------------------------------------------

// GET /ws - WebSocket upgrade handler
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket handshake from fd=%d", httpd_req_to_sockfd(req));
        ws_add_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    // Handle incoming WebSocket frames (ping/pong, close, etc.)
    httpd_ws_frame_t ws_pkt = {0};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %d", ret);
        ws_remove_client(httpd_req_to_sockfd(req));
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ws_remove_client(httpd_req_to_sockfd(req));
    }

    return ESP_OK;
}

// GET /api/aircraft - Returns current aircraft list as JSON
static esp_err_t api_aircraft_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    char *json = build_aircraft_json();
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// GET /api/stats - Returns decoder statistics as JSON
static esp_err_t api_stats_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    char *json = build_stats_json();
    if (!json) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

// GET / and static files - Serve from SPIFFS
static esp_err_t static_file_handler(httpd_req_t *req)
{
    char filepath[128];
    const char *uri = req->uri;

    // 根路径提供入口导航页
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }

    // 目录路径补全 index.html
    size_t uri_len = strlen(uri);
    if (uri[uri_len - 1] == '/') {
        static char idx_uri[128];
        snprintf(idx_uri, sizeof(idx_uri), "%sindex.html", uri);
        uri = idx_uri;
    }

    snprintf(filepath, sizeof(filepath), "%s%s", SPIFFS_BASE_PATH, uri);

    // Determine content type
    const char *content_type = "text/plain";
    if (strstr(uri, ".html")) content_type = "text/html";
    else if (strstr(uri, ".css"))  content_type = "text/css";
    else if (strstr(uri, ".js"))   content_type = "application/javascript";
    else if (strstr(uri, ".json")) content_type = "application/json";
    else if (strstr(uri, ".ico"))  content_type = "image/x-icon";
    else if (strstr(uri, ".png"))  content_type = "image/png";

    FILE *f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGW(TAG, "File not found: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");

    char *chunk = malloc(1024);
    if (!chunk) {
        fclose(f);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    size_t read_bytes;
    do {
        read_bytes = fread(chunk, 1, 1024, f);
        if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                fclose(f);
                free(chunk);
                ESP_LOGE(TAG, "File send failed: %s", filepath);
                httpd_resp_sendstr_chunk(req, NULL);
                return ESP_FAIL;
            }
        }
    } while (read_bytes == 1024);

    fclose(f);
    free(chunk);
    httpd_resp_sendstr_chunk(req, NULL);  // End chunked response

    ESP_LOGD(TAG, "Served: %s (%s)", filepath, content_type);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Server Initialization
// ---------------------------------------------------------------------------

esp_err_t web_server_init(void)
{
    ESP_LOGI(TAG, "Initializing web server on port %d...", WEB_SERVER_PORT);

    // Mount SPIFFS
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path              = SPIFFS_BASE_PATH,
        .partition_label        = NULL,
        .max_files              = 8,
        .format_if_mount_failed = false,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "SPIFFS partition not found");
        } else {
            ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS: %d/%d bytes used", used, total);

    // Initialize WebSocket client list
    memset(s_ws_clients, -1, sizeof(s_ws_clients));
    s_ws_client_count = 0;

    // Configure HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = WEB_SERVER_PORT;
    config.max_open_sockets = WS_MAX_CLIENTS + 4;
    config.max_uri_handlers = 24;
    config.lru_purge_enable = true;
    config.uri_match_fn     = httpd_uri_match_wildcard;

    ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register URI handlers
    httpd_uri_t ws_uri = {
        .uri       = "/ws",
        .method    = HTTP_GET,
        .handler   = ws_handler,
        .user_ctx  = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_server, &ws_uri);

    httpd_uri_t api_aircraft_uri = {
        .uri     = "/api/aircraft",
        .method  = HTTP_GET,
        .handler = api_aircraft_handler,
    };
    httpd_register_uri_handler(s_server, &api_aircraft_uri);

    httpd_uri_t api_stats_uri = {
        .uri     = "/api/stats",
        .method  = HTTP_GET,
        .handler = api_stats_handler,
    };
    httpd_register_uri_handler(s_server, &api_stats_uri);

    // Wildcard handler for static files (must be last)
    httpd_uri_t static_uri = {
        .uri     = "/*",
        .method  = HTTP_GET,
        .handler = static_file_handler,
    };
    httpd_register_uri_handler(s_server, &static_uri);

    // Start WebSocket broadcast task
    xTaskCreate(ws_broadcast_task, "ws_broadcast", 4096, NULL, 4, NULL);

    // 注册 FM 处理器
    fm_receiver_register_handlers(s_server);

    // 注册频谱仪处理器
    spectrum_analyzer_register_handlers(s_server);

    // 注册 AIS 处理器
    ais_register_handlers(s_server);

    // 注册无人机检测处理器
    drone_register_handlers(s_server);

    ESP_LOGI(TAG, "Web server started.");
    ESP_LOGI(TAG, "  Home:     http://192.168.4.1/");
    ESP_LOGI(TAG, "  ADS-B:    http://192.168.4.1/adsb/");
    ESP_LOGI(TAG, "  FM:       http://192.168.4.1/fm/");
    ESP_LOGI(TAG, "  Spectrum: http://192.168.4.1/spectrum/");
    ESP_LOGI(TAG, "  AIS:      http://192.168.4.1/ais/");
    ESP_LOGI(TAG, "  Drone:    http://192.168.4.1/drone/");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        esp_vfs_spiffs_unregister(NULL);
        ESP_LOGI(TAG, "Web server stopped");
    }
}