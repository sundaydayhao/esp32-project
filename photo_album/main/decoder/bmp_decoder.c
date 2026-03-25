/**
 * @file bmp_decoder.c
 * @brief BMP 解码器实现（支持 24-bit / 32-bit 不压缩 BMP）
 */
#include "bmp_decoder.h"
#include "config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* BMP 文件头 */
typedef struct __attribute__((packed)) {
    uint16_t type;          /* 0x4D42 = "BM" */
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;        /* 像素数据偏移 */
} bmp_file_header_t;

/* BMP 信息头 (BITMAPINFOHEADER) */
typedef struct __attribute__((packed)) {
    uint32_t header_size;
    int32_t  width;
    int32_t  height;        /* 正数=底部到顶部，负数=顶部到底部 */
    uint16_t planes;
    uint16_t bit_count;     /* 24 或 32 */
    uint32_t compression;   /* 0=不压缩 */
    uint32_t image_size;
    int32_t  x_pels;
    int32_t  y_pels;
    uint32_t clr_used;
    uint32_t clr_important;
} bmp_info_header_t;

static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void calc_scale(int src_w, int src_h, int dst_w, int dst_h,
                       scale_mode_t mode,
                       int *out_w, int *out_h, int *off_x, int *off_y)
{
    float sx = (float)dst_w / src_w;
    float sy = (float)dst_h / src_h;
    float s;
    switch (mode) {
    case SCALE_FIT:    s = sx < sy ? sx : sy; break;
    case SCALE_FILL:   s = sx > sy ? sx : sy; break;
    default:
        *out_w = dst_w; *out_h = dst_h; *off_x = 0; *off_y = 0; return;
    }
    *out_w = (int)(src_w * s);
    *out_h = (int)(src_h * s);
    *off_x = (dst_w - *out_w) / 2;
    *off_y = (dst_h - *out_h) / 2;
}

esp_err_t bmp_decode_to_fb(const char *path, uint16_t *fb,
                            scale_mode_t mode, uint16_t bg_color)
{
    if (!path || !fb) return ESP_ERR_INVALID_ARG;

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG_BMP, "无法打开文件: %s", path);
        return ESP_FAIL;
    }

    bmp_file_header_t fhdr;
    bmp_info_header_t ihdr;

    if (fread(&fhdr, sizeof(fhdr), 1, f) != 1 || fhdr.type != 0x4D42) {
        ESP_LOGE(TAG_BMP, "不是有效的 BMP 文件");
        fclose(f);
        return ESP_FAIL;
    }

    if (fread(&ihdr, sizeof(ihdr), 1, f) != 1) {
        fclose(f);
        return ESP_FAIL;
    }

    if (ihdr.compression != 0 || (ihdr.bit_count != 24 && ihdr.bit_count != 32)) {
        ESP_LOGE(TAG_BMP, "不支持的 BMP 格式 (压缩=%lu, 位深=%u)",
                 (unsigned long)ihdr.compression, ihdr.bit_count);
        fclose(f);
        return ESP_FAIL;
    }

    int src_w   = ihdr.width;
    int src_h   = abs(ihdr.height);
    int flip_y  = (ihdr.height > 0);   /* 正高度 = 底部到顶部存储 */
    int bpp     = ihdr.bit_count / 8;
    int row_bytes = ((src_w * bpp + 3) / 4) * 4;  /* 4 字节对齐 */

    ESP_LOGD(TAG_BMP, "BMP: %dx%d %dbit flip=%d", src_w, src_h, ihdr.bit_count, flip_y);

    /* 分配行缓冲区 */
    uint8_t *row_buf = malloc(row_bytes);
    if (!row_buf) { fclose(f); return ESP_ERR_NO_MEM; }

    /* 计算缩放参数 */
    int dst_w, dst_h, off_x, off_y;
    calc_scale(src_w, src_h, LCD_H_RES, LCD_V_RES, mode,
               &dst_w, &dst_h, &off_x, &off_y);

    /* 填充背景 */
    if (mode == SCALE_FIT) {
        uint32_t total = LCD_H_RES * LCD_V_RES;
        for (uint32_t i = 0; i < total; i++) fb[i] = bg_color;
    }

    float x_ratio = (float)(src_w - 1) / (dst_w > 1 ? dst_w - 1 : 1);
    float y_ratio = (float)(src_h - 1) / (dst_h > 1 ? dst_h - 1 : 1);

    /* 逐行解码并缩放 */
    for (int dy = 0; dy < dst_h; dy++) {
        int fb_y = off_y + dy;
        if (fb_y < 0 || fb_y >= LCD_V_RES) continue;

        float sy_f = dy * y_ratio;
        int   sy   = (int)sy_f;
        if (sy >= src_h) sy = src_h - 1;

        /* 定位到对应行 */
        int file_row = flip_y ? (src_h - 1 - sy) : sy;
        fseek(f, fhdr.offset + (long)file_row * row_bytes, SEEK_SET);
        if (fread(row_buf, 1, row_bytes, f) != (size_t)row_bytes) break;

        for (int dx = 0; dx < dst_w; dx++) {
            int fb_x = off_x + dx;
            if (fb_x < 0 || fb_x >= LCD_H_RES) continue;

            int sx = (int)(dx * x_ratio);
            if (sx >= src_w) sx = src_w - 1;

            const uint8_t *px = row_buf + sx * bpp;
            /* BMP 存储顺序为 BGR */
            uint8_t b = px[0], g = px[1], r = px[2];
            fb[fb_y * LCD_H_RES + fb_x] = rgb888_to_rgb565(r, g, b);
        }
    }

    free(row_buf);
    fclose(f);
    ESP_LOGD(TAG_BMP, "BMP 解码完成: %dx%d → %dx%d", src_w, src_h, dst_w, dst_h);
    return ESP_OK;
}