/**
 * @file media_library.c
 * @brief SD card Picture/Video directory indexing helpers
 */
#include "storage/media_library.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "config.h"
#include "esp_log.h"

#define MEDIA_SD_ROOT     "/sdcard"
#define MEDIA_PICTURE_DIR MEDIA_SD_ROOT "/Picture"
#define MEDIA_VIDEO_DIR   MEDIA_SD_ROOT "/Video"

typedef struct {
    media_library_entry_t photos[MEDIA_LIBRARY_MAX_ITEMS];
    media_library_entry_t videos[MEDIA_LIBRARY_MAX_ITEMS];
    int photo_count;
    int video_count;
    uint32_t generation;
} media_library_state_t;

static media_library_state_t s_media = {0};

static bool has_extension(const char *filename, const char *ext)
{
    const char *file_ext = strrchr(filename, '.');
    if (!file_ext) {
        return false;
    }
    return strcasecmp(file_ext, ext) == 0;
}

static bool is_photo_file(const char *filename)
{
    return has_extension(filename, ".jpg") ||
           has_extension(filename, ".jpeg") ||
           has_extension(filename, ".png") ||
           has_extension(filename, ".bmp");
}

static bool is_video_file(const char *filename)
{
    return has_extension(filename, ".mp4") ||
           has_extension(filename, ".avi") ||
           has_extension(filename, ".mov") ||
           has_extension(filename, ".mkv") ||
           has_extension(filename, ".mjpeg") ||
           has_extension(filename, ".mpg") ||
           has_extension(filename, ".mpeg");
}

static void normalize_lvgl_extension(char *path)
{
    char *slash = strrchr(path, '/');
    char *ext = strrchr(path, '.');

    if (!ext || (slash && ext < slash)) {
        return;
    }

    for (char *p = ext + 1; *p != '\0'; ++p) {
        *p = (char)tolower((unsigned char)*p);
    }
}

static void build_lvgl_path(const char *abs_path, char *out, size_t out_size)
{
    snprintf(out, out_size, "A:%s", abs_path);
    normalize_lvgl_extension(out);
}

static void build_abs_path(const char *dir_path, const char *filename, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s/%s", dir_path, filename);
}

static void filename_to_title(const char *filename, char *out, size_t out_size)
{
    const char *ext = strrchr(filename, '.');
    size_t len = ext ? (size_t)(ext - filename) : strlen(filename);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, filename, len);
    out[len] = '\0';
}

static int media_entry_cmp(const void *a, const void *b)
{
    const media_library_entry_t *lhs = (const media_library_entry_t *)a;
    const media_library_entry_t *rhs = (const media_library_entry_t *)b;
    return strcasecmp(lhs->name, rhs->name);
}

static bool file_exists(const char *path)
{
    struct stat st = {0};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void find_video_preview(const char *dir_path,
                               const char *video_name,
                               media_library_entry_t *entry)
{
    static const char *preview_exts[] = {".jpg", ".jpeg", ".png", ".bmp"};
    char base_name[MEDIA_LIBRARY_NAME_LEN] = {0};

    if (!entry) {
        return;
    }

    filename_to_title(video_name, base_name, sizeof(base_name));
    entry->has_preview = false;
    entry->preview_path[0] = '\0';
    entry->preview_lvgl_path[0] = '\0';

    for (size_t i = 0; i < sizeof(preview_exts) / sizeof(preview_exts[0]); i++) {
        char preview_name[MEDIA_LIBRARY_NAME_LEN] = {0};
        snprintf(preview_name, sizeof(preview_name), "%s%s", base_name, preview_exts[i]);
        build_abs_path(dir_path, preview_name, entry->preview_path, sizeof(entry->preview_path));
        if (file_exists(entry->preview_path)) {
            build_lvgl_path(entry->preview_path, entry->preview_lvgl_path, sizeof(entry->preview_lvgl_path));
            entry->has_preview = true;
            return;
        }
    }
}

static int scan_directory(const char *dir_path,
                          media_library_entry_t *items,
                          int max_items,
                          bool (*match_fn)(const char *filename),
                          bool find_preview)
{
    DIR *dir = opendir(dir_path);
    struct dirent *entry = NULL;
    int count = 0;

    if (!dir) {
        ESP_LOGW(TAG_UI, "Open media dir failed: %s", dir_path);
        return 0;
    }

    while ((entry = readdir(dir)) != NULL && count < max_items) {
        if ((entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN) || !match_fn(entry->d_name)) {
            continue;
        }

        memset(&items[count], 0, sizeof(items[count]));
        filename_to_title(entry->d_name, items[count].name, sizeof(items[count].name));
        build_abs_path(dir_path, entry->d_name, items[count].path, sizeof(items[count].path));
        build_lvgl_path(items[count].path, items[count].lvgl_path, sizeof(items[count].lvgl_path));

        if (find_preview) {
            find_video_preview(dir_path, entry->d_name, &items[count]);
        } else {
            items[count].has_preview = true;
            strlcpy(items[count].preview_path, items[count].path, sizeof(items[count].preview_path));
            strlcpy(items[count].preview_lvgl_path, items[count].lvgl_path, sizeof(items[count].preview_lvgl_path));
        }

        count++;
    }

    closedir(dir);

    qsort(items, (size_t)count, sizeof(items[0]), media_entry_cmp);
    return count;
}

esp_err_t media_library_refresh(void)
{
    memset(s_media.photos, 0, sizeof(s_media.photos));
    memset(s_media.videos, 0, sizeof(s_media.videos));

    s_media.photo_count = scan_directory(MEDIA_PICTURE_DIR,
                                         s_media.photos,
                                         MEDIA_LIBRARY_MAX_ITEMS,
                                         is_photo_file,
                                         false);
    s_media.video_count = scan_directory(MEDIA_VIDEO_DIR,
                                         s_media.videos,
                                         MEDIA_LIBRARY_MAX_ITEMS,
                                         is_video_file,
                                         true);
    s_media.generation++;

    return ESP_OK;
}

uint32_t media_library_get_generation(void)
{
    return s_media.generation;
}

int media_library_get_photo_count(void)
{
    return s_media.photo_count;
}

int media_library_get_video_count(void)
{
    return s_media.video_count;
}

const media_library_entry_t *media_library_get_photo(int index)
{
    if (index < 0 || index >= s_media.photo_count) {
        return NULL;
    }
    return &s_media.photos[index];
}

const media_library_entry_t *media_library_get_video(int index)
{
    if (index < 0 || index >= s_media.video_count) {
        return NULL;
    }
    return &s_media.videos[index];
}
