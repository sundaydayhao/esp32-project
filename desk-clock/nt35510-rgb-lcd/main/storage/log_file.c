#include "storage/log_file.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "config.h"

static sdmmc_card_t *s_sd_card;
static FILE *s_log_file;
static vprintf_like_t s_prev_vprintf;
static bool s_log_ready;
static bool s_spi_bus_initialized;

static void sync_log_file(void)
{
    if (s_log_file == NULL) {
        return;
    }

    fflush(s_log_file);
    int fd = fileno(s_log_file);
    if (fd >= 0) {
        fsync(fd);
    }
}

static esp_err_t ensure_log_dir(void)
{
    int ret = mkdir(SD_LOG_DIR_PATH, 0775);
    if (ret == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

static int sd_log_vprintf(const char *fmt, va_list args)
{
    int console_written = 0;

    if (s_prev_vprintf != NULL) {
        va_list console_args;
        va_copy(console_args, args);
        console_written = s_prev_vprintf(fmt, console_args);
        va_end(console_args);
    }

    if (s_log_file == NULL) {
        return console_written;
    }

    char stack_buf[256];
    va_list file_args;
    va_copy(file_args, args);
    int required = vsnprintf(stack_buf, sizeof(stack_buf), fmt, file_args);
    va_end(file_args);

    if (required < 0) {
        return console_written;
    }

    if (required < (int)sizeof(stack_buf)) {
        fwrite(stack_buf, 1, (size_t)required, s_log_file);
    } else {
        char *heap_buf = malloc((size_t)required + 1U);
        if (heap_buf == NULL) {
            return console_written;
        }

        va_list heap_args;
        va_copy(heap_args, args);
        vsnprintf(heap_buf, (size_t)required + 1U, fmt, heap_args);
        va_end(heap_args);

        fwrite(heap_buf, 1, (size_t)required, s_log_file);
        free(heap_buf);
    }

    sync_log_file();
    return console_written > 0 ? console_written : required;
}

static esp_err_t mount_sd_card(void)
{
    if (SD_LOG_PIN_MISO < 0 || SD_LOG_PIN_MOSI < 0 ||
        SD_LOG_PIN_SCLK < 0 || SD_LOG_PIN_CS < 0) {
        ESP_LOGW(TAG_SDLOG, "SD logging skipped: SPI pins are not configured");
        return ESP_ERR_NOT_SUPPORTED;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = SD_LOG_FREQ_MHZ * 1000;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_LOG_PIN_MOSI,
        .miso_io_num = SD_LOG_PIN_MISO,
        .sclk_io_num = SD_LOG_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret == ESP_OK) {
        s_spi_bus_initialized = true;
    } else if (ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG_SDLOG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = host.slot;
    slot_config.gpio_cs = SD_LOG_PIN_CS;

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    ret = esp_vfs_fat_sdspi_mount(SD_LOG_MOUNT_POINT, &host, &slot_config, &mount_cfg, &s_sd_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_SDLOG, "SD mount failed: %s", esp_err_to_name(ret));
        if (s_spi_bus_initialized) {
            spi_bus_free(host.slot);
            s_spi_bus_initialized = false;
        }
        return ret;
    }

    return ESP_OK;
}

static esp_err_t open_log_file(void)
{
    esp_err_t ret = ensure_log_dir();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_SDLOG, "Failed to create log directory: %s", SD_LOG_DIR_PATH);
        return ret;
    }

    s_log_file = fopen(SD_LOG_FILE_PATH, "a");
    if (s_log_file == NULL) {
        ESP_LOGW(TAG_SDLOG, "Failed to open log file: %s", SD_LOG_FILE_PATH);
        return ESP_FAIL;
    }

    setvbuf(s_log_file, NULL, _IOLBF, 0);
    fprintf(s_log_file,
            "\n===== boot uptime_us=%llu fw=%s device=%s =====\n",
            (unsigned long long)esp_timer_get_time(),
            FW_VERSION_STR,
            DEVICE_NAME);
    sync_log_file();
    return ESP_OK;
}

esp_err_t log_file_init(void)
{
#if !CONFIG_DESK_CLOCK_SD_LOG_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_log_ready) {
        return ESP_OK;
    }

    esp_err_t ret = mount_sd_card();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = open_log_file();
    if (ret != ESP_OK) {
        return ret;
    }

    s_prev_vprintf = esp_log_set_vprintf(sd_log_vprintf);
    s_log_ready = true;

    ESP_LOGI(TAG_SDLOG, "SD log file ready: %s", SD_LOG_FILE_PATH);
    return ESP_OK;
#endif
}

bool log_file_is_ready(void)
{
    return s_log_ready;
}
