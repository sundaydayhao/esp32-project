/**
 * @file audio_player.c
 * @brief Audio player implementation with SD card support
 */
#include "audio_player.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lcd/waveshare_board.h"
#include "sdmmc_cmd.h"

#define SD_MOUNT_POINT                  "/sdcard"
#define AUDIO_TRACK_DEFAULT_DURATION_SEC 180U
#define AUDIO_I2S_WRITE_FRAMES          256U
#define AUDIO_I2S_CHANNEL_COUNT         2U
#define AUDIO_TEST_TONE_PEAK            8000

typedef struct {
    char files[AUDIO_MAX_PLAYLIST_SIZE][AUDIO_MAX_FILENAME_LEN];
    int count;
} playlist_t;

typedef struct {
    i2s_chan_handle_t tx_handle;
    bool enabled;
    uint32_t phase;
    int16_t dma_buffer[AUDIO_I2S_WRITE_FRAMES * AUDIO_I2S_CHANNEL_COUNT];
} audio_output_t;

static const char *TAG = "AUDIO";
static bool s_sd_mounted = false;
static sdmmc_card_t *s_card = NULL;
static audio_player_status_t s_status = {0};
static playlist_t s_playlist = {0};
static audio_output_t s_audio_output = {0};
static SemaphoreHandle_t s_status_mutex = NULL;
static TaskHandle_t s_audio_task = NULL;
static bool s_task_running = false;
static size_t s_subsecond_frames = 0;

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

static void audio_output_reset_waveform(void)
{
    s_audio_output.phase = 0;
}

static void reset_progress_locked(void)
{
    s_status.current_info.position_sec = 0;
    s_subsecond_frames = 0;
}

static void stop_playback_locked(void)
{
    s_status.state = AUDIO_STATE_STOPPED;
    reset_progress_locked();
    audio_output_reset_waveform();
}

static void clear_current_track_locked(void)
{
    s_status.current_track = -1;
    memset(&s_status.current_info, 0, sizeof(s_status.current_info));
    s_subsecond_frames = 0;
}

static void load_playlist_track_locked(int index)
{
    if (index < 0 || index >= s_playlist.count) {
        clear_current_track_locked();
        return;
    }

    s_status.current_track = index;
    strlcpy(s_status.current_info.filename,
            s_playlist.files[index],
            sizeof(s_status.current_info.filename));
    extract_title_from_filename(s_status.current_info.filename,
                                s_status.current_info.title,
                                sizeof(s_status.current_info.title));
    s_status.current_info.duration_sec = AUDIO_TRACK_DEFAULT_DURATION_SEC;
    reset_progress_locked();
    audio_output_reset_waveform();
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

    ret = waveshare_board_set_sd_card_selected(true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to select SD card through CH422G: %s", esp_err_to_name(ret));
        return ret;
    }

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
        (void)waveshare_board_set_sd_card_selected(false);
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SDSPI_SLOT_NO_CS;
    slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        spi_bus_free(host.slot);
        (void)waveshare_board_set_sd_card_selected(false);
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
        (void)waveshare_board_set_sd_card_selected(false);
        s_sd_mounted = false;
        ESP_LOGI(TAG, "SD card unmounted");
    }
}

static esp_err_t audio_output_init(void)
{
    esp_err_t ret;

    if (s_audio_output.tx_handle) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_frame_num = AUDIO_I2S_WRITE_FRAMES;

    ret = i2s_new_channel(&chan_cfg, &s_audio_output.tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate I2S TX channel: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_I2S_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_I2S_PIN_MCLK,
            .bclk = AUDIO_I2S_PIN_BCLK,
            .ws = AUDIO_I2S_PIN_LRCK,
            .dout = AUDIO_I2S_PIN_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_audio_output.tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S STD mode: %s", esp_err_to_name(ret));
        (void)i2s_del_channel(s_audio_output.tx_handle);
        s_audio_output.tx_handle = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "I2S audio ready BCLK=%d LRCK=%d DOUT=%d @ %lu Hz",
             AUDIO_I2S_PIN_BCLK, AUDIO_I2S_PIN_LRCK, AUDIO_I2S_PIN_DOUT,
             (unsigned long)AUDIO_I2S_SAMPLE_RATE_HZ);
    return ESP_OK;
}

static esp_err_t audio_output_start(void)
{
    if (!s_audio_output.tx_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_audio_output.enabled) {
        return ESP_OK;
    }

    esp_err_t ret = i2s_channel_enable(s_audio_output.tx_handle);
    if (ret == ESP_OK) {
        s_audio_output.enabled = true;
        audio_output_reset_waveform();
    }
    return ret;
}

static esp_err_t audio_output_stop(void)
{
    if (!s_audio_output.tx_handle || !s_audio_output.enabled) {
        return ESP_OK;
    }

    esp_err_t ret = i2s_channel_disable(s_audio_output.tx_handle);
    if (ret == ESP_OK) {
        s_audio_output.enabled = false;
    }
    return ret;
}

static void audio_output_deinit(void)
{
    if (!s_audio_output.tx_handle) {
        return;
    }

    (void)audio_output_stop();
    (void)i2s_del_channel(s_audio_output.tx_handle);
    memset(&s_audio_output, 0, sizeof(s_audio_output));
}

static void fill_test_tone_buffer(uint8_t volume)
{
    const uint32_t phase_step = (uint32_t)(((uint64_t)AUDIO_TEST_TONE_FREQ_HZ << 32) / AUDIO_I2S_SAMPLE_RATE_HZ);
    const int32_t amplitude = (AUDIO_TEST_TONE_PEAK * (int32_t)volume) / 100;

    for (size_t i = 0; i < AUDIO_I2S_WRITE_FRAMES; i++) {
        const uint32_t phase16 = s_audio_output.phase >> 16;
        int32_t triangle = (phase16 < 32768U) ? (int32_t)phase16 : (int32_t)(65535U - phase16);
        triangle = (triangle * 2) - 32767;

        int16_t sample = 0;
        if (amplitude > 0) {
            sample = (int16_t)((triangle * amplitude) / 32767);
        }

        s_audio_output.dma_buffer[i * 2] = sample;
        s_audio_output.dma_buffer[i * 2 + 1] = sample;
        s_audio_output.phase += phase_step;
    }
}

static esp_err_t audio_output_write_tone(uint8_t volume, size_t *frames_written)
{
    if (!s_audio_output.tx_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    fill_test_tone_buffer(volume);

    size_t bytes_written = 0;
    esp_err_t ret = i2s_channel_write(s_audio_output.tx_handle,
                                      s_audio_output.dma_buffer,
                                      sizeof(s_audio_output.dma_buffer),
                                      &bytes_written,
                                      pdMS_TO_TICKS(100));
    if (frames_written) {
        *frames_written = bytes_written / (sizeof(int16_t) * AUDIO_I2S_CHANNEL_COUNT);
    }
    return ret;
}

static void advance_progress_locked(size_t frames_written)
{
    if (frames_written == 0) {
        return;
    }

    s_subsecond_frames += frames_written;
    while (s_subsecond_frames >= AUDIO_I2S_SAMPLE_RATE_HZ) {
        s_subsecond_frames -= AUDIO_I2S_SAMPLE_RATE_HZ;
        s_status.current_info.position_sec++;

        if (s_status.current_info.duration_sec > 0 &&
            s_status.current_info.position_sec >= s_status.current_info.duration_sec) {
            if (s_status.current_track >= 0 && s_status.current_track < (s_playlist.count - 1)) {
                load_playlist_track_locked(s_status.current_track + 1);
            } else {
                stop_playback_locked();
            }
            break;
        }
    }
}

static void audio_playback_task(void *arg)
{
    (void)arg;

    while (s_task_running) {
        audio_state_t state = AUDIO_STATE_STOPPED;
        uint8_t volume = 0;

        if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
            state = s_status.state;
            volume = s_status.volume;
            xSemaphoreGive(s_status_mutex);
        }

        if (state != AUDIO_STATE_PLAYING) {
            (void)audio_output_stop();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        esp_err_t ret = audio_output_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start I2S output: %s", esp_err_to_name(ret));
            if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
                stop_playback_locked();
                xSemaphoreGive(s_status_mutex);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        size_t frames_written = 0;
        ret = audio_output_write_tone(volume, &frames_written);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
            if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
                stop_playback_locked();
                xSemaphoreGive(s_status_mutex);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (frames_written > 0 &&
            xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
            if (s_status.state == AUDIO_STATE_PLAYING) {
                advance_progress_locked(frames_written);
            }
            xSemaphoreGive(s_status_mutex);
        }
    }

    (void)audio_output_stop();
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
    s_status.current_track = -1;

    ret = audio_output_init();
    if (ret != ESP_OK) {
        vSemaphoreDelete(s_status_mutex);
        s_status_mutex = NULL;
        return ret;
    }

    ret = mount_sd_card();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card not available, player initialized with I2S output only");
    } else {
        ret = audio_player_scan_files();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "No audio files found on SD card");
        }
    }

    s_task_running = true;
    if (xTaskCreatePinnedToCore(audio_playback_task, "audio_play",
                                TASK_STACK_AUDIO, NULL,
                                TASK_PRIO_AUDIO, &s_audio_task, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio task");
        s_task_running = false;
        unmount_sd_card();
        audio_output_deinit();
        vSemaphoreDelete(s_status_mutex);
        s_status_mutex = NULL;
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
    audio_output_deinit();

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
            load_playlist_track_locked(0);
        } else {
            clear_current_track_locked();
        }
        xSemaphoreGive(s_status_mutex);
    }

    ESP_LOGI(TAG, "Scanned %d audio files", s_playlist.count);
    return ESP_OK;
}

esp_err_t audio_player_play(void)
{
    if (!s_status_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
        const bool has_tracks = s_status.total_tracks > 0;
        if (has_tracks && (s_status.current_track < 0 || s_status.current_track >= s_playlist.count)) {
            load_playlist_track_locked(0);
        }
        if (s_status.state != AUDIO_STATE_PLAYING) {
            audio_output_reset_waveform();
        }
        s_status.state = AUDIO_STATE_PLAYING;
        xSemaphoreGive(s_status_mutex);

        if (has_tracks && s_status.current_info.title[0] != '\0') {
            ESP_LOGI(TAG, "Playing: %s", s_status.current_info.title);
        } else {
            ESP_LOGI(TAG, "Playing I2S test tone on MAX98357A output");
        }
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t audio_player_pause(void)
{
    if (!s_status_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

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
    if (!s_status_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
        stop_playback_locked();
        xSemaphoreGive(s_status_mutex);
        ESP_LOGI(TAG, "Stopped");
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t audio_player_next(void)
{
    if (!s_status_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_status.current_track >= 0 && s_status.current_track < (s_status.total_tracks - 1)) {
            load_playlist_track_locked(s_status.current_track + 1);
            ESP_LOGI(TAG, "Next track: %s", s_status.current_info.title);
        }
        xSemaphoreGive(s_status_mutex);
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t audio_player_prev(void)
{
    if (!s_status_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) == pdTRUE) {
        if (s_status.current_track > 0) {
            load_playlist_track_locked(s_status.current_track - 1);
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

    if (!s_status_mutex) {
        return ESP_ERR_INVALID_STATE;
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

    if (!s_status_mutex) {
        memcpy(status, &s_status, sizeof(audio_player_status_t));
        return ESP_OK;
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
