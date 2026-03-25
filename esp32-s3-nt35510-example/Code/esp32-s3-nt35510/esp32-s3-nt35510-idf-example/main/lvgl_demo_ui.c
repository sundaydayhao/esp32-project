/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "lvgl.h"

#define EXAMPLE_IMAGE_SCAN_DIR     "/sdcard"
#define EXAMPLE_IMAGE_LVGL_PREFIX  "S:/sdcard/"
#define EXAMPLE_MAX_PATH_LEN       128

static bool example_extension_equals(const char *ext, const char *expected)
{
    size_t i;

    for (i = 0; expected[i] != '\0'; i++) {
        char a = ext[i];
        char b = expected[i];

        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }

    return ext[i] == '\0';
}

static bool example_has_jpeg_extension(const char *name)
{
    const char *ext = strrchr(name, '.');

    if (ext == NULL) {
        return false;
    }

    ext++;
    return example_extension_equals(ext, "jpg") || example_extension_equals(ext, "sjpg");
}

static void example_normalize_jpeg_extension(char *path)
{
    char *ext = strrchr(path, '.');

    if (ext == NULL) {
        return;
    }

    ext++;
    if (example_extension_equals(ext, "jpg")) {
        strcpy(ext, "jpg");
    } else if (example_extension_equals(ext, "sjpg")) {
        strcpy(ext, "sjpg");
    }
}

static bool example_find_first_jpeg(char *path, size_t path_len)
{
    DIR *dir = opendir(EXAMPLE_IMAGE_SCAN_DIR);
    struct dirent *entry;

    if (dir == NULL) {
        return false;
    }

    while ((entry = readdir(dir)) != NULL) {
        int written;

        if (!example_has_jpeg_extension(entry->d_name)) {
            continue;
        }

        written = snprintf(path,
                           path_len,
                           EXAMPLE_IMAGE_LVGL_PREFIX "%s",
                           entry->d_name);
        if (written > 0 && written < path_len) {
            example_normalize_jpeg_extension(path);
            closedir(dir);
            return true;
        }
    }

    closedir(dir);
    return false;
}

static bool example_probe_jpeg(const char *path, lv_img_header_t *header)
{
    lv_img_decoder_dsc_t decoder_dsc;

    if (lv_img_decoder_get_info(path, header) != LV_RES_OK) {
        return false;
    }

    if (lv_img_decoder_open(&decoder_dsc, path, lv_color_black(), 0) != LV_RES_OK) {
        return false;
    }

    lv_img_decoder_close(&decoder_dsc);
    return true;
}

void example_lvgl_demo_ui(lv_disp_t *disp, esp_err_t sdcard_ret)
{
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_t *status = lv_label_create(scr);
    char image_path[EXAMPLE_MAX_PATH_LEN] = {0};
    const char *image_name;
    lv_img_header_t header;

    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    lv_obj_set_width(status, 760);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status, lv_color_white(), 0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_20, 0);

    if (sdcard_ret != ESP_OK) {
        lv_label_set_text_fmt(status, "SD mount failed: %s", esp_err_to_name(sdcard_ret));
        lv_obj_center(status);
        return;
    }

    if (!example_find_first_jpeg(image_path, sizeof(image_path))) {
        lv_label_set_text(status, "No JPG or SJPG files found in /sdcard");
        lv_obj_center(status);
        return;
    }

    if (!example_probe_jpeg(image_path, &header)) {
        lv_label_set_text_fmt(status, "JPEG decode failed: %s", image_path);
        lv_obj_center(status);
        return;
    }

    lv_obj_t *image = lv_img_create(scr);
    lv_img_set_src(image, image_path);
    lv_obj_center(image);

    image_name = strrchr(image_path, '/');
    if (image_name == NULL) {
        image_name = image_path;
    } else {
        image_name++;
    }

    lv_label_set_text_fmt(status, "Loaded %s (%dx%d)", image_name, header.w, header.h);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -24);
}
