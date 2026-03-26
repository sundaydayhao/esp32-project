#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"

#include "camera_server.h"

static const char *TAG = "camera-server";

/* ------------------------------------------------------------------ */
/*  Embedded HTML page (from index.html via CMakeLists EMBED_FILES)    */
/* ------------------------------------------------------------------ */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

/* ------------------------------------------------------------------ */
/*  Camera pin definitions per board model                             */
/* ------------------------------------------------------------------ */
#if defined(CONFIG_CAMERA_MODEL_AI_THINKER)
/* AI-Thinker ESP32-CAM */
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

#elif defined(CONFIG_CAMERA_MODEL_WROVER_KIT)
/* ESP-WROVER-KIT */
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    21
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      19
#define CAM_PIN_D2      18
#define CAM_PIN_D1       5
#define CAM_PIN_D0       4
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

#elif defined(CONFIG_CAMERA_MODEL_ESP_EYE)
/* ESP-EYE */
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     4
#define CAM_PIN_SIOD    18
#define CAM_PIN_SIOC    23
#define CAM_PIN_D7      36
#define CAM_PIN_D6      37
#define CAM_PIN_D5      38
#define CAM_PIN_D4      39
#define CAM_PIN_D3      35
#define CAM_PIN_D2      14
#define CAM_PIN_D1      13
#define CAM_PIN_D0      34
#define CAM_PIN_VSYNC    5
#define CAM_PIN_HREF    27
#define CAM_PIN_PCLK    25

#elif defined(CONFIG_CAMERA_MODEL_M5STACK_PSRAM)
/* M5Stack Camera with PSRAM */
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   15
#define CAM_PIN_XCLK    27
#define CAM_PIN_SIOD    25
#define CAM_PIN_SIOC    23
#define CAM_PIN_D7      19
#define CAM_PIN_D6      36
#define CAM_PIN_D5      18
#define CAM_PIN_D4      39
#define CAM_PIN_D3       5
#define CAM_PIN_D2      34
#define CAM_PIN_D1      35
#define CAM_PIN_D0      32
#define CAM_PIN_VSYNC   22
#define CAM_PIN_HREF    26
#define CAM_PIN_PCLK    21

#elif defined(CONFIG_CAMERA_MODEL_M5STACK_WIDE)
/* M5Stack Camera Wide */
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   15
#define CAM_PIN_XCLK    27
#define CAM_PIN_SIOD    22
#define CAM_PIN_SIOC    23
#define CAM_PIN_D7      19
#define CAM_PIN_D6      36
#define CAM_PIN_D5      18
#define CAM_PIN_D4      39
#define CAM_PIN_D3       5
#define CAM_PIN_D2      34
#define CAM_PIN_D1      35
#define CAM_PIN_D0      32
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    26
#define CAM_PIN_PCLK    21

#else
#error "No camera model selected. Run 'idf.py menuconfig' and select a camera board."
#endif

/* ------------------------------------------------------------------ */
/*  camera_init                                                         */
/* ------------------------------------------------------------------ */
esp_err_t camera_init(void)
{
    camera_config_t config = {
        .pin_pwdn       = CAM_PIN_PWDN,
        .pin_reset      = CAM_PIN_RESET,
        .pin_xclk       = CAM_PIN_XCLK,
        .pin_sccb_sda   = CAM_PIN_SIOD,
        .pin_sccb_scl   = CAM_PIN_SIOC,
        .pin_d7         = CAM_PIN_D7,
        .pin_d6         = CAM_PIN_D6,
        .pin_d5         = CAM_PIN_D5,
        .pin_d4         = CAM_PIN_D4,
        .pin_d3         = CAM_PIN_D3,
        .pin_d2         = CAM_PIN_D2,
        .pin_d1         = CAM_PIN_D1,
        .pin_d0         = CAM_PIN_D0,
        .pin_vsync      = CAM_PIN_VSYNC,
        .pin_href       = CAM_PIN_HREF,
        .pin_pclk       = CAM_PIN_PCLK,

        .xclk_freq_hz   = 20000000,          /* 20 MHz XCLK */
        .ledc_timer     = LEDC_TIMER_0,
        .ledc_channel   = LEDC_CHANNEL_0,

        .pixel_format   = PIXFORMAT_JPEG,    /* JPEG output */
        .frame_size     = CONFIG_CAMERA_FRAME_SIZE,
        .jpeg_quality   = CONFIG_CAMERA_JPEG_QUALITY,
        .fb_count       = 2,                 /* double-buffer */
        .grab_mode      = CAMERA_GRAB_WHEN_EMPTY,
        .fb_location    = CAMERA_FB_IN_PSRAM,
    };

    /* Power up the camera if PWDN pin is defined */
    if (CAM_PIN_PWDN != -1) {
        /* gpio_set_direction is called internally by esp_camera_init */
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: 0x%x (%s)",
                 err, esp_err_to_name(err));
        return err;
    }

    /* Tune sensor settings for close-up PCB inspection */
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_brightness(s,  1);   /* slightly brighter */
        s->set_contrast(s,    1);   /* more contrast for text */
        s->set_saturation(s,  0);
        s->set_sharpness(s,   2);   /* sharper edges */
        s->set_denoise(s,     1);
        s->set_whitebal(s,    1);   /* auto white balance */
        s->set_awb_gain(s,    1);
        s->set_exposure_ctrl(s, 1); /* auto exposure */
        s->set_aec2(s,        1);
        s->set_gain_ctrl(s,   1);   /* auto gain */
        s->set_agc_gain(s,    0);
        s->set_gainceiling(s, GAINCEILING_4X);
        s->set_bpc(s,         1);   /* black pixel correction */
        s->set_wpc(s,         1);   /* white pixel correction */
        s->set_raw_gma(s,     1);
        s->set_lenc(s,        1);   /* lens correction */
        s->set_hmirror(s,     0);
        s->set_vflip(s,       0);
        s->set_dcw(s,         1);
        s->set_colorbar(s,    0);
    }

    ESP_LOGI(TAG, "Camera initialized. Frame size: %d, JPEG quality: %d",
             CONFIG_CAMERA_FRAME_SIZE, CONFIG_CAMERA_JPEG_QUALITY);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  HTTP handler: GET /  →  serve index.html                           */
/* ------------------------------------------------------------------ */
static esp_err_t index_handler(httpd_req_t *req)
{
    size_t html_len = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "identity");
    httpd_resp_send(req, (const char *)index_html_start, html_len);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  HTTP handler: GET /capture  →  single JPEG snapshot               */
/* ------------------------------------------------------------------ */
static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

/* ------------------------------------------------------------------ */
/*  HTTP handler: GET /stream  →  MJPEG stream                        */
/* ------------------------------------------------------------------ */
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %lld\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb        = NULL;
    esp_err_t    res       = ESP_OK;
    char         part_buf[128];
    int64_t      last_frame = 0;

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "30");

    ESP_LOGI(TAG, "MJPEG stream started");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed during stream");
            res = ESP_FAIL;
            break;
        }

        int64_t now = esp_timer_get_time();

        /* Send boundary */
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY,
                                    strlen(STREAM_BOUNDARY));
        if (res != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }

        /* Send part header */
        size_t hlen = snprintf(part_buf, sizeof(part_buf),
                               STREAM_PART, fb->len, now);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }

        /* Send JPEG data */
        res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        esp_camera_fb_return(fb);
        if (res != ESP_OK) break;

        /* Throttle to ~15 fps to avoid overwhelming the network */
        int64_t elapsed = esp_timer_get_time() - last_frame;
        if (elapsed < 66000) { /* 66 ms = ~15 fps */
            vTaskDelay(pdMS_TO_TICKS((66000 - elapsed) / 1000));
        }
        last_frame = esp_timer_get_time();
    }

    ESP_LOGI(TAG, "MJPEG stream ended");
    return res;
}

/* ------------------------------------------------------------------ */
/*  HTTP handler: GET /status  →  JSON status                         */
/* ------------------------------------------------------------------ */
static esp_err_t status_handler(httpd_req_t *req)
{
    sensor_t *s = esp_camera_sensor_get();
    char json[256];
    int len = snprintf(json, sizeof(json),
        "{"
        "\"framesize\":%d,"
        "\"quality\":%d,"
        "\"brightness\":%d,"
        "\"contrast\":%d,"
        "\"sharpness\":%d,"
        "\"hmirror\":%d,"
        "\"vflip\":%d"
        "}",
        s ? s->status.framesize  : 0,
        s ? s->status.quality    : 0,
        s ? s->status.brightness : 0,
        s ? s->status.contrast   : 0,
        s ? s->status.sharpness  : 0,
        s ? s->status.hmirror    : 0,
        s ? s->status.vflip      : 0
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json, len);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  HTTP handler: GET /control?var=xxx&val=yyy  →  camera settings    */
/* ------------------------------------------------------------------ */
static esp_err_t control_handler(httpd_req_t *req)
{
    char var[32] = {0};
    char val[16] = {0};
    char query[128];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_query_key_value(query, "var", var, sizeof(var));
    httpd_query_key_value(query, "val", val, sizeof(val));

    int value = atoi(val);
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int res = 0;
    if      (!strcmp(var, "framesize"))  res = s->set_framesize(s, (framesize_t)value);
    else if (!strcmp(var, "quality"))    res = s->set_quality(s, value);
    else if (!strcmp(var, "brightness")) res = s->set_brightness(s, value);
    else if (!strcmp(var, "contrast"))   res = s->set_contrast(s, value);
    else if (!strcmp(var, "sharpness"))  res = s->set_sharpness(s, value);
    else if (!strcmp(var, "hmirror"))    res = s->set_hmirror(s, value);
    else if (!strcmp(var, "vflip"))      res = s->set_vflip(s, value);
    else if (!strcmp(var, "awb"))        res = s->set_whitebal(s, value);
    else if (!strcmp(var, "aec"))        res = s->set_exposure_ctrl(s, value);
    else if (!strcmp(var, "agc"))        res = s->set_gain_ctrl(s, value);
    else {
        ESP_LOGW(TAG, "Unknown control variable: %s", var);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    if (res != 0) {
        ESP_LOGE(TAG, "Control failed: %s = %d", var, value);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Control: %s = %d", var, value);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  start_camera_server                                                 */
/* ------------------------------------------------------------------ */
esp_err_t start_camera_server(void)
{
    httpd_handle_t server = NULL;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = CONFIG_HTTP_SERVER_PORT;
    config.ctrl_port        = 32768;
    config.max_open_sockets = 5;
    config.max_uri_handlers = 8;
    config.stack_size       = 8192;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    /* Register URI handlers */
    httpd_uri_t uri_index = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = index_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &uri_index);

    httpd_uri_t uri_capture = {
        .uri      = "/capture",
        .method   = HTTP_GET,
        .handler  = capture_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &uri_capture);

    httpd_uri_t uri_stream = {
        .uri      = "/stream",
        .method   = HTTP_GET,
        .handler  = stream_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &uri_stream);

    httpd_uri_t uri_status = {
        .uri      = "/status",
        .method   = HTTP_GET,
        .handler  = status_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &uri_status);

    httpd_uri_t uri_control = {
        .uri      = "/control",
        .method   = HTTP_GET,
        .handler  = control_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &uri_control);

    ESP_LOGI(TAG, "Camera server started.");
    ESP_LOGI(TAG, "  /         - Web UI (magnifier)");
    ESP_LOGI(TAG, "  /stream   - MJPEG stream");
    ESP_LOGI(TAG, "  /capture  - Single JPEG snapshot");
    ESP_LOGI(TAG, "  /status   - Camera status JSON");
    ESP_LOGI(TAG, "  /control  - Camera control API");

    return ESP_OK;
}