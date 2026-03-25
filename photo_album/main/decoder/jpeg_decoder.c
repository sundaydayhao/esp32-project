/**
 * @file jpeg_decoder.c
 * @brief JPEG decoder wrapper built on top of the esp_jpeg/TJpgDec component.
 */
#include "photo_jpeg_decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "jpeg_decoder.h"

#if CONFIG_JD_USE_ROM
#include "rom/tjpgd.h"
typedef UINT jpeg_decode_in_t;
typedef UINT jpeg_decode_out_t;
#define PHOTO_JD_FORMAT 0
#define PHOTO_JPEG_WORKBUF_SIZE 3100
#else
#include "tjpgd.h"
typedef size_t jpeg_decode_in_t;
typedef int jpeg_decode_out_t;
#define PHOTO_JD_FORMAT JD_FORMAT
#if defined(JD_FASTDECODE) && (JD_FASTDECODE == 2)
#define PHOTO_JPEG_WORKBUF_SIZE 65472
#else
#define PHOTO_JPEG_WORKBUF_SIZE 3100
#endif
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
    bool progressive;
} jpeg_header_info_t;

typedef struct {
    FILE *file;
    uint16_t *fb;
    uint16_t bg_color;
    uint16_t src_w;
    uint16_t src_h;
    int dst_w;
    int dst_h;
    int off_x;
    int off_y;
    int16_t *x0_map;
    int16_t *x1_map;
    int16_t *y0_map;
    int16_t *y1_map;
} jpeg_decode_ctx_t;

static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void *malloc_prefer_spiram(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = malloc(size);
    }
    return ptr;
}

static inline uint16_t read_be16_buf(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static bool read_be16_file(FILE *f, uint16_t *out)
{
    uint8_t tmp[2];
    if (fread(tmp, 1, sizeof(tmp), f) != sizeof(tmp)) {
        return false;
    }
    *out = read_be16_buf(tmp);
    return true;
}

static uint8_t jpeg_scale_divisor(esp_jpeg_image_scale_t scale)
{
    switch (scale) {
    case JPEG_IMAGE_SCALE_1_2:
        return 2;
    case JPEG_IMAGE_SCALE_1_4:
        return 4;
    case JPEG_IMAGE_SCALE_1_8:
        return 8;
    case JPEG_IMAGE_SCALE_0:
    default:
        return 1;
    }
}

static void calc_scale(uint16_t src_w, uint16_t src_h,
                       uint16_t dst_w, uint16_t dst_h,
                       scale_mode_t mode,
                       int *out_w, int *out_h,
                       int *off_x, int *off_y)
{
    float scale_x = (float)dst_w / src_w;
    float scale_y = (float)dst_h / src_h;
    float scale;

    switch (mode) {
    case SCALE_FIT:
        scale = (scale_x < scale_y) ? scale_x : scale_y;
        break;
    case SCALE_FILL:
        scale = (scale_x > scale_y) ? scale_x : scale_y;
        break;
    case SCALE_STRETCH:
    default:
        *out_w = dst_w;
        *out_h = dst_h;
        *off_x = 0;
        *off_y = 0;
        return;
    }

    *out_w = (int)(src_w * scale);
    *out_h = (int)(src_h * scale);
    *off_x = (dst_w - *out_w) / 2;
    *off_y = (dst_h - *out_h) / 2;
}

static esp_err_t inspect_jpeg_file(FILE *f, jpeg_header_info_t *info)
{
    if (!f || !info) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));
    rewind(f);

    uint16_t soi = 0;
    if (!read_be16_file(f, &soi) || soi != 0xFFD8) {
        return ESP_FAIL;
    }

    while (true) {
        int prefix;
        do {
            prefix = fgetc(f);
            if (prefix == EOF) {
                return ESP_FAIL;
            }
        } while (prefix != 0xFF);

        int marker;
        do {
            marker = fgetc(f);
            if (marker == EOF) {
                return ESP_FAIL;
            }
        } while (marker == 0xFF);

        if (marker == 0x00) {
            continue;
        }
        if (marker == 0xD8 || marker == 0xD9 ||
            (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            continue;
        }
        if (marker == 0xDA) {
            break;
        }

        uint16_t seg_len = 0;
        if (!read_be16_file(f, &seg_len) || seg_len < 2) {
            return ESP_FAIL;
        }

        if (marker >= 0xC0 && marker <= 0xCF &&
            marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            uint8_t sof[5];
            if (seg_len < 7 || fread(sof, 1, sizeof(sof), f) != sizeof(sof)) {
                return ESP_FAIL;
            }

            info->height = read_be16_buf(&sof[1]);
            info->width = read_be16_buf(&sof[3]);
            info->progressive = (marker == 0xC2);
            return info->progressive ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
        }

        if (fseek(f, (long)seg_len - 2L, SEEK_CUR) != 0) {
            return ESP_FAIL;
        }
    }

    return ESP_FAIL;
}

static esp_jpeg_image_scale_t choose_decode_scale(uint16_t src_w, uint16_t src_h,
                                                  scale_mode_t mode)
{
    int target_w = 0;
    int target_h = 0;
    int off_x = 0;
    int off_y = 0;

    calc_scale(src_w, src_h, LCD_H_RES, LCD_V_RES, mode,
               &target_w, &target_h, &off_x, &off_y);

    const struct {
        esp_jpeg_image_scale_t scale;
        uint8_t div;
    } candidates[] = {
        { JPEG_IMAGE_SCALE_1_8, 8 },
        { JPEG_IMAGE_SCALE_1_4, 4 },
        { JPEG_IMAGE_SCALE_1_2, 2 },
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if ((src_w / candidates[i].div) >= target_w &&
            (src_h / candidates[i].div) >= target_h) {
            return candidates[i].scale;
        }
    }

    return JPEG_IMAGE_SCALE_0;
}

static void build_axis_map(int src_len, int dst_len, int offset, int limit,
                           int16_t *start_map, int16_t *end_map)
{
    for (int i = 0; i < src_len; i++) {
        int start = offset + (i * dst_len) / src_len;
        int end = offset + ((i + 1) * dst_len) / src_len - 1;

        if (end < 0 || start >= limit) {
            start_map[i] = 1;
            end_map[i] = 0;
            continue;
        }

        if (start < 0) {
            start = 0;
        }
        if (end >= limit) {
            end = limit - 1;
        }

        start_map[i] = (int16_t)start;
        end_map[i] = (int16_t)end;
    }
}

static jpeg_decode_in_t jpeg_file_in_cb(JDEC *jd, uint8_t *buf, jpeg_decode_in_t len)
{
    jpeg_decode_ctx_t *ctx = (jpeg_decode_ctx_t *)jd->device;
    if (!ctx || !ctx->file) {
        return 0;
    }

    if (buf) {
        return (jpeg_decode_in_t)fread(buf, 1, len, ctx->file);
    }

    return (fseek(ctx->file, (long)len, SEEK_CUR) == 0) ? len : 0;
}

static inline uint16_t jpeg_bitmap_color_at(const void *bitmap, int index)
{
#if CONFIG_JD_USE_ROM || (PHOTO_JD_FORMAT == 0)
    const uint8_t *rgb = (const uint8_t *)bitmap + index * 3;
    return rgb888_to_rgb565(rgb[0], rgb[1], rgb[2]);
#else
    const uint16_t *rgb565 = (const uint16_t *)bitmap;
    return rgb565[index];
#endif
}

static jpeg_decode_out_t jpeg_fb_out_cb(JDEC *jd, void *bitmap, JRECT *rect)
{
    jpeg_decode_ctx_t *ctx = (jpeg_decode_ctx_t *)jd->device;
    if (!ctx || !bitmap || !rect) {
        return 0;
    }

    const int rect_w = (int)rect->right - (int)rect->left + 1;
    const int rect_h = (int)rect->bottom - (int)rect->top + 1;

    for (int ry = 0; ry < rect_h; ry++) {
        int src_y = (int)rect->top + ry;
        int y0 = ctx->y0_map[src_y];
        int y1 = ctx->y1_map[src_y];
        if (y1 < y0) {
            continue;
        }

        for (int rx = 0; rx < rect_w; rx++) {
            int src_x = (int)rect->left + rx;
            int x0 = ctx->x0_map[src_x];
            int x1 = ctx->x1_map[src_x];
            if (x1 < x0) {
                continue;
            }

            uint16_t color = jpeg_bitmap_color_at(bitmap, ry * rect_w + rx);
            for (int y = y0; y <= y1; y++) {
                uint16_t *row = ctx->fb + y * LCD_H_RES;
                for (int x = x0; x <= x1; x++) {
                    row[x] = color;
                }
            }
        }
    }

    return 1;
}

static esp_err_t decode_jpeg_stream_to_fb(FILE *f, const jpeg_header_info_t *header,
                                          uint16_t *fb, scale_mode_t mode,
                                          uint16_t bg_color, decode_info_t *info)
{
    if (!f || !header || !fb) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_FAIL;
    JDEC jd;
    jpeg_decode_ctx_t ctx = {
        .file = f,
        .fb = fb,
        .bg_color = bg_color,
    };
    void *workbuf = NULL;

    const esp_jpeg_image_scale_t decode_scale = choose_decode_scale(header->width, header->height, mode);
    const uint8_t decode_div = jpeg_scale_divisor(decode_scale);

    ctx.src_w = header->width / decode_div;
    ctx.src_h = header->height / decode_div;
    calc_scale(header->width, header->height, LCD_H_RES, LCD_V_RES, mode,
               &ctx.dst_w, &ctx.dst_h, &ctx.off_x, &ctx.off_y);

    if (ctx.src_w == 0 || ctx.src_h == 0 || ctx.dst_w <= 0 || ctx.dst_h <= 0) {
        return ESP_FAIL;
    }

    uint32_t total = LCD_H_RES * LCD_V_RES;
    for (uint32_t i = 0; i < total; i++) {
        fb[i] = bg_color;
    }

    ctx.x0_map = malloc((size_t)ctx.src_w * sizeof(int16_t));
    ctx.x1_map = malloc((size_t)ctx.src_w * sizeof(int16_t));
    ctx.y0_map = malloc((size_t)ctx.src_h * sizeof(int16_t));
    ctx.y1_map = malloc((size_t)ctx.src_h * sizeof(int16_t));
    if (!ctx.x0_map || !ctx.x1_map || !ctx.y0_map || !ctx.y1_map) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    build_axis_map(ctx.src_w, ctx.dst_w, ctx.off_x, LCD_H_RES, ctx.x0_map, ctx.x1_map);
    build_axis_map(ctx.src_h, ctx.dst_h, ctx.off_y, LCD_V_RES, ctx.y0_map, ctx.y1_map);

    workbuf = malloc_prefer_spiram(PHOTO_JPEG_WORKBUF_SIZE);
    if (!workbuf) {
        ESP_LOGE(TAG_JPEG, "No memory for JPEG work buffer: %u bytes", (unsigned)PHOTO_JPEG_WORKBUF_SIZE);
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    rewind(f);
    JRESULT jres = jd_prepare(&jd, jpeg_file_in_cb, workbuf, PHOTO_JPEG_WORKBUF_SIZE, &ctx);
    if (jres == JDR_FMT3 || header->progressive) {
        ESP_LOGW(TAG_JPEG, "Progressive JPEG is not supported");
        ret = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }
    if (jres != JDR_OK) {
        ESP_LOGE(TAG_JPEG, "Failed to prepare JPEG stream: %d", jres);
        ret = ESP_FAIL;
        goto cleanup;
    }

    ESP_LOGI(TAG_JPEG, "Decode JPEG: src=%ux%u scale=1/%u -> %ux%u (stream)",
             header->width, header->height, decode_div, ctx.src_w, ctx.src_h);

    jres = jd_decomp(&jd, jpeg_fb_out_cb, decode_scale);
    if (jres == JDR_FMT3 || header->progressive) {
        ESP_LOGW(TAG_JPEG, "Progressive JPEG is not supported");
        ret = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }
    if (jres != JDR_OK) {
        ESP_LOGE(TAG_JPEG, "Failed to decode JPEG stream: %d", jres);
        ret = (jres == JDR_MEM1 || jres == JDR_MEM2) ? ESP_ERR_NO_MEM : ESP_FAIL;
        goto cleanup;
    }

    if (info) {
        info->src_w = header->width;
        info->src_h = header->height;
        info->dst_w = ctx.dst_w;
        info->dst_h = ctx.dst_h;
        info->offset_x = ctx.off_x;
        info->offset_y = ctx.off_y;
    }

    ret = ESP_OK;

cleanup:
    free(workbuf);
    free(ctx.x0_map);
    free(ctx.x1_map);
    free(ctx.y0_map);
    free(ctx.y1_map);
    return ret;
}

esp_err_t jpeg_decode_to_fb(const char *path, uint16_t *fb,
                            scale_mode_t mode, uint16_t bg_color,
                            decode_info_t *info)
{
    if (!path || !fb) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG_JPEG, "Failed to open file: %s", path);
        return ESP_FAIL;
    }

    jpeg_header_info_t header = { 0 };
    esp_err_t ret = inspect_jpeg_file(f, &header);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG_JPEG, "Progressive JPEG is not supported");
        fclose(f);
        return ret;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_JPEG, "Failed to parse JPEG header: %s", path);
        fclose(f);
        return ret;
    }

    ret = decode_jpeg_stream_to_fb(f, &header, fb, mode, bg_color, info);
    fclose(f);
    return ret;
}

esp_err_t jpeg_get_size(const char *path, uint16_t *width, uint16_t *height)
{
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG_JPEG, "Failed to open file: %s", path);
        return ESP_FAIL;
    }

    jpeg_header_info_t header = { 0 };
    esp_err_t ret = inspect_jpeg_file(f, &header);
    fclose(f);
    if (ret != ESP_OK) {
        return ret;
    }

    if (width) {
        *width = header.width;
    }
    if (height) {
        *height = header.height;
    }
    return ESP_OK;
}
