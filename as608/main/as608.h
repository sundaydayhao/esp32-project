#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AS608_DEFAULT_ADDRESS        0xFFFFFFFFu
#define AS608_DEFAULT_PASSWORD       0x00000000u
#define AS608_DEFAULT_BAUD_RATE      57600u
#define AS608_DEFAULT_TIMEOUT_MS     1200u
#define AS608_DEFAULT_POWER_DELAY_MS 500u
#define AS608_DEFAULT_LIBRARY_SIZE   127u

#define AS608_CHAR_BUFFER_1          0x01u
#define AS608_CHAR_BUFFER_2          0x02u

typedef enum {
    AS608_STATUS_OK = 0x00,
    AS608_STATUS_PACKET_ERROR = 0x01,
    AS608_STATUS_NO_FINGER = 0x02,
    AS608_STATUS_IMAGE_FAIL = 0x03,
    AS608_STATUS_IMAGE_TOO_BLURRY = 0x04,
    AS608_STATUS_IMAGE_TOO_MESSY = 0x06,
    AS608_STATUS_FEATURE_FAIL = 0x07,
    AS608_STATUS_NO_MATCH = 0x08,
    AS608_STATUS_NOT_FOUND = 0x09,
    AS608_STATUS_MODEL_FAIL = 0x0A,
    AS608_STATUS_BAD_LOCATION = 0x0B,
    AS608_STATUS_LOAD_FAIL = 0x0C,
    AS608_STATUS_UPLOAD_FEATURE_FAIL = 0x0D,
    AS608_STATUS_PACKET_RESPONSE_FAIL = 0x0E,
    AS608_STATUS_UPLOAD_IMAGE_FAIL = 0x0F,
    AS608_STATUS_DELETE_FAIL = 0x10,
    AS608_STATUS_CLEAR_FAIL = 0x11,
    AS608_STATUS_WRONG_PASSWORD = 0x13,
    AS608_STATUS_INVALID_IMAGE = 0x15,
    AS608_STATUS_FLASH_ERROR = 0x18,
    AS608_STATUS_INVALID_REGISTER = 0x1A,
    AS608_STATUS_REGISTER_CONFIG_ERROR = 0x1B,
    AS608_STATUS_NOTEPAD_PAGE_ERROR = 0x1C,
    AS608_STATUS_COMM_PORT_ERROR = 0x1D,
} as608_status_t;

typedef struct {
    uart_port_t uart_num;
    int tx_gpio_num;
    int rx_gpio_num;
    uint32_t baud_rate;
    uint32_t address;
    uint32_t password;
    uint32_t rx_timeout_ms;
    size_t rx_buffer_size;
    uint32_t power_on_delay_ms;
} as608_config_t;

typedef struct {
    uart_port_t uart_num;
    uint32_t address;
    uint32_t password;
    uint32_t rx_timeout_ms;
    void *lock;
    bool initialized;
    uint8_t last_status;
} as608_t;

typedef struct {
    uint16_t status_register;
    uint16_t system_id;
    uint16_t library_size;
    uint16_t security_level;
    uint32_t address;
    uint16_t packet_size_code;
    uint16_t packet_size;
    uint16_t baud_rate_code;
    uint32_t baud_rate;
} as608_sys_params_t;

typedef struct {
    uint16_t page_id;
    uint16_t score;
} as608_search_result_t;

esp_err_t as608_init(as608_t *dev, const as608_config_t *config);
esp_err_t as608_deinit(as608_t *dev);

esp_err_t as608_verify_password(as608_t *dev);
esp_err_t as608_read_sys_params(as608_t *dev, as608_sys_params_t *params);
esp_err_t as608_read_template_count(as608_t *dev, uint16_t *count);

esp_err_t as608_get_image(as608_t *dev);
esp_err_t as608_gen_char(as608_t *dev, uint8_t buffer_id);
esp_err_t as608_match(as608_t *dev, uint16_t *score);
esp_err_t as608_reg_model(as608_t *dev);
esp_err_t as608_store_char(as608_t *dev, uint8_t buffer_id, uint16_t page_id);
esp_err_t as608_load_char(as608_t *dev, uint8_t buffer_id, uint16_t page_id);
esp_err_t as608_delete_char(as608_t *dev, uint16_t page_id, uint16_t count);
esp_err_t as608_empty_library(as608_t *dev);
esp_err_t as608_search(as608_t *dev, uint8_t buffer_id, uint16_t start_page,
                       uint16_t page_count, as608_search_result_t *result);
esp_err_t as608_high_speed_search(as608_t *dev, uint8_t buffer_id,
                                  uint16_t start_page, uint16_t page_count,
                                  as608_search_result_t *result);

esp_err_t as608_wait_for_finger(as608_t *dev, uint32_t timeout_ms);
esp_err_t as608_wait_for_no_finger(as608_t *dev, uint32_t timeout_ms);
esp_err_t as608_enroll(as608_t *dev, uint16_t page_id,
                       uint32_t finger_timeout_ms, uint32_t remove_timeout_ms);
esp_err_t as608_verify(as608_t *dev, uint16_t library_size,
                       uint32_t finger_timeout_ms, as608_search_result_t *result);

uint8_t as608_get_last_status(const as608_t *dev);
const char *as608_status_to_name(uint8_t status);

#ifdef __cplusplus
}
#endif
