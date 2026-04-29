/**
 * @file main.c
 * @brief VC0706 serial JPEG camera demo for ESP-IDF.
 *
 * Flow:
 *  1. Initialize UART camera.
 *  2. Mount SD card over SPI.
 *  3. Wait for active-low button.
 *  4. Capture JPEG from VC0706 FBUF and save IMAGE00.JPG ... IMAGE99.JPG.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"
#include "vc0706.h"

static const char *TAG = "APP";

#ifndef CONFIG_VC0706_POWER_UP_DELAY_MS
#define CONFIG_VC0706_POWER_UP_DELAY_MS 1500
#define APP_VC0706_KCONFIG_STALE 1
#else
#define APP_VC0706_KCONFIG_STALE 0
#endif

#ifndef CONFIG_VC0706_INIT_RETRIES
#define CONFIG_VC0706_INIT_RETRIES 3
#endif

#if defined(CONFIG_VC0706_AUTO_BAUD) || APP_VC0706_KCONFIG_STALE
#define APP_VC0706_AUTO_BAUD true
#else
#define APP_VC0706_AUTO_BAUD false
#endif

#define SD_MOUNT_POINT "/sdcard"
#define MAX_IMAGE_INDEX 100

#if CONFIG_VC0706_SD_SPI3_HOST
#define APP_SD_SPI_HOST SPI3_HOST
#else
#define APP_SD_SPI_HOST SPI2_HOST
#endif

#if CONFIG_VC0706_IMAGE_SIZE_640X480
#define APP_IMAGE_SIZE VC0706_IMAGE_SIZE_640X480
#elif CONFIG_VC0706_IMAGE_SIZE_320X240
#define APP_IMAGE_SIZE VC0706_IMAGE_SIZE_320X240
#else
#define APP_IMAGE_SIZE VC0706_IMAGE_SIZE_160X120
#endif

static sdmmc_card_t *s_card = NULL;
static bool s_sd_mounted = false;

static esp_err_t mount_sd_card(void)
{
#if CONFIG_VC0706_SAVE_TO_SD
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = APP_SD_SPI_HOST;
    host.max_freq_khz = CONFIG_VC0706_SD_SPI_FREQ_KHZ;

    spi_bus_config_t bus_config = {
        .mosi_io_num = CONFIG_VC0706_SD_MOSI_GPIO,
        .miso_io_num = CONFIG_VC0706_SD_MISO_GPIO,
        .sclk_io_num = CONFIG_VC0706_SD_CLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t ret = spi_bus_initialize(APP_SD_SPI_HOST, &bus_config, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SD SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CONFIG_VC0706_SD_CS_GPIO;
    slot_config.host_id = APP_SD_SPI_HOST;

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Check that the card is inserted and formatted as FAT32");
        }
        return ret;
    }

    s_sd_mounted = true;
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD mounted at %s", SD_MOUNT_POINT);
#endif
    return ESP_OK;
}

static void unmount_sd_card(void)
{
#if CONFIG_VC0706_SAVE_TO_SD
    if (s_sd_mounted) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        spi_bus_free(APP_SD_SPI_HOST);
        s_sd_mounted = false;
        s_card = NULL;
    }
#endif
}

static esp_err_t init_capture_button(void)
{
    if (CONFIG_VC0706_BUTTON_GPIO < 0) {
        return ESP_OK;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << CONFIG_VC0706_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io_conf);
}

static bool capture_button_pressed(void)
{
    if (CONFIG_VC0706_BUTTON_GPIO < 0) {
        return false;
    }
    return gpio_get_level(CONFIG_VC0706_BUTTON_GPIO) == 0;
}

static esp_err_t next_image_path(char *path, size_t path_size)
{
#if CONFIG_VC0706_SAVE_TO_SD
    for (int i = 0; i < MAX_IMAGE_INDEX; ++i) {
        int written = snprintf(path, path_size, SD_MOUNT_POINT "/IMAGE%02d.JPG", i);
        if (written < 0 || (size_t)written >= path_size) {
            return ESP_ERR_INVALID_SIZE;
        }

        struct stat st;
        if (stat(path, &st) != 0) {
            if (errno == ENOENT) {
                return ESP_OK;
            }
            return ESP_FAIL;
        }
    }
    return ESP_ERR_NOT_FOUND;
#else
    (void)path;
    (void)path_size;
    return ESP_OK;
#endif
}

static esp_err_t capture_jpeg(vc0706_t *camera)
{
    esp_err_t ret = ESP_OK;
    FILE *image_file = NULL;
    bool frame_stopped = false;

    ESP_LOGI(TAG, "Taking picture...");
    ESP_RETURN_ON_ERROR(vc0706_take_picture(camera), TAG, "take picture failed");
    frame_stopped = true;

    uint32_t jpg_len = 0;
    ESP_GOTO_ON_ERROR(vc0706_frame_length(camera, &jpg_len), cleanup, TAG, "get frame length failed");
    ESP_GOTO_ON_FALSE(jpg_len > 0, ESP_FAIL, cleanup, TAG, "camera returned empty JPEG");

    ESP_LOGI(TAG, "Picture taken: %lu bytes", (unsigned long)jpg_len);

#if CONFIG_VC0706_SAVE_TO_SD
    char path[64] = {0};
    ESP_GOTO_ON_ERROR(next_image_path(path, sizeof(path)), cleanup, TAG, "no free IMAGExx.JPG name");

    image_file = fopen(path, "wb");
    ESP_GOTO_ON_FALSE(image_file != NULL, ESP_FAIL, cleanup, TAG, "open %s failed", path);
    ESP_LOGI(TAG, "Writing image to %s", path);
#else
    ESP_LOGI(TAG, "SD saving disabled; reading JPEG data and discarding it");
#endif

    uint8_t buffer[CONFIG_VC0706_READ_CHUNK_SIZE] = {0};
    uint32_t remaining = jpg_len;
    uint32_t written_total = 0;
    bool checked_soi = false;
    uint8_t last_two[2] = {0};

    while (remaining > 0) {
        size_t to_read = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        size_t read = 0;

        ESP_GOTO_ON_ERROR(vc0706_read_picture(camera, buffer, to_read, &read),
                          cleanup,
                          TAG,
                          "read JPEG chunk failed");
        ESP_GOTO_ON_FALSE(read == to_read, ESP_FAIL, cleanup, TAG, "short JPEG chunk");

        if (!checked_soi && read >= 2) {
            checked_soi = true;
            if (buffer[0] != 0xFF || buffer[1] != 0xD8) {
                ESP_LOGW(TAG, "JPEG SOI marker not found at start");
            }
        }
        if (read >= 2) {
            last_two[0] = buffer[read - 2];
            last_two[1] = buffer[read - 1];
        } else if (read == 1) {
            last_two[0] = last_two[1];
            last_two[1] = buffer[0];
        }

#if CONFIG_VC0706_SAVE_TO_SD
        size_t written = fwrite(buffer, 1, read, image_file);
        ESP_GOTO_ON_FALSE(written == read, ESP_FAIL, cleanup, TAG, "write JPEG chunk failed");
#endif

        remaining -= read;
        written_total += read;

        if ((written_total % 4096) < read || remaining == 0) {
            ESP_LOGI(TAG, "JPEG progress: %lu/%lu bytes",
                     (unsigned long)written_total,
                     (unsigned long)jpg_len);
        }
    }

    if (last_two[0] != 0xFF || last_two[1] != 0xD9) {
        ESP_LOGW(TAG, "JPEG EOI marker not found at end");
    }

#if CONFIG_VC0706_SAVE_TO_SD
    ESP_GOTO_ON_FALSE(fclose(image_file) == 0, ESP_FAIL, cleanup, TAG, "close image file failed");
    image_file = NULL;
    ESP_LOGI(TAG, "Done, saved %lu bytes", (unsigned long)written_total);
#endif

cleanup:
#if CONFIG_VC0706_SAVE_TO_SD
    if (image_file) {
        fclose(image_file);
    }
#endif
    if (frame_stopped) {
        esp_err_t resume_ret = vc0706_resume_video(camera);
        if (resume_ret != ESP_OK) {
            ESP_LOGW(TAG, "resume video failed: %s", esp_err_to_name(resume_ret));
            if (ret == ESP_OK) {
                ret = resume_ret;
            }
        }
    }
    return ret;
}

void app_main(void)
{
    ESP_LOGI(TAG, "VC0706 serial JPEG camera test");
    ESP_LOGI(TAG, "Camera UART%d TX=%d RX=%d baud=%d",
             CONFIG_VC0706_UART_NUM,
             CONFIG_VC0706_UART_TX_GPIO,
             CONFIG_VC0706_UART_RX_GPIO,
             CONFIG_VC0706_UART_BAUD_RATE);

    vc0706_t camera = {0};
    vc0706_config_t camera_config = {
        .uart_num = CONFIG_VC0706_UART_NUM,
        .tx_pin = CONFIG_VC0706_UART_TX_GPIO,
        .rx_pin = CONFIG_VC0706_UART_RX_GPIO,
        .baud_rate = CONFIG_VC0706_UART_BAUD_RATE,
        .rx_buffer_size = CONFIG_VC0706_UART_RX_BUFFER_SIZE,
        .tx_buffer_size = 0,
        .serial_number = 0x00,
        .command_timeout_ms = CONFIG_VC0706_COMMAND_TIMEOUT_MS,
        .image_read_timeout_ms = CONFIG_VC0706_IMAGE_READ_TIMEOUT_MS,
        .power_up_delay_ms = CONFIG_VC0706_POWER_UP_DELAY_MS,
        .init_retries = CONFIG_VC0706_INIT_RETRIES,
        .auto_baud = APP_VC0706_AUTO_BAUD,
        .read_delay_ms = CONFIG_VC0706_READ_DELAY_MS,
    };

    esp_err_t ret = vc0706_init(&camera, &camera_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No camera found: %s", esp_err_to_name(ret));
        return;
    }

    char version[64] = {0};
    ret = vc0706_get_version(&camera, version, sizeof(version));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Camera Found: %s", version);
    } else {
        ESP_LOGW(TAG, "Failed to get version: %s", esp_err_to_name(ret));
    }

    ESP_ERROR_CHECK(vc0706_set_image_size(&camera, APP_IMAGE_SIZE));
    vc0706_image_size_t image_size = 0;
    ESP_ERROR_CHECK(vc0706_get_image_size(&camera, &image_size));
    ESP_LOGI(TAG, "Image size: %s", vc0706_image_size_name(image_size));

    ESP_ERROR_CHECK(mount_sd_card());
    ESP_ERROR_CHECK(init_capture_button());

    if (CONFIG_VC0706_BUTTON_GPIO < 0) {
        ESP_LOGI(TAG, "Button disabled; capturing once now");
        ESP_ERROR_CHECK(capture_jpeg(&camera));
    } else {
        ESP_LOGI(TAG, "Ready. Press GPIO%d button to capture.", CONFIG_VC0706_BUTTON_GPIO);
    }

    while (true) {
        if (capture_button_pressed()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            if (capture_button_pressed()) {
                ret = capture_jpeg(&camera);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Capture failed: %s", esp_err_to_name(ret));
                }
                while (capture_button_pressed()) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    unmount_sd_card();
    (void)vc0706_deinit(&camera);
}
