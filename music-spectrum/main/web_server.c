#include "web_server.h"
#include "audio_player.h"
#include "fft_analyzer.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include <dirent.h>
#include <strings.h>

static const char *TAG = "web_server";

/* ── Embedded web files (from main/web/) ─────────────────────────────── */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");

/* ── WebSocket client tracking ───────────────────────────────────────── */
#define MAX_WS_CLIENTS  CONFIG_MAX_WS_CLIENTS

static int              s_ws_fds[MAX_WS_CLIENTS];
static SemaphoreHandle_t s_ws_mutex = NULL;
static httpd_handle_t   s_server   = NULL;

static void ws_clients_init(void)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) s_ws_fds[i] = -1;
}

static void ws_client_add(int fd)
{
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] == -1) { s_ws_fds[i] = fd; break; }
    }
    xSemaphoreGive(s_ws_mutex);
}

static void ws_client_remove(int fd)
{
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] == fd) { s_ws_fds[i] = -1; break; }
    }
    xSemaphoreGive(s_ws_mutex);
}

/* ── Helpers ─────────────────────────────────────────────────────────── */
static const char *state_str(audio_state_t st)
{
    switch (st) {
        case AUDIO_STATE_IDLE:    return "idle";
        case AUDIO_STATE_PLAYING: return "playing";
        case AUDIO_STATE_PAUSED:  return "paused";
        case AUDIO_STATE_STOPPED: return "stopped";
        case AUDIO_STATE_ERROR:   return "error";
        default:                  return "unknown";
    }
}

/* ── GET / → index.html ──────────────────────────────────────────────── */
static esp_err_t handler_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "identity");
    size_t len = index_html_end - index_html_start;
    return httpd_resp_send(req, (const char *)index_html_start, len);
}

/* ── GET /style.css ──────────────────────────────────────────────────── */
static esp_err_t handler_css(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    size_t len = style_css_end - style_css_start;
    return httpd_resp_send(req, (const char *)style_css_start, len);
}

/* ── GET /app.js ─────────────────────────────────────────────────────── */
static esp_err_t handler_js(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    size_t len = app_js_end - app_js_start;
    return httpd_resp_send(req, (const char *)app_js_start, len);
}

/* ── GET /api/status ─────────────────────────────────────────────────── */
static esp_err_t handler_status(httpd_req_t *req)
{
    audio_status_t status;
    audio_player_get_status(&status);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state",    state_str(status.state));
    cJSON_AddNumberToObject(root, "position", status.position_sec);
    cJSON_AddNumberToObject(root, "volume",   status.volume);

    cJSON *track = cJSON_AddObjectToObject(root, "track");
    cJSON_AddStringToObject(track, "title",       status.current_track.title);
    cJSON_AddStringToObject(track, "artist",      status.current_track.artist);
    cJSON_AddStringToObject(track, "album",       status.current_track.album);
    cJSON_AddStringToObject(track, "url",         status.current_track.url);
    cJSON_AddNumberToObject(track, "sample_rate", status.current_track.sample_rate);
    cJSON_AddNumberToObject(track, "channels",    status.current_track.channels);
    cJSON_AddNumberToObject(track, "bitrate",     status.current_track.bitrate);
    cJSON_AddNumberToObject(track, "duration",    status.current_track.duration_sec);
    cJSON_AddStringToObject(track, "source",
        status.current_track.source == AUDIO_SOURCE_HTTP ? "http" : "local");

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t ret = httpd_resp_sendstr(req, json_str);
    free(json_str);
    return ret;
}

/* ── POST /api/play ──────────────────────────────────────────────────── */
static esp_err_t handler_play(httpd_req_t *req)
{
    char buf[768];
    int  total = req->content_len;
    if (total <= 0 || total >= (int)sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_FAIL;
    }
    int received = httpd_req_recv(req, buf, total);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Recv error");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    cJSON *root   = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char *url    = cJSON_GetStringValue(cJSON_GetObjectItem(root, "url"));
    const char *title  = cJSON_GetStringValue(cJSON_GetObjectItem(root, "title"));
    const char *artist = cJSON_GetStringValue(cJSON_GetObjectItem(root, "artist"));

    if (!url) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing url");
        return ESP_FAIL;
    }

    esp_err_t err = audio_player_play(url, title, artist);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (err == ESP_OK) {
        return httpd_resp_sendstr(req, "{\"ok\":true}");
    } else {
        return httpd_resp_sendstr(req, "{\"ok\":false}");
    }
}

/* ── POST /api/pause ─────────────────────────────────────────────────── */
static esp_err_t handler_pause(httpd_req_t *req)
{
    audio_player_pause();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* ── POST /api/resume ────────────────────────────────────────────────── */
static esp_err_t handler_resume(httpd_req_t *req)
{
    audio_player_resume();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* ── POST /api/stop ──────────────────────────────────────────────────── */
static esp_err_t handler_stop(httpd_req_t *req)
{
    audio_player_stop();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* ── POST /api/volume ────────────────────────────────────────────────── */
static esp_err_t handler_volume(httpd_req_t *req)
{
    char buf[64];
    int total = req->content_len;
    if (total <= 0 || total >= (int)sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_FAIL;
    }
    httpd_req_recv(req, buf, total);
    buf[total] = '\0';

    cJSON *root = cJSON_Parse(buf);
    cJSON *vol_item = root ? cJSON_GetObjectItem(root, "volume") : NULL;
    if (vol_item && cJSON_IsNumber(vol_item)) {
        audio_player_set_volume((uint8_t)vol_item->valueint);
    }
    if (root) cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* ── GET /api/files  (list SPIFFS music files) ───────────────────────── */
static esp_err_t handler_files(httpd_req_t *req)
{
    cJSON *root  = cJSON_CreateObject();
    cJSON *files = cJSON_AddArrayToObject(root, "files");

    DIR *dir = opendir("/spiffs");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            /* Only list .mp3 files */
            const char *name = entry->d_name;
            size_t nlen = strlen(name);
            if (nlen > 4 && strcasecmp(name + nlen - 4, ".mp3") == 0) {
                cJSON *obj = cJSON_CreateObject();
                cJSON_AddStringToObject(obj, "name", name);
                char path[64];
                snprintf(path, sizeof(path), "/spiffs/%s", name);
                cJSON_AddStringToObject(obj, "path", path);
                cJSON_AddItemToArray(files, obj);
            }
        }
        closedir(dir);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t ret = httpd_resp_sendstr(req, json_str);
    free(json_str);
    return ret;
}

/* ── WebSocket handler ───────────────────────────────────────────────── */
static esp_err_t handler_ws(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* New WebSocket connection */
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WebSocket client connected: fd=%d", fd);
        ws_client_add(fd);
        return ESP_OK;
    }

    /* Receive frame */
    httpd_ws_frame_t ws_pkt;
    uint8_t buf[256] = {0};
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.payload = buf;
    ws_pkt.type    = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, sizeof(buf) - 1);
    if (ret != ESP_OK) {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGW(TAG, "WS recv error fd=%d: %s", fd, esp_err_to_name(ret));
        ws_client_remove(fd);
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WebSocket client disconnected: fd=%d", fd);
        ws_client_remove(fd);
    }

    return ESP_OK;
}

/* ── Broadcast spectrum via WebSocket ────────────────────────────────── */
void web_server_broadcast_spectrum(const fft_spectrum_t *spectrum)
{
    if (!s_server) return;

    /* Build compact binary message:
       [1 byte type=0x01] [64 x float32 bands] = 257 bytes */
    static uint8_t msg[1 + FFT_BANDS * sizeof(float)];
    msg[0] = 0x01;   /* type: spectrum */
    memcpy(msg + 1, spectrum->bands, FFT_BANDS * sizeof(float));

    httpd_ws_frame_t ws_pkt = {
        .final   = true,
        .fragmented = false,
        .type    = HTTPD_WS_TYPE_BINARY,
        .payload = msg,
        .len     = sizeof(msg),
    };

    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] < 0) continue;
        esp_err_t err = httpd_ws_send_frame_async(s_server, s_ws_fds[i], &ws_pkt);
        if (err != ESP_OK) {
            ESP_LOGD(TAG, "WS send failed fd=%d, removing", s_ws_fds[i]);
            s_ws_fds[i] = -1;
        }
    }
    xSemaphoreGive(s_ws_mutex);
}

/* ── Start server ────────────────────────────────────────────────────── */
esp_err_t web_server_start(void)
{
    s_ws_mutex = xSemaphoreCreateMutex();
    if (!s_ws_mutex) return ESP_ERR_NO_MEM;
    ws_clients_init();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = CONFIG_HTTP_SERVER_PORT;
    config.max_open_sockets = MAX_WS_CLIENTS + 4;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    /* Static file handlers */
    httpd_uri_t uri_index = { .uri = "/",           .method = HTTP_GET,  .handler = handler_index };
    httpd_uri_t uri_css   = { .uri = "/style.css",  .method = HTTP_GET,  .handler = handler_css   };
    httpd_uri_t uri_js    = { .uri = "/app.js",     .method = HTTP_GET,  .handler = handler_js    };

    /* API handlers */
    httpd_uri_t uri_status = { .uri = "/api/status", .method = HTTP_GET,  .handler = handler_status };
    httpd_uri_t uri_play   = { .uri = "/api/play",   .method = HTTP_POST, .handler = handler_play   };
    httpd_uri_t uri_pause  = { .uri = "/api/pause",  .method = HTTP_POST, .handler = handler_pause  };
    httpd_uri_t uri_resume = { .uri = "/api/resume", .method = HTTP_POST, .handler = handler_resume };
    httpd_uri_t uri_stop   = { .uri = "/api/stop",   .method = HTTP_POST, .handler = handler_stop   };
    httpd_uri_t uri_volume = { .uri = "/api/volume", .method = HTTP_POST, .handler = handler_volume };
    httpd_uri_t uri_files  = { .uri = "/api/files",  .method = HTTP_GET,  .handler = handler_files  };

    /* WebSocket handler */
    httpd_uri_t uri_ws = {
        .uri          = "/ws",
        .method       = HTTP_GET,
        .handler      = handler_ws,
        .is_websocket = true,
    };

    httpd_register_uri_handler(s_server, &uri_index);
    httpd_register_uri_handler(s_server, &uri_css);
    httpd_register_uri_handler(s_server, &uri_js);
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_play);
    httpd_register_uri_handler(s_server, &uri_pause);
    httpd_register_uri_handler(s_server, &uri_resume);
    httpd_register_uri_handler(s_server, &uri_stop);
    httpd_register_uri_handler(s_server, &uri_volume);
    httpd_register_uri_handler(s_server, &uri_files);
    httpd_register_uri_handler(s_server, &uri_ws);

    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}