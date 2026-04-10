/**
 * @file ui_stream_binfont.c
 * @brief Preload an LVGL binfont from SPIFFS, decode all glyphs to A8, and index them in memory
 */
#include "ui_stream_binfont.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "draw/lv_draw_buf.h"
#include "font/lv_font_fmt_txt.h"
#include "misc/lv_math.h"

typedef struct {
    uint32_t version;
    uint16_t tables_count;
    uint16_t font_size;
    uint16_t ascent;
    int16_t descent;
    uint16_t typo_ascent;
    int16_t typo_descent;
    uint16_t typo_line_gap;
    int16_t min_y;
    int16_t max_y;
    uint16_t default_advance_width;
    uint16_t kerning_scale;
    uint8_t index_to_loc_format;
    uint8_t glyph_id_format;
    uint8_t advance_width_format;
    uint8_t bits_per_pixel;
    uint8_t xy_bits;
    uint8_t wh_bits;
    uint8_t advance_width_bits;
    uint8_t compression_id;
    uint8_t subpixels_mode;
    uint8_t padding;
    int16_t underline_position;
    uint16_t underline_thickness;
} font_header_bin_t;

typedef struct {
    uint32_t data_offset;
    uint32_t range_start;
    uint16_t range_length;
    uint16_t glyph_id_start;
    uint16_t data_entries_count;
    uint8_t format_type;
    uint8_t padding;
} cmap_table_bin_t;

typedef struct {
    uint32_t glyph_file_offset;
    uint32_t decoded_buf_offset;
    uint16_t adv_w;
    uint16_t glyph_blob_size;
    uint8_t box_w;
    uint8_t box_h;
    int8_t ofs_x;
    int8_t ofs_y;
} preloaded_binfont_glyph_t;

typedef struct {
    uint8_t *data;
    size_t size;
} font_blob_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    uint32_t byte_index;
    int8_t bit_pos;
    uint8_t byte_value;
} bit_iterator_t;

#if LV_USE_FONT_COMPRESSED
typedef enum {
    RLE_STATE_SINGLE = 0,
    RLE_STATE_REPEATED,
    RLE_STATE_COUNTER,
} ui_font_rle_state_t;

typedef struct {
    uint32_t rdp;
    const uint8_t *in;
    uint8_t bpp;
    uint8_t prev_v;
    uint8_t count;
    ui_font_rle_state_t state;
} ui_font_rle_t;
#endif

typedef struct {
    lv_font_fmt_txt_dsc_t fmt;
    preloaded_binfont_glyph_t *glyphs;
    lv_draw_buf_t *glyph_draw_bufs;
    uint8_t *decoded_bitmap_storage;
    uint8_t *decoded_bitmap_blob;
    size_t decoded_bitmap_size;
    uint32_t glyph_count;
    uint16_t glyph_header_bits;
    uint8_t glyph_header_bytes;
} stream_binfont_dsc_t;

static bool stream_binfont_get_glyph_dsc_cb(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
                                            uint32_t unicode_letter, uint32_t unicode_letter_next);
static const void *stream_binfont_get_glyph_bitmap_cb(lv_font_glyph_dsc_t *g_dsc, lv_draw_buf_t *draw_buf);
static void stream_binfont_release_glyph_cb(const lv_font_t *font, lv_font_glyph_dsc_t *g_dsc);

static void *font_mem_alloc(size_t size);
static void *font_mem_calloc(size_t count, size_t size);
static const char *resolve_font_path(const char *path);
static bool load_font_blob(const char *path, font_blob_t *blob);
static const uint8_t *blob_range_ptr(const font_blob_t *blob, uint32_t offset, size_t len);
static int32_t read_label(const font_blob_t *blob, uint32_t start, const char *label);

static bit_iterator_t init_bit_iterator(const font_blob_t *blob, uint32_t offset);
static unsigned int read_bits(bit_iterator_t *it, int n_bits, bool *ok);
static int read_bits_signed(bit_iterator_t *it, int n_bits, bool *ok);

static uint32_t glyph_header_bytes(const font_header_bin_t *header);
static uint32_t glyph_a8_stride(uint32_t width);
static bool load_cmaps(stream_binfont_dsc_t *dsc, const font_blob_t *blob, uint32_t cmaps_start);
static bool load_glyphs(stream_binfont_dsc_t *dsc, const font_blob_t *blob, uint32_t loca_start,
                        uint32_t glyph_start, const font_header_bin_t *header);
static bool init_predecoded_glyph_buffers(stream_binfont_dsc_t *dsc);
static bool decode_all_glyphs(stream_binfont_dsc_t *dsc, const font_blob_t *blob);
static bool extract_glyph_blob(const stream_binfont_dsc_t *dsc, const font_blob_t *blob,
                               const preloaded_binfont_glyph_t *glyph, uint8_t *buf);
static bool decode_glyph_to_a8(const stream_binfont_dsc_t *dsc, const uint8_t *glyph_blob,
                               const preloaded_binfont_glyph_t *glyph, lv_draw_buf_t *draw_buf);

static uint32_t get_glyph_id(const stream_binfont_dsc_t *dsc, uint32_t letter);
static const preloaded_binfont_glyph_t *get_glyph(const stream_binfont_dsc_t *dsc, uint32_t glyph_id);
static int unicode_list_search(const uint16_t *list, uint16_t list_length, uint16_t key);
static void free_cmaps(lv_font_fmt_txt_dsc_t *fmt);

static uint8_t get_bits_from_bytes(const uint8_t *in, uint32_t bit_pos, uint8_t len);
static bool decode_plain_bitmap_to_a8(const uint8_t *in, uint8_t *out, uint32_t stride,
                                      uint32_t w, uint32_t h, uint8_t bpp);
#if LV_USE_FONT_COMPRESSED
static void rle_init(ui_font_rle_t *rle, const uint8_t *in, uint8_t bpp);
static uint8_t rle_next(ui_font_rle_t *rle);
static bool decode_compressed_bitmap_to_a8(const uint8_t *in, uint8_t *out, uint32_t stride,
                                           uint32_t w, uint32_t h, uint8_t bpp, bool prefilter);
#endif

static const uint8_t opa2_table[4] = {0, 85, 170, 255};
static const uint8_t opa4_table[16] = {
    0,   17,  34,  51,
    68,  85,  102, 119,
    136, 153, 170, 187,
    204, 221, 238, 255
};
#if LV_USE_FONT_COMPRESSED
static const uint8_t opa3_table[8] = {0, 36, 73, 109, 146, 182, 218, 255};
#endif

lv_font_t *ui_stream_binfont_create(const char *path)
{
    if(path == NULL) {
        return NULL;
    }

    lv_font_t *font = calloc(1, sizeof(lv_font_t));
    stream_binfont_dsc_t *dsc = calloc(1, sizeof(stream_binfont_dsc_t));
    font_blob_t blob = {0};

    if(font == NULL || dsc == NULL) {
        free(font);
        free(dsc);
        return NULL;
    }

    font->dsc = dsc;

    if(!load_font_blob(path, &blob)) {
        ui_stream_binfont_destroy(font);
        return NULL;
    }

    int32_t header_length = read_label(&blob, 0, "head");
    const uint8_t *header_ptr = blob_range_ptr(&blob, 8, sizeof(font_header_bin_t));
    if(header_length < 0 || header_ptr == NULL) {
        ESP_LOGW(TAG_UI, "Font header is invalid");
        goto fail;
    }

    font_header_bin_t header;
    memcpy(&header, header_ptr, sizeof(header));

    font->get_glyph_dsc = stream_binfont_get_glyph_dsc_cb;
    font->get_glyph_bitmap = stream_binfont_get_glyph_bitmap_cb;
    font->release_glyph = stream_binfont_release_glyph_cb;
    font->line_height = header.ascent - header.descent;
    font->base_line = -header.descent;
    font->subpx = header.subpixels_mode;
    font->kerning = LV_FONT_KERNING_NONE;
    font->underline_position = (int8_t)header.underline_position;
    font->underline_thickness = (int8_t)header.underline_thickness;

    dsc->fmt.bpp = header.bits_per_pixel;
    dsc->fmt.bitmap_format = header.compression_id;
    dsc->fmt.kern_scale = 0;
    dsc->fmt.kern_classes = 0;
    dsc->fmt.kern_dsc = NULL;

    uint32_t cmaps_start = (uint32_t)header_length;
    if(!load_cmaps(dsc, &blob, cmaps_start)) {
        ESP_LOGW(TAG_UI, "Load font cmaps failed");
        goto fail;
    }

    int32_t cmaps_length = read_label(&blob, cmaps_start, "cmap");
    if(cmaps_length < 0) {
        ESP_LOGW(TAG_UI, "Font cmap section is invalid");
        goto fail;
    }

    uint32_t loca_start = cmaps_start + (uint32_t)cmaps_length;
    int32_t loca_length = read_label(&blob, loca_start, "loca");
    if(loca_length < 0) {
        ESP_LOGW(TAG_UI, "Font loca section is invalid");
        goto fail;
    }

    uint32_t glyph_start = loca_start + (uint32_t)loca_length;
    if(!load_glyphs(dsc, &blob, loca_start, glyph_start, &header)) {
        ESP_LOGW(TAG_UI, "Load font glyph metadata failed");
        goto fail;
    }

    if(!init_predecoded_glyph_buffers(dsc)) {
        ESP_LOGW(TAG_UI, "Allocate predecoded glyph buffers failed");
        goto fail;
    }

    if(!decode_all_glyphs(dsc, &blob)) {
        ESP_LOGW(TAG_UI, "Decode font glyphs failed");
        goto fail;
    }

    size_t source_size = blob.size;
    free(blob.data);
    blob.data = NULL;
    blob.size = 0;

    ESP_LOGI(TAG_UI, "Predecoded font ready: glyphs=%u, source=%u bytes, decoded=%u bytes",
             (unsigned)dsc->glyph_count, (unsigned)source_size, (unsigned)dsc->decoded_bitmap_size);
    return font;

fail:
    free(blob.data);
    ui_stream_binfont_destroy(font);
    return NULL;
}

void ui_stream_binfont_destroy(lv_font_t *font)
{
    if(font == NULL) {
        return;
    }

    stream_binfont_dsc_t *dsc = (stream_binfont_dsc_t *)font->dsc;
    if(dsc != NULL) {
        free_cmaps(&dsc->fmt);
        free(dsc->decoded_bitmap_storage);
        free(dsc->glyph_draw_bufs);
        free(dsc->glyphs);
        free(dsc);
    }

    free(font);
}

static bool stream_binfont_get_glyph_dsc_cb(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
                                            uint32_t unicode_letter, uint32_t unicode_letter_next)
{
    LV_UNUSED(unicode_letter_next);

    bool is_tab = unicode_letter == '\t';
    if(is_tab) {
        unicode_letter = ' ';
    }

    stream_binfont_dsc_t *dsc = (stream_binfont_dsc_t *)font->dsc;
    uint32_t glyph_id = get_glyph_id(dsc, unicode_letter);
    const preloaded_binfont_glyph_t *glyph = get_glyph(dsc, glyph_id);
    if(glyph == NULL) {
        return false;
    }

    uint32_t adv_w = glyph->adv_w;
    adv_w = (adv_w + (1U << 3)) >> 4;

    dsc_out->adv_w = adv_w;
    dsc_out->box_w = glyph->box_w;
    dsc_out->box_h = glyph->box_h;
    dsc_out->ofs_x = glyph->ofs_x;
    dsc_out->ofs_y = glyph->ofs_y;
    dsc_out->format = LV_FONT_GLYPH_FORMAT_A8;
    dsc_out->is_placeholder = false;
    dsc_out->gid.index = glyph_id;
    dsc_out->entry = NULL;

    if(is_tab) {
        dsc_out->adv_w *= 2;
        dsc_out->box_w *= 2;
    }

    return true;
}

static const void *stream_binfont_get_glyph_bitmap_cb(lv_font_glyph_dsc_t *g_dsc, lv_draw_buf_t *draw_buf)
{
    LV_UNUSED(draw_buf);

    const lv_font_t *font = g_dsc->resolved_font;
    if(font == NULL) {
        return NULL;
    }

    stream_binfont_dsc_t *dsc = (stream_binfont_dsc_t *)font->dsc;
    const preloaded_binfont_glyph_t *glyph = get_glyph(dsc, g_dsc->gid.index);
    if(glyph == NULL || glyph->box_w == 0 || glyph->box_h == 0 || glyph->glyph_blob_size == 0) {
        return NULL;
    }

    g_dsc->entry = NULL;
    return &dsc->glyph_draw_bufs[g_dsc->gid.index];
}

static void stream_binfont_release_glyph_cb(const lv_font_t *font, lv_font_glyph_dsc_t *g_dsc)
{
    LV_UNUSED(font);

    if(g_dsc != NULL) {
        g_dsc->entry = NULL;
    }
}

static void *font_mem_alloc(size_t size)
{
    if(size == 0) {
        return NULL;
    }

    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(ptr == NULL) {
        ptr = malloc(size);
    }

    return ptr;
}

static void *font_mem_calloc(size_t count, size_t size)
{
    if(count == 0 || size == 0 || count > SIZE_MAX / size) {
        return NULL;
    }

    void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if(ptr == NULL) {
        ptr = calloc(count, size);
    }

    return ptr;
}

static const char *resolve_font_path(const char *path)
{
    if(path != NULL && path[0] != '\0' && path[1] == ':') {
        return path + 2;
    }

    return path;
}

static bool load_font_blob(const char *path, font_blob_t *blob)
{
    if(path == NULL || blob == NULL) {
        return false;
    }

    const char *file_path = resolve_font_path(path);
    FILE *fp = fopen(file_path, "rb");
    if(fp == NULL) {
        ESP_LOGW(TAG_UI, "Open font file failed: %s (%d)", file_path, errno);
        return false;
    }

    if(fseek(fp, 0, SEEK_END) != 0) {
        ESP_LOGW(TAG_UI, "Seek font file end failed: %s", file_path);
        fclose(fp);
        return false;
    }

    long file_size = ftell(fp);
    if(file_size <= 0) {
        ESP_LOGW(TAG_UI, "Font file size invalid: %s (%ld)", file_path, file_size);
        fclose(fp);
        return false;
    }

    if(fseek(fp, 0, SEEK_SET) != 0) {
        ESP_LOGW(TAG_UI, "Seek font file start failed: %s", file_path);
        fclose(fp);
        return false;
    }

    uint8_t *data = font_mem_alloc((size_t)file_size);
    if(data == NULL) {
        ESP_LOGW(TAG_UI, "Allocate font blob failed: %ld bytes", file_size);
        fclose(fp);
        return false;
    }

    size_t read_size = fread(data, 1, (size_t)file_size, fp);
    fclose(fp);

    if(read_size != (size_t)file_size) {
        ESP_LOGW(TAG_UI, "Read font file failed: expect=%ld actual=%u",
                 file_size, (unsigned)read_size);
        free(data);
        return false;
    }

    blob->data = data;
    blob->size = (size_t)file_size;
    return true;
}

static const uint8_t *blob_range_ptr(const font_blob_t *blob, uint32_t offset, size_t len)
{
    if(blob == NULL || blob->data == NULL) {
        return NULL;
    }

    if((size_t)offset > blob->size || len > blob->size - (size_t)offset) {
        return NULL;
    }

    return blob->data + offset;
}

static int32_t read_label(const font_blob_t *blob, uint32_t start, const char *label)
{
    const uint8_t *ptr = blob_range_ptr(blob, start, 8);
    if(ptr == NULL) {
        return -1;
    }

    uint32_t length = 0;
    memcpy(&length, ptr, sizeof(length));
    if(memcmp(ptr + sizeof(length), label, 4) != 0) {
        return -1;
    }

    return (int32_t)length;
}

static bit_iterator_t init_bit_iterator(const font_blob_t *blob, uint32_t offset)
{
    bit_iterator_t it = {
        .data = blob ? blob->data : NULL,
        .size = blob ? blob->size : 0,
        .byte_index = offset,
        .bit_pos = -1,
        .byte_value = 0,
    };

    return it;
}

static unsigned int read_bits(bit_iterator_t *it, int n_bits, bool *ok)
{
    if(ok != NULL) {
        *ok = true;
    }

    if(n_bits <= 0) {
        return 0;
    }

    unsigned int value = 0;
    while(n_bits--) {
        it->byte_value <<= 1;
        it->bit_pos--;

        if(it->bit_pos < 0) {
            it->bit_pos = 7;
            if(it->byte_index >= it->size) {
                if(ok != NULL) {
                    *ok = false;
                }
                return 0;
            }

            it->byte_value = it->data[it->byte_index++];
        }

        value |= ((it->byte_value & 0x80U) ? 1U : 0U) << n_bits;
    }

    return value;
}

static int read_bits_signed(bit_iterator_t *it, int n_bits, bool *ok)
{
    if(n_bits <= 0) {
        if(ok != NULL) {
            *ok = true;
        }
        return 0;
    }

    unsigned int value = read_bits(it, n_bits, ok);
    if(ok != NULL && !(*ok)) {
        return 0;
    }

    if(value & (1U << (n_bits - 1))) {
        value |= ~0U << n_bits;
    }

    return (int)value;
}

static uint32_t glyph_header_bytes(const font_header_bin_t *header)
{
    uint32_t nbits = header->advance_width_bits + 2U * header->xy_bits + 2U * header->wh_bits;
    return (nbits + 7U) / 8U;
}

static uint32_t glyph_a8_stride(uint32_t width)
{
    return LV_DRAW_BUF_STRIDE(width, LV_COLOR_FORMAT_A8);
}

static bool load_cmaps(stream_binfont_dsc_t *dsc, const font_blob_t *blob, uint32_t cmaps_start)
{
    int32_t cmaps_length = read_label(blob, cmaps_start, "cmap");
    if(cmaps_length < 0) {
        return false;
    }

    const uint8_t *count_ptr = blob_range_ptr(blob, cmaps_start + 8, sizeof(uint32_t));
    if(count_ptr == NULL) {
        return false;
    }

    uint32_t cmaps_subtables_count = 0;
    memcpy(&cmaps_subtables_count, count_ptr, sizeof(cmaps_subtables_count));

    lv_font_fmt_txt_cmap_t *cmaps = font_mem_calloc(cmaps_subtables_count, sizeof(lv_font_fmt_txt_cmap_t));
    cmap_table_bin_t *cmap_tables = font_mem_alloc(sizeof(cmap_table_bin_t) * cmaps_subtables_count);
    if(cmaps == NULL || cmap_tables == NULL) {
        free(cmaps);
        free(cmap_tables);
        return false;
    }

    const uint8_t *tables_ptr = blob_range_ptr(blob, cmaps_start + 8 + sizeof(uint32_t),
                                               sizeof(cmap_table_bin_t) * cmaps_subtables_count);
    if(tables_ptr == NULL) {
        free(cmaps);
        free(cmap_tables);
        return false;
    }

    memcpy(cmap_tables, tables_ptr, sizeof(cmap_table_bin_t) * cmaps_subtables_count);

    dsc->fmt.cmaps = cmaps;
    dsc->fmt.cmap_num = (uint16_t)cmaps_subtables_count;

    for(uint16_t i = 0; i < dsc->fmt.cmap_num; ++i) {
        lv_font_fmt_txt_cmap_t *cmap = &((lv_font_fmt_txt_cmap_t *)dsc->fmt.cmaps)[i];
        cmap->range_start = cmap_tables[i].range_start;
        cmap->range_length = cmap_tables[i].range_length;
        cmap->glyph_id_start = cmap_tables[i].glyph_id_start;
        cmap->type = cmap_tables[i].format_type;

        if(blob_range_ptr(blob, cmaps_start + cmap_tables[i].data_offset, 0) == NULL) {
            free(cmap_tables);
            return false;
        }

        switch(cmap_tables[i].format_type) {
            case LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL: {
                size_t ids_size = (size_t)cmap_tables[i].data_entries_count;
                uint8_t *glyph_id_ofs_list = font_mem_alloc(ids_size);
                const uint8_t *ids_ptr = blob_range_ptr(blob, cmaps_start + cmap_tables[i].data_offset, ids_size);
                if(glyph_id_ofs_list == NULL || ids_ptr == NULL) {
                    free(glyph_id_ofs_list);
                    free(cmap_tables);
                    return false;
                }

                memcpy(glyph_id_ofs_list, ids_ptr, ids_size);
                cmap->glyph_id_ofs_list = glyph_id_ofs_list;
                cmap->list_length = cmap->range_length;
                break;
            }
            case LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY:
                break;
            case LV_FONT_FMT_TXT_CMAP_SPARSE_FULL:
            case LV_FONT_FMT_TXT_CMAP_SPARSE_TINY: {
                size_t list_size = sizeof(uint16_t) * cmap_tables[i].data_entries_count;
                uint16_t *unicode_list = font_mem_alloc(list_size);
                const uint8_t *list_ptr = blob_range_ptr(blob, cmaps_start + cmap_tables[i].data_offset, list_size);
                if(unicode_list == NULL || list_ptr == NULL) {
                    free(unicode_list);
                    free(cmap_tables);
                    return false;
                }

                memcpy(unicode_list, list_ptr, list_size);
                cmap->unicode_list = unicode_list;
                cmap->list_length = cmap_tables[i].data_entries_count;

                if(cmap_tables[i].format_type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL) {
                    size_t ofs_size = sizeof(uint16_t) * cmap->list_length;
                    uint16_t *glyph_ofs = font_mem_alloc(ofs_size);
                    const uint8_t *ofs_ptr = blob_range_ptr(blob,
                                                            cmaps_start + cmap_tables[i].data_offset + list_size,
                                                            ofs_size);
                    if(glyph_ofs == NULL || ofs_ptr == NULL) {
                        free(glyph_ofs);
                        free(cmap_tables);
                        return false;
                    }

                    memcpy(glyph_ofs, ofs_ptr, ofs_size);
                    cmap->glyph_id_ofs_list = glyph_ofs;
                }
                break;
            }
            default:
                ESP_LOGW(TAG_UI, "Unsupported cmap type: %u", (unsigned)cmap_tables[i].format_type);
                free(cmap_tables);
                return false;
        }
    }

    free(cmap_tables);
    return true;
}

static bool load_glyphs(stream_binfont_dsc_t *dsc, const font_blob_t *blob, uint32_t loca_start,
                        uint32_t glyph_start, const font_header_bin_t *header)
{
    int32_t loca_length = read_label(blob, loca_start, "loca");
    if(loca_length < 0) {
        return false;
    }

    const uint8_t *count_ptr = blob_range_ptr(blob, loca_start + 8, sizeof(uint32_t));
    if(count_ptr == NULL) {
        return false;
    }

    uint32_t loca_count = 0;
    memcpy(&loca_count, count_ptr, sizeof(loca_count));

    uint32_t *glyph_offsets = font_mem_calloc(loca_count + 1U, sizeof(uint32_t));
    preloaded_binfont_glyph_t *glyphs = font_mem_calloc(loca_count, sizeof(preloaded_binfont_glyph_t));
    if(glyph_offsets == NULL || glyphs == NULL) {
        free(glyph_offsets);
        free(glyphs);
        return false;
    }

    if(blob_range_ptr(blob, loca_start + 8 + sizeof(uint32_t), 0) == NULL) {
        free(glyph_offsets);
        free(glyphs);
        return false;
    }

    if(header->index_to_loc_format == 0) {
        const uint8_t *ptr = blob_range_ptr(blob, loca_start + 8 + sizeof(uint32_t),
                                            loca_count * sizeof(uint16_t));
        if(ptr == NULL) {
            free(glyph_offsets);
            free(glyphs);
            return false;
        }

        for(uint32_t i = 0; i < loca_count; ++i) {
            uint16_t offset = 0;
            memcpy(&offset, ptr + (sizeof(uint16_t) * i), sizeof(offset));
            glyph_offsets[i] = offset;
        }
    }
    else if(header->index_to_loc_format == 1) {
        const uint8_t *ptr = blob_range_ptr(blob, loca_start + 8 + sizeof(uint32_t),
                                            loca_count * sizeof(uint32_t));
        if(ptr == NULL) {
            free(glyph_offsets);
            free(glyphs);
            return false;
        }

        memcpy(glyph_offsets, ptr, loca_count * sizeof(uint32_t));
    }
    else {
        ESP_LOGW(TAG_UI, "Unsupported loca format: %u", (unsigned)header->index_to_loc_format);
        free(glyph_offsets);
        free(glyphs);
        return false;
    }

    int32_t glyph_length = read_label(blob, glyph_start, "glyf");
    if(glyph_length < 0) {
        free(glyph_offsets);
        free(glyphs);
        return false;
    }

    dsc->glyph_header_bits = (uint16_t)(header->advance_width_bits + 2U * header->xy_bits + 2U * header->wh_bits);
    dsc->glyph_header_bytes = (uint8_t)glyph_header_bytes(header);

    size_t decoded_total = 0;
    for(uint32_t i = 1; i < loca_count; ++i) {
        preloaded_binfont_glyph_t *glyph = &glyphs[i];
        uint32_t glyph_offset = glyph_offsets[i];
        uint32_t glyph_file_offset = glyph_start + glyph_offset;
        bit_iterator_t bit_it = init_bit_iterator(blob, glyph_file_offset);
        bool ok = true;

        if(header->advance_width_bits == 0) {
            glyph->adv_w = header->default_advance_width;
        }
        else {
            glyph->adv_w = (uint16_t)read_bits(&bit_it, header->advance_width_bits, &ok);
            if(!ok) {
                free(glyph_offsets);
                free(glyphs);
                return false;
            }
        }

        if(header->advance_width_format == 0) {
            glyph->adv_w = (uint16_t)(glyph->adv_w * 16U);
        }

        glyph->ofs_x = (int8_t)read_bits_signed(&bit_it, header->xy_bits, &ok);
        glyph->ofs_y = (int8_t)read_bits_signed(&bit_it, header->xy_bits, &ok);
        glyph->box_w = (uint8_t)read_bits(&bit_it, header->wh_bits, &ok);
        glyph->box_h = (uint8_t)read_bits(&bit_it, header->wh_bits, &ok);
        if(!ok) {
            free(glyph_offsets);
            free(glyphs);
            return false;
        }

        uint32_t next_offset = (i < loca_count - 1U) ? glyph_offsets[i + 1U] : (uint32_t)glyph_length;
        if(next_offset < glyph_offset + dsc->glyph_header_bytes) {
            free(glyph_offsets);
            free(glyphs);
            return false;
        }

        uint32_t glyph_blob_size = next_offset - glyph_offset - dsc->glyph_header_bytes;
        if((glyph->box_w != 0U || glyph->box_h != 0U) && glyph_blob_size == 0U) {
            free(glyph_offsets);
            free(glyphs);
            return false;
        }
        if(glyph_blob_size > UINT16_MAX) {
            free(glyph_offsets);
            free(glyphs);
            return false;
        }

        glyph->glyph_file_offset = glyph_file_offset;
        glyph->glyph_blob_size = (uint16_t)glyph_blob_size;

        if(glyph->box_w == 0 || glyph->box_h == 0 || glyph->glyph_blob_size == 0) {
            continue;
        }

        uint32_t stride = glyph_a8_stride(glyph->box_w);
        size_t glyph_buf_size = (size_t)stride * glyph->box_h;
        decoded_total = LV_ROUND_UP(decoded_total, LV_DRAW_BUF_ALIGN);
        if(decoded_total > UINT32_MAX || glyph_buf_size > UINT32_MAX - decoded_total) {
            free(glyph_offsets);
            free(glyphs);
            return false;
        }

        glyph->decoded_buf_offset = (uint32_t)decoded_total;
        decoded_total += glyph_buf_size;
    }

    dsc->glyphs = glyphs;
    dsc->glyph_count = loca_count;
    dsc->decoded_bitmap_size = decoded_total;

    free(glyph_offsets);
    return true;
}

static bool init_predecoded_glyph_buffers(stream_binfont_dsc_t *dsc)
{
    if(dsc == NULL || dsc->glyph_count == 0) {
        return false;
    }

    dsc->glyph_draw_bufs = font_mem_calloc(dsc->glyph_count, sizeof(lv_draw_buf_t));
    if(dsc->glyph_draw_bufs == NULL) {
        return false;
    }

    if(dsc->decoded_bitmap_size == 0) {
        return true;
    }

    dsc->decoded_bitmap_storage = font_mem_alloc(dsc->decoded_bitmap_size + LV_DRAW_BUF_ALIGN);
    if(dsc->decoded_bitmap_storage == NULL) {
        return false;
    }

    uintptr_t aligned = (uintptr_t)dsc->decoded_bitmap_storage;
    aligned = LV_ROUND_UP(aligned, LV_DRAW_BUF_ALIGN);
    dsc->decoded_bitmap_blob = (uint8_t *)aligned;
    memset(dsc->decoded_bitmap_blob, 0, dsc->decoded_bitmap_size);

    for(uint32_t i = 1; i < dsc->glyph_count; ++i) {
        const preloaded_binfont_glyph_t *glyph = &dsc->glyphs[i];
        if(glyph->box_w == 0 || glyph->box_h == 0 || glyph->glyph_blob_size == 0) {
            continue;
        }

        lv_draw_buf_t *draw_buf = &dsc->glyph_draw_bufs[i];
        uint32_t stride = glyph_a8_stride(glyph->box_w);
        uint32_t data_size = stride * glyph->box_h;
        uint8_t *data = dsc->decoded_bitmap_blob + glyph->decoded_buf_offset;

        memset(draw_buf, 0, sizeof(*draw_buf));
        draw_buf->header.magic = LV_IMAGE_HEADER_MAGIC;
        draw_buf->header.cf = LV_COLOR_FORMAT_A8;
        draw_buf->header.flags = 0;
        draw_buf->header.w = glyph->box_w;
        draw_buf->header.h = glyph->box_h;
        draw_buf->header.stride = stride;
        draw_buf->data_size = data_size;
        draw_buf->data = data;
        draw_buf->unaligned_data = data;
        draw_buf->handlers = NULL;
    }

    return true;
}

static bool decode_all_glyphs(stream_binfont_dsc_t *dsc, const font_blob_t *blob)
{
    if(dsc == NULL || blob == NULL) {
        return false;
    }

    size_t max_blob_size = 0;
    for(uint32_t i = 1; i < dsc->glyph_count; ++i) {
        if(dsc->glyphs[i].glyph_blob_size > max_blob_size) {
            max_blob_size = dsc->glyphs[i].glyph_blob_size;
        }
    }

    uint8_t *glyph_blob = NULL;
    if(max_blob_size > 0) {
        glyph_blob = malloc(max_blob_size);
        if(glyph_blob == NULL) {
            return false;
        }
    }

    bool ok = true;
    for(uint32_t i = 1; i < dsc->glyph_count; ++i) {
        const preloaded_binfont_glyph_t *glyph = &dsc->glyphs[i];
        lv_draw_buf_t *draw_buf = &dsc->glyph_draw_bufs[i];

        if(glyph->box_w == 0 || glyph->box_h == 0 || glyph->glyph_blob_size == 0) {
            continue;
        }

        if(!extract_glyph_blob(dsc, blob, glyph, glyph_blob) ||
           !decode_glyph_to_a8(dsc, glyph_blob, glyph, draw_buf)) {
            ok = false;
            break;
        }
    }

    free(glyph_blob);
    return ok;
}

static bool extract_glyph_blob(const stream_binfont_dsc_t *dsc, const font_blob_t *blob,
                               const preloaded_binfont_glyph_t *glyph, uint8_t *buf)
{
    if(glyph->glyph_blob_size == 0) {
        return true;
    }

    if((dsc->glyph_header_bits % 8U) == 0U) {
        const uint8_t *src = blob_range_ptr(blob,
                                            glyph->glyph_file_offset + dsc->glyph_header_bytes,
                                            glyph->glyph_blob_size);
        if(src == NULL) {
            return false;
        }

        memcpy(buf, src, glyph->glyph_blob_size);
        return true;
    }

    bit_iterator_t bit_it = init_bit_iterator(blob, glyph->glyph_file_offset);
    bool ok = true;
    read_bits(&bit_it, dsc->glyph_header_bits, &ok);
    if(!ok) {
        return false;
    }

    for(uint16_t i = 0; i + 1U < glyph->glyph_blob_size; ++i) {
        buf[i] = (uint8_t)read_bits(&bit_it, 8, &ok);
        if(!ok) {
            return false;
        }
    }

    buf[glyph->glyph_blob_size - 1U] = (uint8_t)read_bits(&bit_it, 8 - (dsc->glyph_header_bits % 8U), &ok);
    if(!ok) {
        return false;
    }

    buf[glyph->glyph_blob_size - 1U] <<= dsc->glyph_header_bits % 8U;
    return true;
}

static bool decode_glyph_to_a8(const stream_binfont_dsc_t *dsc, const uint8_t *glyph_blob,
                               const preloaded_binfont_glyph_t *glyph, lv_draw_buf_t *draw_buf)
{
    if(dsc == NULL || glyph == NULL || draw_buf == NULL || draw_buf->data == NULL) {
        return false;
    }

    memset(draw_buf->data, 0, draw_buf->data_size);

    if(dsc->fmt.bitmap_format == LV_FONT_FMT_TXT_PLAIN) {
        return decode_plain_bitmap_to_a8(glyph_blob, draw_buf->data, draw_buf->header.stride,
                                         glyph->box_w, glyph->box_h, dsc->fmt.bpp);
    }

#if LV_USE_FONT_COMPRESSED
    if(dsc->fmt.bitmap_format == LV_FONT_FMT_TXT_COMPRESSED) {
        return decode_compressed_bitmap_to_a8(glyph_blob, draw_buf->data, draw_buf->header.stride,
                                              glyph->box_w, glyph->box_h, dsc->fmt.bpp, true);
    }
#endif

    ESP_LOGW(TAG_UI, "Unsupported font bitmap format: %u", (unsigned)dsc->fmt.bitmap_format);
    return false;
}

static uint32_t get_glyph_id(const stream_binfont_dsc_t *dsc, uint32_t letter)
{
    if(letter == '\0') {
        return 0;
    }

    const lv_font_fmt_txt_cmap_t *cmaps = dsc->fmt.cmaps;
    for(uint16_t i = 0; i < dsc->fmt.cmap_num; ++i) {
        if(letter < cmaps[i].range_start) {
            continue;
        }

        uint32_t rcp = letter - cmaps[i].range_start;
        if(rcp >= cmaps[i].range_length) {
            continue;
        }

        switch(cmaps[i].type) {
            case LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY:
                return cmaps[i].glyph_id_start + rcp;
            case LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL: {
                const uint8_t *gid_ofs_8 = (const uint8_t *)cmaps[i].glyph_id_ofs_list;
                return cmaps[i].glyph_id_start + gid_ofs_8[rcp];
            }
            case LV_FONT_FMT_TXT_CMAP_SPARSE_TINY: {
                int ofs = unicode_list_search(cmaps[i].unicode_list, cmaps[i].list_length, (uint16_t)rcp);
                if(ofs >= 0) {
                    return cmaps[i].glyph_id_start + (uint32_t)ofs;
                }
                break;
            }
            case LV_FONT_FMT_TXT_CMAP_SPARSE_FULL: {
                int ofs = unicode_list_search(cmaps[i].unicode_list, cmaps[i].list_length, (uint16_t)rcp);
                if(ofs >= 0) {
                    const uint16_t *gid_ofs_16 = (const uint16_t *)cmaps[i].glyph_id_ofs_list;
                    return cmaps[i].glyph_id_start + gid_ofs_16[ofs];
                }
                break;
            }
            default:
                break;
        }
    }

    return 0;
}

static const preloaded_binfont_glyph_t *get_glyph(const stream_binfont_dsc_t *dsc, uint32_t glyph_id)
{
    if(glyph_id == 0 || glyph_id >= dsc->glyph_count || dsc->glyphs == NULL) {
        return NULL;
    }

    return &dsc->glyphs[glyph_id];
}

static int unicode_list_search(const uint16_t *list, uint16_t list_length, uint16_t key)
{
    int left = 0;
    int right = (int)list_length - 1;
    while(left <= right) {
        int mid = left + (right - left) / 2;
        uint16_t value = list[mid];
        if(value == key) {
            return mid;
        }
        if(value < key) {
            left = mid + 1;
        }
        else {
            right = mid - 1;
        }
    }

    return -1;
}

static void free_cmaps(lv_font_fmt_txt_dsc_t *fmt)
{
    if(fmt == NULL || fmt->cmaps == NULL) {
        return;
    }

    lv_font_fmt_txt_cmap_t *cmaps = (lv_font_fmt_txt_cmap_t *)fmt->cmaps;
    for(uint16_t i = 0; i < fmt->cmap_num; ++i) {
        free((void *)cmaps[i].glyph_id_ofs_list);
        free((void *)cmaps[i].unicode_list);
    }

    free(cmaps);
    fmt->cmaps = NULL;
    fmt->cmap_num = 0;
}

static uint8_t get_bits_from_bytes(const uint8_t *in, uint32_t bit_pos, uint8_t len)
{
    uint8_t bit_mask;
    switch(len) {
        case 1:
            bit_mask = 0x1;
            break;
        case 2:
            bit_mask = 0x3;
            break;
        case 3:
            bit_mask = 0x7;
            break;
        case 4:
            bit_mask = 0xF;
            break;
        case 8:
            bit_mask = 0xFF;
            break;
        default:
            bit_mask = (uint8_t)((1U << len) - 1U);
            break;
    }

    uint32_t byte_pos = bit_pos >> 3;
    bit_pos &= 0x7U;

    if(bit_pos + len >= 8U) {
        uint16_t in16 = (uint16_t)((in[byte_pos] << 8) + in[byte_pos + 1U]);
        return (uint8_t)((in16 >> (16U - bit_pos - len)) & bit_mask);
    }

    return (uint8_t)((in[byte_pos] >> (8U - bit_pos - len)) & bit_mask);
}

static bool decode_plain_bitmap_to_a8(const uint8_t *in, uint8_t *out, uint32_t stride,
                                      uint32_t w, uint32_t h, uint8_t bpp)
{
    uint32_t bit_pos = 0;
    for(uint32_t y = 0; y < h; ++y) {
        uint8_t *row = out + (stride * y);
        for(uint32_t x = 0; x < w; ++x) {
            uint8_t value = get_bits_from_bytes(in, bit_pos, bpp);
            bit_pos += bpp;

            switch(bpp) {
                case 1:
                    row[x] = value ? 0xFF : 0x00;
                    break;
                case 2:
                    row[x] = opa2_table[value];
                    break;
#if LV_USE_FONT_COMPRESSED
                case 3:
                    row[x] = opa3_table[value];
                    break;
#endif
                case 4:
                    row[x] = opa4_table[value];
                    break;
                case 8:
                    row[x] = value;
                    break;
                default:
                    ESP_LOGW(TAG_UI, "Unsupported plain font bpp: %u", (unsigned)bpp);
                    return false;
            }
        }
    }

    return true;
}

#if LV_USE_FONT_COMPRESSED
static void rle_init(ui_font_rle_t *rle, const uint8_t *in, uint8_t bpp)
{
    rle->in = in;
    rle->bpp = bpp;
    rle->state = RLE_STATE_SINGLE;
    rle->rdp = 0;
    rle->prev_v = 0;
    rle->count = 0;
}

static uint8_t rle_next(ui_font_rle_t *rle)
{
    uint8_t v = 0;
    uint8_t ret = 0;

    if(rle->state == RLE_STATE_SINGLE) {
        ret = get_bits_from_bytes(rle->in, rle->rdp, rle->bpp);
        if(rle->rdp != 0 && rle->prev_v == ret) {
            rle->count = 0;
            rle->state = RLE_STATE_REPEATED;
        }

        rle->prev_v = ret;
        rle->rdp += rle->bpp;
    }
    else if(rle->state == RLE_STATE_REPEATED) {
        v = get_bits_from_bytes(rle->in, rle->rdp, 1);
        rle->count++;
        rle->rdp += 1;
        if(v == 1) {
            ret = rle->prev_v;
            if(rle->count == 11) {
                rle->count = get_bits_from_bytes(rle->in, rle->rdp, 6);
                rle->rdp += 6;
                if(rle->count != 0) {
                    rle->state = RLE_STATE_COUNTER;
                }
                else {
                    ret = get_bits_from_bytes(rle->in, rle->rdp, rle->bpp);
                    rle->prev_v = ret;
                    rle->rdp += rle->bpp;
                    rle->state = RLE_STATE_SINGLE;
                }
            }
        }
        else {
            ret = get_bits_from_bytes(rle->in, rle->rdp, rle->bpp);
            rle->prev_v = ret;
            rle->rdp += rle->bpp;
            rle->state = RLE_STATE_SINGLE;
        }
    }
    else if(rle->state == RLE_STATE_COUNTER) {
        ret = rle->prev_v;
        rle->count--;
        if(rle->count == 0) {
            ret = get_bits_from_bytes(rle->in, rle->rdp, rle->bpp);
            rle->prev_v = ret;
            rle->rdp += rle->bpp;
            rle->state = RLE_STATE_SINGLE;
        }
    }

    return ret;
}

static bool decode_compressed_bitmap_to_a8(const uint8_t *in, uint8_t *out, uint32_t stride,
                                           uint32_t w, uint32_t h, uint8_t bpp, bool prefilter)
{
    const uint8_t *opa_table = NULL;
    switch(bpp) {
        case 2:
            opa_table = opa2_table;
            break;
        case 3:
            opa_table = opa3_table;
            break;
        case 4:
            opa_table = opa4_table;
            break;
        default:
            ESP_LOGW(TAG_UI, "Unsupported compressed font bpp: %u", (unsigned)bpp);
            return false;
    }

    ui_font_rle_t rle;
    rle_init(&rle, in, bpp);

    uint8_t *line_prev = malloc(w);
    uint8_t *line_cur = prefilter ? malloc(w) : NULL;
    if(line_prev == NULL || (prefilter && line_cur == NULL)) {
        free(line_prev);
        free(line_cur);
        return false;
    }

    for(uint32_t x = 0; x < w; ++x) {
        line_prev[x] = rle_next(&rle);
        out[x] = opa_table[line_prev[x]];
    }

    for(uint32_t y = 1; y < h; ++y) {
        uint8_t *row = out + (stride * y);
        if(prefilter) {
            for(uint32_t x = 0; x < w; ++x) {
                line_cur[x] = rle_next(&rle);
                line_prev[x] ^= line_cur[x];
                row[x] = opa_table[line_prev[x]];
            }
        }
        else {
            for(uint32_t x = 0; x < w; ++x) {
                line_prev[x] = rle_next(&rle);
                row[x] = opa_table[line_prev[x]];
            }
        }
    }

    free(line_prev);
    free(line_cur);
    return true;
}
#endif
