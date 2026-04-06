#include "wechat_sender.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "img_converters.h"
#include "mbedtls/base64.h"
#include "mbedtls/md5.h"
#include "sdkconfig.h"

static const char *TAG = "wechat_sender";
static char s_http_response[512];
static size_t s_http_response_len = 0;
#define WECOM_API_URL \
    "https://qyapi.weixin.qq.com/cgi-bin/webhook/send?key=" CONFIG_WECOM_WEBHOOK_KEY

#define SERVERCHAN_API_URL \
    "https://sctapi.ftqq.com/" CONFIG_SERVERCHAN_SENDKEY ".send"

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            s_http_response_len = 0;
            s_http_response[0] = '\0';
            ESP_LOGD(TAG, "HTTP connected");
            break;

        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP request header sent");
            break;

        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP response header: %s: %s", evt->header_key, evt->header_value);
            break;

        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0 && s_http_response_len < sizeof(s_http_response) - 1) {
                size_t remaining = sizeof(s_http_response) - 1 - s_http_response_len;
                size_t copy_len = evt->data_len < remaining ? (size_t)evt->data_len : remaining;
                memcpy(s_http_response + s_http_response_len, evt->data, copy_len);
                s_http_response_len += copy_len;
                s_http_response[s_http_response_len] = '\0';
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            if (s_http_response_len > 0) {
                ESP_LOGI(TAG, "HTTP response: %s", s_http_response);
            }
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP disconnected");
            break;

        default:
            break;
    }

    return ESP_OK;
}

static void drain_http_response(esp_http_client_handle_t client)
{
    char scratch[128];

    while (esp_http_client_read(client, scratch, sizeof(scratch)) > 0) {
    }
}

static void calc_md5_hex(const uint8_t *data, size_t len, char *md5_hex_out)
{
    uint8_t md5_bytes[16];
    mbedtls_md5_context ctx;

    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts(&ctx);
    mbedtls_md5_update(&ctx, data, len);
    mbedtls_md5_finish(&ctx, md5_bytes);
    mbedtls_md5_free(&ctx);

    for (int i = 0; i < 16; i++) {
        sprintf(md5_hex_out + i * 2, "%02x", md5_bytes[i]);
    }
    md5_hex_out[32] = '\0';
}

static void *alloc_image_buffer(size_t size)
{
    void *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = malloc(size);
    }

    return buf;
}

static void *realloc_image_buffer(void *ptr, size_t size)
{
    void *buf = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = realloc(ptr, size);
    }

    return buf;
}

static bool image_is_jpeg(const uint8_t *data, size_t len)
{
    return len >= 4 &&
           data[0] == 0xff &&
           data[1] == 0xd8 &&
           data[len - 2] == 0xff &&
           data[len - 1] == 0xd9;
}

typedef struct {
    uint8_t *data;
    size_t len;
    size_t capacity;
} jpeg_buffer_t;

static size_t jpeg_buffer_append(void *arg, size_t index, const void *data, size_t len)
{
    (void)index;

    jpeg_buffer_t *buffer = (jpeg_buffer_t *)arg;
    if (buffer == NULL || data == NULL || len == 0) {
        return 0;
    }

    size_t required = buffer->len + len;
    if (required > buffer->capacity) {
        size_t new_capacity = buffer->capacity == 0 ? 65536 : buffer->capacity;
        while (new_capacity < required) {
            if (new_capacity > SIZE_MAX / 2) {
                return 0;
            }
            new_capacity *= 2;
        }

        uint8_t *new_data = (uint8_t *)realloc_image_buffer(buffer->data, new_capacity);
        if (new_data == NULL) {
            return 0;
        }

        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }

    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    return len;
}

static esp_err_t encode_frame_to_jpeg_dynamic(uint8_t *src,
                                              size_t src_len,
                                              uint16_t width,
                                              uint16_t height,
                                              pixformat_t format,
                                              uint8_t quality,
                                              uint8_t **out,
                                              size_t *out_len)
{
    if (src == NULL || out == NULL || out_len == NULL || width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    *out = NULL;
    *out_len = 0;

    jpeg_buffer_t buffer = {0};

    jpgSetChroma(CHROMA_444);
    jpgSetRgb565BE(true);

    if (!fmt2jpg_cb(src, src_len, width, height, format, quality, jpeg_buffer_append, &buffer)) {
        free(buffer.data);
        return ESP_FAIL;
    }

    if (!image_is_jpeg(buffer.data, buffer.len)) {
        free(buffer.data);
        return ESP_FAIL;
    }

    *out = buffer.data;
    *out_len = buffer.len;
    return ESP_OK;
}


#ifdef CONFIG_WECHAT_PUSH_WECOM
static const size_t WECOM_IMAGE_BASE64_LIMIT_BYTES = 2 * 1024 * 1024;

typedef struct {
    esp_jpeg_image_scale_t scale;
    uint8_t quality;
} wecom_jpeg_candidate_t;

static const wecom_jpeg_candidate_t s_wecom_jpeg_candidates[] = {
    {JPEG_IMAGE_SCALE_0, 85},
    {JPEG_IMAGE_SCALE_0, 72},
    {JPEG_IMAGE_SCALE_1_2, 85},
    {JPEG_IMAGE_SCALE_1_2, 72},
    {JPEG_IMAGE_SCALE_1_4, 85},
    {JPEG_IMAGE_SCALE_1_4, 72},
};

static size_t base64_encoded_length(size_t binary_len)
{
    if (binary_len > SIZE_MAX - 2) {
        return SIZE_MAX;
    }

    return ((binary_len + 2) / 3) * 4;
}

static bool wecom_image_size_allowed(size_t image_len)
{
    return base64_encoded_length(image_len) <= WECOM_IMAGE_BASE64_LIMIT_BYTES;
}

static uint16_t jpeg_scaled_dimension(uint16_t value, esp_jpeg_image_scale_t scale)
{
    uint16_t divisor = 1;

    switch (scale) {
        case JPEG_IMAGE_SCALE_1_2:
            divisor = 2;
            break;
        case JPEG_IMAGE_SCALE_1_4:
            divisor = 4;
            break;
        case JPEG_IMAGE_SCALE_1_8:
            divisor = 8;
            break;
        case JPEG_IMAGE_SCALE_0:
        default:
            divisor = 1;
            break;
    }

    if (value == 0) {
        return 0;
    }

    value = (uint16_t)(value / divisor);
    return value > 0 ? value : 1;
}

static esp_err_t jpeg_get_dimensions_with_fallback(const camera_fb_t *frame,
                                                   esp_jpeg_image_scale_t scale,
                                                   uint16_t *width_out,
                                                   uint16_t *height_out)
{
    if (frame == NULL || width_out == NULL || height_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *width_out = 0;
    *height_out = 0;

    esp_jpeg_image_cfg_t jpeg_cfg = {
        .indata = frame->buf,
        .indata_size = frame->len,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = scale,
        .flags.swap_color_bytes = 0,
        .advanced.working_buffer = NULL,
        .advanced.working_buffer_size = 0,
    };
    esp_jpeg_image_output_t jpeg_info = {0};
    esp_err_t ret = esp_jpeg_get_image_info(&jpeg_cfg, &jpeg_info);

    if (ret == ESP_OK && jpeg_info.width != 0 && jpeg_info.height != 0) {
        *width_out = jpeg_info.width;
        *height_out = jpeg_info.height;
        return ESP_OK;
    }

    if (frame->width == 0 || frame->height == 0) {
        ESP_LOGE(TAG, "Failed to parse JPEG dimensions: %s", esp_err_to_name(ret));
        return ret != ESP_OK ? ret : ESP_FAIL;
    }

    *width_out = jpeg_scaled_dimension((uint16_t)frame->width, scale);
    *height_out = jpeg_scaled_dimension((uint16_t)frame->height, scale);
    ESP_LOGW(TAG,
             "JPEG header parsing failed (%s), falling back to frame metadata %zux%zu",
             esp_err_to_name(ret), frame->width, frame->height);
    return ESP_OK;
}

static esp_err_t reencode_jpeg_frame_for_wecom(const camera_fb_t *frame,
                                               esp_jpeg_image_scale_t scale,
                                               uint8_t quality,
                                               uint8_t **image_out,
                                               size_t *image_len_out)
{
    if (frame == NULL || image_out == NULL || image_len_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *image_out = NULL;
    *image_len_out = 0;

    uint16_t decoded_width = 0;
    uint16_t decoded_height = 0;
    esp_err_t ret = jpeg_get_dimensions_with_fallback(frame, scale, &decoded_width, &decoded_height);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t pixel_count = (size_t)decoded_width * (size_t)decoded_height;
    if (pixel_count == 0 || pixel_count > SIZE_MAX / 2) {
        ESP_LOGE(TAG, "Invalid RGB565 buffer size for normalization: %ux%u",
                 decoded_width, decoded_height);
        return ESP_FAIL;
    }

    size_t rgb565_len = pixel_count * 2;
    uint8_t *rgb565_buf = alloc_image_buffer(rgb565_len);
    if (rgb565_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate RGB565 buffer for normalization: %zu bytes",
                 rgb565_len);
        return ESP_ERR_NO_MEM;
    }

    if (!jpg2rgb565(frame->buf, frame->len, rgb565_buf, scale)) {
        ESP_LOGE(TAG, "Failed to decode JPEG for normalization");
        free(rgb565_buf);
        return ESP_FAIL;
    }

    ret = encode_frame_to_jpeg_dynamic(rgb565_buf, rgb565_len,
                                       decoded_width, decoded_height,
                                       PIXFORMAT_RGB565, quality,
                                       image_out, image_len_out);
    free(rgb565_buf);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG,
             "Re-encoded JPEG candidate: %ux%u quality=%u size=%zu bytes base64=%zu bytes",
             decoded_width, decoded_height, quality, *image_len_out,
             base64_encoded_length(*image_len_out));
    return ESP_OK;
}

static esp_err_t prepare_frame_for_wecom_upload(const camera_fb_t *frame,
                                                uint8_t **image_out,
                                                size_t *image_len_out)
{
    if (frame == NULL || image_out == NULL || image_len_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *image_out = NULL;
    *image_len_out = 0;

    if (frame->format == PIXFORMAT_JPEG) {
        for (size_t i = 0; i < sizeof(s_wecom_jpeg_candidates) / sizeof(s_wecom_jpeg_candidates[0]); i++) {
            const wecom_jpeg_candidate_t candidate = s_wecom_jpeg_candidates[i];
            uint8_t *candidate_image = NULL;
            size_t candidate_len = 0;

            esp_err_t ret = reencode_jpeg_frame_for_wecom(frame,
                                                          candidate.scale,
                                                          candidate.quality,
                                                          &candidate_image,
                                                          &candidate_len);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "JPEG fallback candidate failed: scale=%d quality=%u err=%s",
                         candidate.scale, candidate.quality, esp_err_to_name(ret));
                continue;
            }

            if (wecom_image_size_allowed(candidate_len)) {
                *image_out = candidate_image;
                *image_len_out = candidate_len;
                return ESP_OK;
            }

            ESP_LOGW(TAG,
                     "JPEG fallback candidate still too large: scale=%d quality=%u size=%zu bytes base64=%zu bytes",
                     candidate.scale, candidate.quality, candidate_len,
                     base64_encoded_length(candidate_len));
            free(candidate_image);
        }

        ESP_LOGE(TAG, "Unable to fit JPEG into WeCom upload size limit after recompression");
        return ESP_ERR_INVALID_SIZE;
    }

    if (encode_frame_to_jpeg_dynamic(frame->buf, frame->len,
                                     (uint16_t)frame->width, (uint16_t)frame->height,
                                     frame->format, 85,
                                     image_out, image_len_out) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to convert frame format %d to JPEG", frame->format);
        return ESP_FAIL;
    }

    if (!wecom_image_size_allowed(*image_len_out)) {
        ESP_LOGE(TAG, "Converted non-JPEG frame is too large for WeCom upload");
        free(*image_out);
        *image_out = NULL;
        *image_len_out = 0;
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}


static esp_err_t validate_wecom_webhook_config(void)
{
    if (CONFIG_WECOM_WEBHOOK_KEY[0] == '\0') {
        ESP_LOGE(TAG, "WeCom webhook key is empty. Set it in menuconfig first.");
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

static bool wecom_parse_errcode(long *errcode_out)
{
    if (errcode_out == NULL) {
        return false;
    }

    const char *errcode_key = strstr(s_http_response, "\"errcode\"");
    if (errcode_key == NULL) {
        ESP_LOGE(TAG, "WeCom response missing errcode: %s",
                 s_http_response_len > 0 ? s_http_response : "<empty>");
        return false;
    }

    const char *colon = strchr(errcode_key, ':');
    if (colon == NULL) {
        ESP_LOGE(TAG, "WeCom response format is invalid: %s", s_http_response);
        return false;
    }

    *errcode_out = strtol(colon + 1, NULL, 10);
    return true;
}

static bool wecom_response_is_success(int status_code, long *errcode_out)
{
    if (status_code != 200) {
        if (errcode_out != NULL) {
            *errcode_out = LONG_MIN;
        }
        return false;
    }

    long errcode = 0;
    if (!wecom_parse_errcode(&errcode)) {
        if (errcode_out != NULL) {
            *errcode_out = LONG_MIN;
        }
        return false;
    }

    if (errcode_out != NULL) {
        *errcode_out = errcode;
    }

    if (errcode != 0) {
        ESP_LOGE(TAG, "WeCom API returned errcode=%ld, body=%s", errcode, s_http_response);
        return false;
    }

    return true;
}

static esp_err_t normalize_frame_for_wecom(const camera_fb_t *frame,
                                           uint8_t **image_out,
                                           size_t *image_len_out)
{
    return prepare_frame_for_wecom_upload(frame, image_out, image_len_out);
}

static esp_err_t wecom_send_image_bytes(const uint8_t *image_data,
                                        size_t image_len,
                                        long *errcode_out)
{
    esp_err_t ret = validate_wecom_webhook_config();
    if (ret != ESP_OK) {
        return ret;
    }

    char md5_hex[33];
    calc_md5_hex(image_data, image_len, md5_hex);
    ESP_LOGI(TAG, "Image MD5: %s", md5_hex);

    size_t base64_len = base64_encoded_length(image_len);
    ESP_LOGI(TAG, "Image size: %zu bytes, base64 size: %zu bytes", image_len, base64_len);

    if (base64_len > WECOM_IMAGE_BASE64_LIMIT_BYTES) {
        ESP_LOGE(TAG, "Image is too large after base64 encoding: %zu bytes", base64_len);
        return ESP_ERR_INVALID_SIZE;
    }

    char *base64_buf = (char *)alloc_image_buffer(base64_len + 1);
    if (base64_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate base64 buffer: %zu bytes", base64_len + 1);
        return ESP_ERR_NO_MEM;
    }

    size_t actual_base64_len = 0;
    int rc = mbedtls_base64_encode((unsigned char *)base64_buf, base64_len + 1,
                                   &actual_base64_len, image_data, image_len);
    if (rc != 0) {
        ESP_LOGE(TAG, "Base64 encode failed: %d", rc);
        free(base64_buf);
        return ESP_FAIL;
    }
    base64_buf[actual_base64_len] = '\0';

    const char *json_prefix = "{\"msgtype\":\"image\",\"image\":{\"base64\":\"";
    const char *json_suffix_fmt = "\",\"md5\":\"%s\"}}";
    char json_suffix[64];

    snprintf(json_suffix, sizeof(json_suffix), json_suffix_fmt, md5_hex);

    size_t prefix_len = strlen(json_prefix);
    size_t suffix_len = strlen(json_suffix);
    size_t total_len = prefix_len + actual_base64_len + suffix_len;

    esp_http_client_config_t config = {
        .url = WECOM_API_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        free(base64_buf);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");

    ret = esp_http_client_open(client, (int)total_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    int written = esp_http_client_write(client, json_prefix, (int)prefix_len);
    if (written < 0) {
        ESP_LOGE(TAG, "Failed to write JSON prefix");
        ret = ESP_FAIL;
        goto cleanup;
    }

    const int chunk_size = 4096;
    size_t sent = 0;
    while (sent < actual_base64_len) {
        size_t to_send = actual_base64_len - sent;
        if (to_send > (size_t)chunk_size) {
            to_send = (size_t)chunk_size;
        }

        written = esp_http_client_write(client, base64_buf + sent, (int)to_send);
        if (written < 0) {
            ESP_LOGE(TAG, "Failed to write base64 body at %zu/%zu", sent, actual_base64_len);
            ret = ESP_FAIL;
            goto cleanup;
        }
        sent += (size_t)written;
    }

    written = esp_http_client_write(client, json_suffix, (int)suffix_len);
    if (written < 0) {
        ESP_LOGE(TAG, "Failed to write JSON suffix");
        ret = ESP_FAIL;
        goto cleanup;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    drain_http_response(client);

    ESP_LOGI(TAG, "HTTP status=%d, content_length=%d", status_code, content_length);

    if (wecom_response_is_success(status_code, errcode_out)) {
        ESP_LOGI(TAG, "Image sent to WeCom successfully");
        ret = ESP_OK;
    } else {
        ESP_LOGE(TAG, "Image send failed, HTTP status=%d", status_code);
        ret = ESP_FAIL;
    }

cleanup:
    free(base64_buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ret;
}

static esp_err_t wecom_send_text(const char *message)
{
    esp_err_t ret = validate_wecom_webhook_config();
    if (ret != ESP_OK) {
        return ret;
    }

    char json_body[512];
    snprintf(json_body, sizeof(json_body),
             "{\"msgtype\":\"text\",\"text\":{\"content\":\"%s\"}}",
             message);

    esp_http_client_config_t config = {
        .url = WECOM_API_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Text send finished, HTTP status=%d", status);
        ret = wecom_response_is_success(status, NULL) ? ESP_OK : ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Text send failed: %s", esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    return ret;
}

#endif

#ifdef CONFIG_WECHAT_PUSH_SERVERCHAN

static esp_err_t serverchan_send_text(const char *title, const char *content);

static esp_err_t serverchan_send_image(const uint8_t *image_data, size_t image_len)
{
    ESP_LOGW(TAG, "ServerChan does not support raw image upload, falling back to text");

    char message[256];
    snprintf(message, sizeof(message),
             "Gas meter capture notification\n\n"
             "- Status: image captured\n"
             "- Image size: %zu bytes\n"
             "- Check the device log if you need the raw image",
             image_len);

    return serverchan_send_text("Gas meter capture", message);
}

static esp_err_t serverchan_send_text(const char *title, const char *content)
{
    char post_data[1024];
    snprintf(post_data, sizeof(post_data), "title=%s&desp=%s", title, content);

    esp_http_client_config_t config = {
        .url = SERVERCHAN_API_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type",
                               "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "ServerChan send finished, HTTP status=%d", status);
        ret = (status == 200) ? ESP_OK : ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "ServerChan send failed: %s", esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    return ret;
}

#endif

esp_err_t wechat_send_image(const uint8_t *image_data, size_t image_len)
{
    if (image_data == NULL || image_len == 0) {
        ESP_LOGE(TAG, "Invalid image data");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Preparing to send image, size=%zu bytes", image_len);

#ifdef CONFIG_WECHAT_PUSH_WECOM
    return wecom_send_image_bytes(image_data, image_len, NULL);
#elif defined(CONFIG_WECHAT_PUSH_SERVERCHAN)
    return serverchan_send_image(image_data, image_len);
#else
    ESP_LOGE(TAG, "No WeChat push method configured");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t wechat_send_camera_frame(const camera_fb_t *frame)
{
    if (frame == NULL || frame->buf == NULL || frame->len == 0) {
        ESP_LOGE(TAG, "Invalid camera frame");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Preparing to send frame: %zux%zu format=%d size=%zu bytes",
             frame->width, frame->height, frame->format, frame->len);

#ifdef CONFIG_WECHAT_PUSH_WECOM
    uint8_t *normalized = NULL;
    size_t normalized_len = 0;
    ESP_LOGI(TAG, "Normalizing frame for WeCom upload");
    esp_err_t ret = normalize_frame_for_wecom(frame, &normalized, &normalized_len);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = wecom_send_image_bytes(normalized, normalized_len, NULL);
    free(normalized);
    return ret;
#else
    return wechat_send_image(frame->buf, frame->len);
#endif
}

esp_err_t wechat_send_text(const char *message)
{
    if (message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef CONFIG_WECHAT_PUSH_WECOM
    return wecom_send_text(message);
#elif defined(CONFIG_WECHAT_PUSH_SERVERCHAN)
    return serverchan_send_text("Gas meter notification", message);
#else
    ESP_LOGE(TAG, "No WeChat push method configured");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
