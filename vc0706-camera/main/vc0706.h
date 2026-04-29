/**
 * @file vc0706.h
 * @brief ESP-IDF UART driver for VC0706 serial JPEG camera modules.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VC0706_IMAGE_SIZE_640X480 = 0x00,
    VC0706_IMAGE_SIZE_320X240 = 0x11,
    VC0706_IMAGE_SIZE_160X120 = 0x22,
} vc0706_image_size_t;

typedef struct {
    uart_port_t uart_num;
    gpio_num_t tx_pin;
    gpio_num_t rx_pin;
    uint32_t baud_rate;
    size_t rx_buffer_size;
    size_t tx_buffer_size;
    uint8_t serial_number;
    uint32_t command_timeout_ms;
    uint32_t image_read_timeout_ms;
    uint32_t power_up_delay_ms;
    uint8_t init_retries;
    bool auto_baud;
    uint16_t read_delay_ms;
} vc0706_config_t;

typedef struct {
    vc0706_config_t config;
    uint32_t frame_ptr;
    bool installed;
} vc0706_t;

esp_err_t vc0706_init(vc0706_t *cam, const vc0706_config_t *config);
esp_err_t vc0706_deinit(vc0706_t *cam);

esp_err_t vc0706_reset(vc0706_t *cam);
esp_err_t vc0706_get_version(vc0706_t *cam, char *version, size_t version_size);

esp_err_t vc0706_set_image_size(vc0706_t *cam, vc0706_image_size_t size);
esp_err_t vc0706_get_image_size(vc0706_t *cam, vc0706_image_size_t *size);

esp_err_t vc0706_take_picture(vc0706_t *cam);
esp_err_t vc0706_resume_video(vc0706_t *cam);
esp_err_t vc0706_frame_length(vc0706_t *cam, uint32_t *length);

esp_err_t vc0706_read_picture(vc0706_t *cam, uint8_t *buffer, size_t length, size_t *bytes_read);

const char *vc0706_image_size_name(vc0706_image_size_t size);

#ifdef __cplusplus
}
#endif
