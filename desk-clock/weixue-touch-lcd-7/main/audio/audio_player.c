/**
 * @file audio_player.c
 * @brief Audio player implementation with SD card support
 */
#include "audio_player.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#define SD_MOUNT_POINT "/sdcard"
#define AUDIO_TASK_STACK_SIZE (4 * 1024)
#define AUDIO_TASK_PRIORITY 5

typedef struct {
    char files[AUDIO_MAX_PLAYLIST_SIZE][AUDIO_MAX_FILENAME_LEN];
    int count;
} playlist_t;

static const char *TAG = "AUDIO";
static bool s_sd_mounted = false;
static sdmmc_card_t *s_card = NULL;
static audio_player_status_t s_status = {0};
static playlist_t s_playlist = {0};
static SemaphoreHandle_t s_status_mutex = NULL;
static TaskHandle_t s_audio_task = NULL;
static bool s_task_running = false;

static bool is_audio_file(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (!ext) {
        return false;
    }

    return (strcasecmp(ext, ".mp3") == 0 ||
            strcasecmp(ext, ".wav") == 0 ||
            strcasecmp(ext, ".aac") == 0 ||
            strcasecmp(ext, ".m4a") == 0);
}

static void extract_title_from_filename(const char *filename, char *title, size_t title_size)
{
    const char *basename = strrchr(filename, '/');
    basename = basename ? basename + 1 : filename;

    const char *ext = strrchr(basename, '.');
    size_t len = ext ? (size_t)(ext - basename) : strlen(basename);

    if (len >= title_size) {
        len = title_size - 1;
    }

    strncpy(title, basename, len);
    title[len] = '\0';
}

static bool build_audio_path(const char *filename, char *path, size_t path_size)
{
    const size_t mount_len = sizeof(SD_MOUNT_POINT) - 1;
    const size_t name_len = strlen(filename);

    if (mount_len + 1 + name_len >= path_size) {
        return false;
    }

    memcpy(path, SD_MOUNT_POINT, mount_len);
    path[mount_len] = '/';
    memcpy(path + mount_len + 1, filename, name_len + 1);
    return true;
}

static esp_err_t mount_sd_card(void)
{
    esp_err_t ret;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_PROBING;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        spi_bus_free(host.slot);
        return ret;
    }

    sdmmc_card_print_info(stdout, s_card);
    s_sd_mounted = true;
    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);

    return ESP_OK;
}

static void unmount_sd_card(void)
{
    if (s_sd_mounted) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        spi_bus_free(SDSPI_DEFAULT_HOST);
        s_sd_mounted = false;
        ESP_LOGI(TAG, "SD card unmounted");
    }
}

static void audio_playback_task(void *arg)
{
    (void)arg;

    while (s_task_running) {
        if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
            if (s_status.state == AUDIO_STATE_PLAYING) {
                // Simulate playback progress
                s_status.current_info.position_sec++;
                if (s_status.current_info.position_sec >= s_status.current_info.duration_sec) {
                    // Auto play next track
                    s_status.current_info.position_sec = 0;
                    if (s_status.current_track < s_status.total_tracks - 1) {
                        s_status.current_track++;
                        if (s_status.current_track < s_playlist.count) {
                            strlcpy(s_status.current_info.filename,
                                    s_playlist.files[s_status.current_track],
                                    sizeof(s_status.current_info.filename));
                            extract_title_from_filename(s_status.current_info.filename,
                                                        s_status.current_info.title,
                                                        sizeof(s_status.current_info.title));
                            s_status.current_info.duration_sec = 180; // Default 3 minutes
                        }
                    } else {
                        s_status.state = AUDIO_STATE_STOPPED;
                    }
                }
            }
            xSemaphoreGive(s_status_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelete(NULL);
}

esp_err_t audio_player_init(void)
{
    esp_err_t ret;

    s_status_mutex = xSemaphoreCreateMutex();
    if (!s_status_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(&s_status, 0, sizeof(s_status));
    s_status.volume = 50;
    s_status.state = AUDIO_STATE_STOPPED;

    ret = mount_sd_card();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card not available, player initialized without storage");
        return ESP_OK; // Not a fatal error
    }

    ret = audio_player_scan_files();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No audio files found on SD card");
    }

    s_task_running = true;
    if (xTaskCreatePinnedToCore(audio_playback_task, "audio_play",
                                AUDIO_TASK_STACK_SIZE, NULL,
                                AUDIO_TASK_PRIORITY, &s_audio_task, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio task");
        s_task_running = false;
        unmount_sd_card();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Audio player initialized");
    return ESP_OK;
}

void audio_player_deinit(void)
{
    s_task_running = false;
    if (s_audio_task) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_audio_task = NULL;
    }

    unmount_sd_card();

    if (s_status_mutex) {
        vSemaphoreDelete(s_status_mutex);
        s_status_mutex = NULL;
    }

    ESP_LOGI(TAG, "Audio player deinitialized");
}

esp_err_t audio_player_scan_files(void)
{
    if (!s_sd_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory");
        return ESP_FAIL;
    }

    s_playlist.count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && s_playlist.count < AUDIO_MAX_PLAYLIST_SIZE) {
        if (entry->d_type == DT_REG && is_audio_file(entry->d_name)) {
            if (!build_audio_path(entry->d_name,
                                  s_playlist.files[s_playlist.count],
                                  AUDIO_MAX_FILENAME_LEN)) {
                ESP_LOGW(TAG, "Skip audio file with long path: %s", entry->d_name);
                continue;
            }
            s_playlist.count++;
            ESP_LOGI(TAG, "Found audio file: %s", entry->d_name);
        }
    }

    closedir(dir);

    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
        s_status.total_tracks = s_playlist.count;
        if (s_playlist.count > 0) {
            s_status.current_track = 0;
            strlcpy(s_status.current_info.filename,
                    s_playlist.files[0],
                    sizeof(s_status.current_info.filename));
            extract_title_from_filename(s_status.current_info.filename,
                                        s_status.current_info.title,
                                        sizeof(s_status.current_info.title));
            s_status.current_info.duration_sec = 180; // Default 3 minutes
            s_status.current_info.position_sec = 0;
        }
        xSemaphoreGive(s_status_mutex);
    }

    ESP_LOGI(TAG, "Scanned %d audio files", s_playlist.count);
    return ESP_OK;
}

esp_err_t audio_player_play(void)
{
    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_status.total_tracks == 0) {
            xSemaphoreGive(s_status_mutex);
            return ESP_ERR_INVALID_STATE;
        }

        s_status.state = AUDIO_STATE_PLAYING;
        xSemaphoreGive(s_status_mutex);
        ESP_LOGI(TAG, "Playing: %s", s_status.current_info.title);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t audio_player_pause(void)
{
    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
        s_status.state = AUDIO_STATE_PAUSED;
        xSemaphoreGive(s_status_mutex);
        ESP_LOGI(TAG, "Paused");
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t audio_player_stop(void)
{
    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
        s_status.state = AUDIO_STATE_STOPPED;
        s_status.current_info.position_sec = 0;
        xSemaphoreGive(s_status_mutex);
        ESP_LOGI(TAG, "Stopped");
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t audio_player_next(void)
{
    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_status.current_track < s_status.total_tracks - 1) {
            s_status.current_track++;
            s_status.current_info.position_sec = 0;

            if (s_status.current_track < s_playlist.count) {
                strlcpy(s_status.current_info.filename,
                        s_playlist.files[s_status.current_track],
                        sizeof(s_status.current_info.filename));
                extract_title_from_filename(s_status.current_info.filename,
                                            s_status.current_info.title,
                                            sizeof(s_status.current_info.title));
                s_status.current_info.duration_sec = 180;
            }

            ESP_LOGI(TAG, "Next track: %s", s_status.current_info.title);
        }
        xSemaphoreGive(s_status_mutex);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t audio_player_prev(void)
{
    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_status.current_track > 0) {
            s_status.current_track--;
            s_status.current_info.position_sec = 0;

            if (s_status.current_track < s_playlist.count) {
                strlcpy(s_status.current_info.filename,
                        s_playlist.files[s_status.current_track],
                        sizeof(s_status.current_info.filename));
                extract_title_from_filename(s_status.current_info.filename,
                                            s_status.current_info.title,
                                            sizeof(s_status.current_info.title));
                s_status.current_info.duration_sec = 180;
            }

            ESP_LOGI(TAG, "Previous track: %s", s_status.current_info.title);
        }
        xSemaphoreGive(s_status_mutex);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t audio_player_set_volume(uint8_t volume)
{
    if (volume > 100) {
        volume = 100;
    }

    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
        s_status.volume = volume;
        xSemaphoreGive(s_status_mutex);
        ESP_LOGI(TAG, "Volume set to %d%%", volume);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t audio_player_get_status(audio_player_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
        memcpy(status, &s_status, sizeof(audio_player_status_t));
        xSemaphoreGive(s_status_mutex);
        return ESP_OK;
    }

    return ESP_FAIL;
}

bool audio_player_is_sd_mounted(void)
{
    return s_sd_mounted;
}
