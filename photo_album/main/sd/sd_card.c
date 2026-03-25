/**
 * @file sd_card.c
 * @brief SD card driver implementation
 */
#include "sd_card.h"

#include <stdio.h>

#include "board/ch422g_ctrl.h"
#include "config.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

static esp_err_t validate_sd_pins(void)
{
    ESP_RETURN_ON_FALSE(SD_PIN_MOSI >= 0, ESP_ERR_INVALID_ARG, TAG_SD, "SD MOSI pin is not configured");
    ESP_RETURN_ON_FALSE(SD_PIN_MISO >= 0, ESP_ERR_INVALID_ARG, TAG_SD, "SD MISO pin is not configured");
    ESP_RETURN_ON_FALSE(SD_PIN_CLK >= 0, ESP_ERR_INVALID_ARG, TAG_SD, "SD CLK pin is not configured");
#if !BOARD_HAS_CH422G_SD_CS
    ESP_RETURN_ON_FALSE(SD_PIN_CS >= 0, ESP_ERR_INVALID_ARG, TAG_SD, "SD CS pin is not configured");
#endif
    return ESP_OK;
}

static esp_err_t ch422g_init_for_sd(void)
{
#if BOARD_HAS_CH422G_SD_CS
    return ch422g_ctrl_set_pin(BOARD_CH422G_PIN_SD_CS, false);
#else
    return ESP_OK;
#endif
}

static void ch422g_deinit_for_sd(void)
{
#if BOARD_HAS_CH422G_SD_CS
    (void)ch422g_ctrl_set_pin(BOARD_CH422G_PIN_SD_CS, true);
#endif
}

esp_err_t sd_card_init(void)
{
    ESP_LOGI(TAG_SD, "Initialize SD card over SPI");
    ESP_RETURN_ON_ERROR(validate_sd_pins(), TAG_SD, "invalid SD pin configuration");

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = SD_MAX_FILES,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = SD_SPI_FREQ_KHZ;

    esp_err_t ret = ch422g_init_for_sd();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SD, "prepare board SD CS failed: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64 * 1024,
    };

    ret = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG_SD, "SPI bus init failed: %s", esp_err_to_name(ret));
        ch422g_deinit_for_sd();
        return ret;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
#if BOARD_HAS_CH422G_SD_CS
    slot_cfg.gpio_cs = SDSPI_SLOT_NO_CS;
#else
    slot_cfg.gpio_cs = SD_PIN_CS;
#endif
    slot_cfg.host_id = SD_SPI_HOST;

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SD, "SD mount failed: %s", esp_err_to_name(ret));
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG_SD, "Check that the card is inserted and formatted as FAT32");
        }
        ch422g_deinit_for_sd();
        return ret;
    }

    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);

    uint64_t total_mb = 0;
    uint64_t free_mb = 0;
    sd_card_get_info(&total_mb, &free_mb);
    ESP_LOGI(TAG_SD, "SD mounted total=%lluMB free=%lluMB", total_mb, free_mb);
    return ESP_OK;
}

void sd_card_deinit(void)
{
    if (s_mounted) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        spi_bus_free(SD_SPI_HOST);
        ch422g_deinit_for_sd();
        s_mounted = false;
        s_card = NULL;
        ESP_LOGI(TAG_SD, "SD unmounted");
    }
}

bool sd_card_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sd_card_get_info(uint64_t *total_mb, uint64_t *free_mb)
{
    if (!s_mounted || !s_card) {
        return ESP_ERR_INVALID_STATE;
    }

    FATFS *fs = NULL;
    DWORD free_clusters = 0;
    if (f_getfree("0:", &free_clusters, &fs) == FR_OK) {
        uint64_t total_sectors = ((uint64_t)(fs->n_fatent - 2)) * fs->csize;
        uint64_t free_sectors = (uint64_t)free_clusters * fs->csize;
        const uint32_t sector_size = 512;

        if (total_mb) {
            *total_mb = total_sectors * sector_size / (1024 * 1024);
        }
        if (free_mb) {
            *free_mb = free_sectors * sector_size / (1024 * 1024);
        }
    }

    return ESP_OK;
}
