/**
 * @file photo_scanner.c
 * @brief 图片文件扫描器实现
 */
#include "photo_scanner.h"
#include "config.h"
#include "esp_log.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static char   **s_paths      = NULL;
static int      s_count      = 0;
static int      s_current    = 0;
static char     s_root[128]  = "";

static const char *s_exts[] = PHOTO_EXTS;

static bool is_photo(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    for (int i = 0; i < PHOTO_EXT_COUNT; i++) {
        if (strcasecmp(dot, s_exts[i]) == 0) return true;
    }
    return false;
}

static void scan_dir(const char *path, int *count)
{
    if (*count >= PHOTO_MAX_COUNT) return;

    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && *count < PHOTO_MAX_COUNT) {
        if (entry->d_name[0] == '.') continue;

        char full_path[PHOTO_PATH_MAX_LEN];
        int written = snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        if (written < 0 || written >= (int)sizeof(full_path)) {
            ESP_LOGW(TAG_SCANNER, "Path too long, skipping: %s/%s", path, entry->d_name);
            continue;
        }

        if (entry->d_type == DT_DIR) {
            /* 递归扫描子目录（最多 3 层） */
            int depth = 0;
            const char *p = full_path + strlen(s_root);
            while (*p) { if (*p++ == '/') depth++; }
            if (depth <= 3) scan_dir(full_path, count);
        } else if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            if (is_photo(entry->d_name)) {
                s_paths[*count] = strdup(full_path);
                if (s_paths[*count]) {
                    (*count)++;
                    ESP_LOGD(TAG_SCANNER, "发现图片: %s", full_path);
                }
            }
        }
    }
    closedir(dir);
}

int photo_scanner_init(const char *root_path)
{
    photo_scanner_deinit();

    strlcpy(s_root, root_path, sizeof(s_root));

    s_paths = calloc(PHOTO_MAX_COUNT, sizeof(char *));
    if (!s_paths) {
        ESP_LOGE(TAG_SCANNER, "内存分配失败");
        return -1;
    }

    s_count   = 0;
    s_current = 0;
    scan_dir(root_path, &s_count);

    ESP_LOGI(TAG_SCANNER, "扫描完成: 共找到 %d 张图片", s_count);
    return s_count;
}

int photo_scanner_get_count(void)
{
    return s_count;
}

const char *photo_scanner_get_path(int index)
{
    if (index < 0 || index >= s_count) return NULL;
    return s_paths[index];
}

const char *photo_scanner_next(play_order_t order)
{
    if (s_count == 0) return NULL;

    switch (order) {
    case PLAY_ORDER_SEQUENTIAL:
        s_current = (s_current + 1) % s_count;
        break;
    case PLAY_ORDER_REVERSE:
        s_current = (s_current - 1 + s_count) % s_count;
        break;
    case PLAY_ORDER_RANDOM:
        if (s_count > 1) {
            int next;
            do { next = rand() % s_count; } while (next == s_current);
            s_current = next;
        }
        break;
    }
    return s_paths[s_current];
}

const char *photo_scanner_prev(void)
{
    if (s_count == 0) return NULL;
    s_current = (s_current - 1 + s_count) % s_count;
    return s_paths[s_current];
}

int photo_scanner_get_current_index(void)
{
    return s_current;
}

void photo_scanner_set_index(int index)
{
    if (index >= 0 && index < s_count)
        s_current = index;
}

int photo_scanner_rescan(void)
{
    return photo_scanner_init(s_root);
}

void photo_scanner_deinit(void)
{
    if (s_paths) {
        for (int i = 0; i < s_count; i++) {
            free(s_paths[i]);
        }
        free(s_paths);
        s_paths = NULL;
    }
    s_count   = 0;
    s_current = 0;
}
