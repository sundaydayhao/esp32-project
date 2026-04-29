/**
 * @file vc0706.c
 * @brief ESP-IDF UART driver for VC0706 serial JPEG camera modules.
 */
#include "vc0706.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "VC0706";

#define VC0706_PROTOCOL_SIGN       0x56
#define VC0706_RESPONSE_SIGN       0x76
#define VC0706_STATUS_OK           0x00

#define VC0706_CMD_GEN_VERSION     0x11
#define VC0706_CMD_RESET           0x26
#define VC0706_CMD_READ_DATA       0x30
#define VC0706_CMD_WRITE_DATA      0x31
#define VC0706_CMD_READ_FBUF       0x32
#define VC0706_CMD_GET_FBUF_LEN    0x34
#define VC0706_CMD_FBUF_CTRL       0x36

#define VC0706_STOP_CURRENT_FRAME  0x00
#define VC0706_RESUME_FRAME        0x03

#define VC0706_MAX_ARGS            16
#define VC0706_READ_FBUF_DATA_LEN  0x0C
#define VC0706_READ_FBUF_CURRENT   0x00
#define VC0706_READ_FBUF_MODE      0x0A

static esp_err_t uart_read_exact(vc0706_t *cam, uint8_t *data, size_t length, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(cam && data, ESP_ERR_INVALID_ARG, TAG, "invalid read args");

    size_t total = 0;
    const int64_t deadline = esp_timer_get_time() + ((int64_t)timeout_ms * 1000);

    while (total < length) {
        int64_t now = esp_timer_get_time();
        if (now >= deadline) {
            if (total > 0) {
                ESP_LOGW(TAG, "UART read timeout: got %u/%u bytes, first bytes: %02x %02x %02x %02x",
                         (unsigned)total,
                         (unsigned)length,
                         total > 0 ? data[0] : 0,
                         total > 1 ? data[1] : 0,
                         total > 2 ? data[2] : 0,
                         total > 3 ? data[3] : 0);
            }
            return ESP_ERR_TIMEOUT;
        }

        uint32_t remaining_ms = (uint32_t)((deadline - now + 999) / 1000);
        if (remaining_ms > 50) {
            remaining_ms = 50;
        }

        TickType_t wait_ticks = pdMS_TO_TICKS(remaining_ms);
        if (wait_ticks == 0) {
            wait_ticks = 1;
        }

        int read = uart_read_bytes(cam->config.uart_num, data + total, length - total, wait_ticks);
        if (read < 0) {
            return ESP_FAIL;
        }
        total += (size_t)read;
    }

    return ESP_OK;
}

static void vc0706_flush_input(vc0706_t *cam)
{
    if (cam && cam->installed) {
        (void)uart_flush_input(cam->config.uart_num);
    }
}

static esp_err_t vc0706_send_command(vc0706_t *cam, uint8_t command, const uint8_t *args, size_t args_len)
{
    ESP_RETURN_ON_FALSE(cam && cam->installed, ESP_ERR_INVALID_STATE, TAG, "camera UART not initialized");
    ESP_RETURN_ON_FALSE(args_len <= VC0706_MAX_ARGS, ESP_ERR_INVALID_ARG, TAG, "too many command args");
    ESP_RETURN_ON_FALSE(args || args_len == 0, ESP_ERR_INVALID_ARG, TAG, "args is NULL");

    uint8_t frame[3 + VC0706_MAX_ARGS] = {
        VC0706_PROTOCOL_SIGN,
        cam->config.serial_number,
        command,
    };
    if (args_len > 0) {
        memcpy(&frame[3], args, args_len);
    }

    const size_t frame_len = 3 + args_len;
    int written = uart_write_bytes(cam->config.uart_num, frame, frame_len);
    if (written != (int)frame_len) {
        return ESP_FAIL;
    }
    return uart_wait_tx_done(cam->config.uart_num, pdMS_TO_TICKS(cam->config.command_timeout_ms));
}

static esp_err_t vc0706_verify_response(vc0706_t *cam, uint8_t command, const uint8_t *response, size_t response_len)
{
    ESP_RETURN_ON_FALSE(cam && response, ESP_ERR_INVALID_ARG, TAG, "invalid response args");
    ESP_RETURN_ON_FALSE(response_len >= 5, ESP_ERR_INVALID_SIZE, TAG, "short response");

    if (response[0] != VC0706_RESPONSE_SIGN ||
        response[1] != cam->config.serial_number ||
        response[2] != command) {
        ESP_LOGD(TAG, "unexpected response: %02x %02x %02x %02x %02x",
                 response[0], response[1], response[2], response[3], response[4]);
        return ESP_FAIL;
    }

    if (response[3] != VC0706_STATUS_OK) {
        ESP_LOGW(TAG, "command 0x%02x failed, status=0x%02x", command, response[3]);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t vc0706_run_command(vc0706_t *cam,
                                    uint8_t command,
                                    const uint8_t *args,
                                    size_t args_len,
                                    uint8_t *response,
                                    size_t response_len,
                                    bool flush_input)
{
    ESP_RETURN_ON_FALSE(response && response_len >= 5, ESP_ERR_INVALID_ARG, TAG, "invalid response buffer");

    if (flush_input) {
        vc0706_flush_input(cam);
    }

    ESP_RETURN_ON_ERROR(vc0706_send_command(cam, command, args, args_len), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(uart_read_exact(cam, response, response_len, cam->config.command_timeout_ms),
                        TAG, "read command response failed");
    return vc0706_verify_response(cam, command, response, response_len);
}

static esp_err_t vc0706_probe_current_baud(vc0706_t *cam)
{
    const uint8_t retries = cam->config.init_retries > 0 ? cam->config.init_retries : 1;

    for (uint8_t i = 0; i < retries; ++i) {
        esp_err_t ret = vc0706_reset(cam);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "camera answered reset at %lu baud", (unsigned long)cam->config.baud_rate);
            return ESP_OK;
        }

        char version[64] = {0};
        ret = vc0706_get_version(cam, version, sizeof(version));
        if (ret == ESP_OK) {
            ESP_LOGW(TAG, "camera did not answer reset but version probe worked at %lu baud: %s",
                     (unsigned long)cam->config.baud_rate,
                     version);
            cam->frame_ptr = 0;
            return ESP_OK;
        }

        ESP_LOGW(TAG, "probe attempt %u/%u failed at %lu baud: %s",
                 (unsigned)(i + 1),
                 (unsigned)retries,
                 (unsigned long)cam->config.baud_rate,
                 esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    return ESP_ERR_TIMEOUT;
}

static esp_err_t vc0706_probe_camera(vc0706_t *cam)
{
    static const uint32_t common_bauds[] = {38400, 9600, 19200, 57600, 115200};
    const uint32_t first_baud = cam->config.baud_rate;

    ESP_LOGI(TAG, "probing camera at %lu baud", (unsigned long)cam->config.baud_rate);
    esp_err_t ret = vc0706_probe_current_baud(cam);
    if (ret == ESP_OK || !cam->config.auto_baud) {
        return ret;
    }

    for (size_t i = 0; i < sizeof(common_bauds) / sizeof(common_bauds[0]); ++i) {
        if (common_bauds[i] == first_baud) {
            continue;
        }

        ret = uart_set_baudrate(cam->config.uart_num, (uint32_t)common_bauds[i]);
        if (ret != ESP_OK) {
            return ret;
        }

        cam->config.baud_rate = common_bauds[i];
        ESP_LOGI(TAG, "probing camera at %lu baud", (unsigned long)cam->config.baud_rate);

        ret = vc0706_probe_current_baud(cam);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t vc0706_init(vc0706_t *cam, const vc0706_config_t *config)
{
    ESP_RETURN_ON_FALSE(cam && config, ESP_ERR_INVALID_ARG, TAG, "invalid init args");
    ESP_RETURN_ON_FALSE(config->rx_buffer_size >= 512, ESP_ERR_INVALID_ARG, TAG, "RX buffer too small");

    memset(cam, 0, sizeof(*cam));
    cam->config = *config;
    if (cam->config.command_timeout_ms == 0) {
        cam->config.command_timeout_ms = 500;
    }
    if (cam->config.image_read_timeout_ms == 0) {
        cam->config.image_read_timeout_ms = 1500;
    }
    if (cam->config.init_retries == 0) {
        cam->config.init_retries = 1;
    }

    uart_config_t uart_config = {
        .baud_rate = (int)cam->config.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(cam->config.uart_num,
                                        (int)cam->config.rx_buffer_size,
                                        (int)cam->config.tx_buffer_size,
                                        0,
                                        NULL,
                                        0);
    ESP_RETURN_ON_ERROR(ret, TAG, "install UART driver failed");
    cam->installed = true;

    ESP_GOTO_ON_ERROR(uart_param_config(cam->config.uart_num, &uart_config), fail, TAG, "config UART failed");
    ESP_GOTO_ON_ERROR(uart_set_pin(cam->config.uart_num,
                                   cam->config.tx_pin,
                                   cam->config.rx_pin,
                                   UART_PIN_NO_CHANGE,
                                   UART_PIN_NO_CHANGE),
                      fail,
                      TAG,
                      "set UART pins failed");
    vc0706_flush_input(cam);

    if (cam->config.power_up_delay_ms > 0) {
        ESP_LOGI(TAG, "waiting %lu ms for camera power-up",
                 (unsigned long)cam->config.power_up_delay_ms);
        vTaskDelay(pdMS_TO_TICKS(cam->config.power_up_delay_ms));
    }

    int rx_level = gpio_get_level(cam->config.rx_pin);
    ESP_LOGI(TAG, "RX GPIO%d idle level=%d, expected 1 when camera TX is connected",
             cam->config.rx_pin,
             rx_level);
    if (rx_level == 0) {
        ESP_LOGW(TAG, "RX is low before probing; check camera power, GND, TX pin, or use another GPIO");
    }

    ret = vc0706_probe_camera(cam);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "camera probe failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    ESP_LOGI(TAG, "UART%d initialized, TX=%d RX=%d baud=%lu",
             cam->config.uart_num,
             cam->config.tx_pin,
             cam->config.rx_pin,
             (unsigned long)cam->config.baud_rate);
    return ESP_OK;

fail:
    (void)vc0706_deinit(cam);
    return ret;
}

esp_err_t vc0706_deinit(vc0706_t *cam)
{
    ESP_RETURN_ON_FALSE(cam, ESP_ERR_INVALID_ARG, TAG, "invalid deinit args");

    if (cam->installed) {
        ESP_RETURN_ON_ERROR(uart_driver_delete(cam->config.uart_num), TAG, "delete UART driver failed");
        cam->installed = false;
    }
    return ESP_OK;
}

esp_err_t vc0706_reset(vc0706_t *cam)
{
    uint8_t response[5] = {0};
    const uint8_t args[] = {0x00};
    ESP_RETURN_ON_ERROR(vc0706_run_command(cam, VC0706_CMD_RESET, args, sizeof(args),
                                           response, sizeof(response), true),
                        TAG, "reset command failed");

    vTaskDelay(pdMS_TO_TICKS(100));
    vc0706_flush_input(cam);
    cam->frame_ptr = 0;
    return ESP_OK;
}

esp_err_t vc0706_get_version(vc0706_t *cam, char *version, size_t version_size)
{
    ESP_RETURN_ON_FALSE(cam && version && version_size > 0, ESP_ERR_INVALID_ARG, TAG, "invalid version args");

    uint8_t response[80] = {0};
    const uint8_t arduino_args[] = {0x01};
    vc0706_flush_input(cam);
    esp_err_t err = vc0706_send_command(cam, VC0706_CMD_GEN_VERSION, arduino_args, sizeof(arduino_args));
    if (err == ESP_OK) {
        err = uart_read_exact(cam, response, 5, cam->config.command_timeout_ms);
        if (err == ESP_OK && vc0706_verify_response(cam, VC0706_CMD_GEN_VERSION, response, 5) == ESP_OK) {
            size_t payload_len = response[4];
            if (payload_len > sizeof(response) - 6) {
                payload_len = sizeof(response) - 6;
            }
            err = uart_read_exact(cam, &response[5], payload_len, cam->config.command_timeout_ms);
            if (err != ESP_OK) {
                goto protocol_fallback;
            }
            if (payload_len >= version_size) {
                payload_len = version_size - 1;
            }
            memcpy(version, &response[5], payload_len);
            version[payload_len] = '\0';
            return ESP_OK;
        }
    }

protocol_fallback:
    vc0706_flush_input(cam);

    const uint8_t protocol_args[] = {0x00};
    ESP_RETURN_ON_ERROR(vc0706_send_command(cam, VC0706_CMD_GEN_VERSION, protocol_args, sizeof(protocol_args)),
                        TAG, "send version command failed");

    ESP_RETURN_ON_ERROR(uart_read_exact(cam, response, 5, cam->config.command_timeout_ms),
                        TAG, "read version header failed");
    ESP_RETURN_ON_ERROR(vc0706_verify_response(cam, VC0706_CMD_GEN_VERSION, response, 5),
                        TAG, "version response failed");

    size_t payload_len = response[4];
    if (payload_len > sizeof(response) - 6) {
        payload_len = sizeof(response) - 6;
    }
    ESP_RETURN_ON_ERROR(uart_read_exact(cam, &response[5], payload_len, cam->config.command_timeout_ms),
                        TAG, "read version payload failed");

    if (payload_len >= version_size) {
        payload_len = version_size - 1;
    }
    memcpy(version, &response[5], payload_len);
    version[payload_len] = '\0';
    return ESP_OK;
}

esp_err_t vc0706_set_image_size(vc0706_t *cam, vc0706_image_size_t size)
{
    uint8_t response[5] = {0};
    uint8_t args[] = {0x05, 0x04, 0x01, 0x00, 0x19, (uint8_t)size};
    return vc0706_run_command(cam, VC0706_CMD_WRITE_DATA, args, sizeof(args),
                              response, sizeof(response), true);
}

esp_err_t vc0706_get_image_size(vc0706_t *cam, vc0706_image_size_t *size)
{
    ESP_RETURN_ON_FALSE(size, ESP_ERR_INVALID_ARG, TAG, "size is NULL");

    uint8_t response[6] = {0};
    const uint8_t args[] = {0x04, 0x04, 0x01, 0x00, 0x19};
    ESP_RETURN_ON_ERROR(vc0706_run_command(cam, VC0706_CMD_READ_DATA, args, sizeof(args),
                                           response, sizeof(response), true),
                        TAG, "get image size failed");
    *size = (vc0706_image_size_t)response[5];
    return ESP_OK;
}

esp_err_t vc0706_take_picture(vc0706_t *cam)
{
    uint8_t response[5] = {0};
    const uint8_t args[] = {0x01, VC0706_STOP_CURRENT_FRAME};
    ESP_RETURN_ON_ERROR(vc0706_run_command(cam, VC0706_CMD_FBUF_CTRL, args, sizeof(args),
                                           response, sizeof(response), true),
                        TAG, "stop current frame failed");
    cam->frame_ptr = 0;
    return ESP_OK;
}

esp_err_t vc0706_resume_video(vc0706_t *cam)
{
    uint8_t response[5] = {0};
    const uint8_t args[] = {0x01, VC0706_RESUME_FRAME};
    return vc0706_run_command(cam, VC0706_CMD_FBUF_CTRL, args, sizeof(args),
                              response, sizeof(response), true);
}

esp_err_t vc0706_frame_length(vc0706_t *cam, uint32_t *length)
{
    ESP_RETURN_ON_FALSE(length, ESP_ERR_INVALID_ARG, TAG, "length is NULL");

    uint8_t response[9] = {0};
    const uint8_t args[] = {0x01, VC0706_READ_FBUF_CURRENT};
    ESP_RETURN_ON_ERROR(vc0706_run_command(cam, VC0706_CMD_GET_FBUF_LEN, args, sizeof(args),
                                           response, sizeof(response), true),
                        TAG, "get frame length failed");

    *length = ((uint32_t)response[5] << 24) |
              ((uint32_t)response[6] << 16) |
              ((uint32_t)response[7] << 8) |
              (uint32_t)response[8];
    return ESP_OK;
}

esp_err_t vc0706_read_picture(vc0706_t *cam, uint8_t *buffer, size_t length, size_t *bytes_read)
{
    ESP_RETURN_ON_FALSE(cam && buffer && length > 0, ESP_ERR_INVALID_ARG, TAG, "invalid picture read args");
    ESP_RETURN_ON_FALSE(length <= 255, ESP_ERR_INVALID_ARG, TAG, "READ_FBUF chunk must be <=255 bytes");

    if (bytes_read) {
        *bytes_read = 0;
    }

    uint32_t addr = cam->frame_ptr;
    uint32_t requested = (uint32_t)length;
    uint8_t args[] = {
        VC0706_READ_FBUF_DATA_LEN,
        VC0706_READ_FBUF_CURRENT,
        VC0706_READ_FBUF_MODE,
        (uint8_t)((addr >> 24) & 0xFF),
        (uint8_t)((addr >> 16) & 0xFF),
        (uint8_t)((addr >> 8) & 0xFF),
        (uint8_t)(addr & 0xFF),
        (uint8_t)((requested >> 24) & 0xFF),
        (uint8_t)((requested >> 16) & 0xFF),
        (uint8_t)((requested >> 8) & 0xFF),
        (uint8_t)(requested & 0xFF),
        (uint8_t)((cam->config.read_delay_ms >> 8) & 0xFF),
        (uint8_t)(cam->config.read_delay_ms & 0xFF),
    };

    uint8_t response[5] = {0};
    ESP_RETURN_ON_ERROR(vc0706_run_command(cam, VC0706_CMD_READ_FBUF, args, sizeof(args),
                                           response, sizeof(response), false),
                        TAG, "READ_FBUF command failed");

    ESP_RETURN_ON_ERROR(uart_read_exact(cam, buffer, length, cam->config.image_read_timeout_ms),
                        TAG, "read JPEG data failed");

    uint8_t second_ack[5] = {0};
    ESP_RETURN_ON_ERROR(uart_read_exact(cam, second_ack, sizeof(second_ack), cam->config.command_timeout_ms),
                        TAG, "read READ_FBUF finish ack failed");
    ESP_RETURN_ON_ERROR(vc0706_verify_response(cam, VC0706_CMD_READ_FBUF, second_ack, sizeof(second_ack)),
                        TAG, "READ_FBUF finish ack invalid");

    cam->frame_ptr += (uint32_t)length;
    if (bytes_read) {
        *bytes_read = length;
    }
    return ESP_OK;
}

const char *vc0706_image_size_name(vc0706_image_size_t size)
{
    switch (size) {
        case VC0706_IMAGE_SIZE_640X480:
            return "640x480";
        case VC0706_IMAGE_SIZE_320X240:
            return "320x240";
        case VC0706_IMAGE_SIZE_160X120:
            return "160x120";
        default:
            return "unknown";
    }
}
