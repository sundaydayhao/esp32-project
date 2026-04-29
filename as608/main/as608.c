#include "as608.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "as608";

#define AS608_HEADER_HIGH       0xEFu
#define AS608_HEADER_LOW        0x01u

#define AS608_PID_COMMAND       0x01u
#define AS608_PID_DATA          0x02u
#define AS608_PID_ACK           0x07u
#define AS608_PID_END_DATA      0x08u

#define AS608_CMD_GET_IMAGE     0x01u
#define AS608_CMD_GEN_CHAR      0x02u
#define AS608_CMD_MATCH         0x03u
#define AS608_CMD_SEARCH        0x04u
#define AS608_CMD_REG_MODEL     0x05u
#define AS608_CMD_STORE_CHAR    0x06u
#define AS608_CMD_LOAD_CHAR     0x07u
#define AS608_CMD_DELETE_CHAR   0x0Cu
#define AS608_CMD_EMPTY         0x0Du
#define AS608_CMD_READ_PARAMS   0x0Fu
#define AS608_CMD_VERIFY_PWD    0x13u
#define AS608_CMD_HISPEED_SRCH  0x1Bu
#define AS608_CMD_TEMPLATE_NUM  0x1Du

#define AS608_PACKET_HEADER_LEN 9u
#define AS608_CHECKSUM_LEN      2u
#define AS608_MAX_PAYLOAD_LEN   256u
#define AS608_MAX_BODY_LEN      (AS608_MAX_PAYLOAD_LEN + AS608_CHECKSUM_LEN)
#define AS608_MAX_PACKET_LEN    (AS608_PACKET_HEADER_LEN + AS608_MAX_BODY_LEN)
#define AS608_MAX_COMMAND_LEN   32u

static uint16_t get_be16(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | buf[1];
}

static uint32_t get_be32(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) |
           buf[3];
}

static uint16_t packet_size_from_code(uint16_t code)
{
    switch (code) {
    case 0:
        return 32;
    case 1:
        return 64;
    case 2:
        return 128;
    case 3:
        return 256;
    default:
        return 0;
    }
}

static esp_err_t status_to_esp_err(uint8_t status)
{
    switch (status) {
    case AS608_STATUS_OK:
        return ESP_OK;
    case AS608_STATUS_NO_FINGER:
    case AS608_STATUS_NOT_FOUND:
        return ESP_ERR_NOT_FOUND;
    case AS608_STATUS_BAD_LOCATION:
    case AS608_STATUS_INVALID_REGISTER:
    case AS608_STATUS_REGISTER_CONFIG_ERROR:
    case AS608_STATUS_NOTEPAD_PAGE_ERROR:
        return ESP_ERR_INVALID_ARG;
    case AS608_STATUS_PACKET_ERROR:
    case AS608_STATUS_PACKET_RESPONSE_FAIL:
    case AS608_STATUS_COMM_PORT_ERROR:
        return ESP_ERR_INVALID_RESPONSE;
    case AS608_STATUS_WRONG_PASSWORD:
        return ESP_ERR_INVALID_STATE;
    default:
        return ESP_FAIL;
    }
}

static bool is_ready(const as608_t *dev)
{
    return dev != NULL && dev->initialized;
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static uint32_t remaining_ms(int64_t deadline_ms)
{
    int64_t remaining = deadline_ms - now_ms();
    if (remaining <= 0) {
        return 0;
    }
    return remaining > 1000 ? 1000 : (uint32_t)remaining;
}

static esp_err_t uart_read_exact(as608_t *dev, uint8_t *buf, size_t len,
                                 int64_t deadline_ms)
{
    size_t offset = 0;

    while (offset < len) {
        uint32_t wait_ms = remaining_ms(deadline_ms);
        if (wait_ms == 0) {
            return ESP_ERR_TIMEOUT;
        }

        int got = uart_read_bytes(dev->uart_num, buf + offset, len - offset,
                                  pdMS_TO_TICKS(wait_ms));
        if (got < 0) {
            return ESP_FAIL;
        }
        offset += (size_t)got;
    }

    return ESP_OK;
}

static esp_err_t uart_read_synced_header(as608_t *dev, uint8_t header[AS608_PACKET_HEADER_LEN],
                                         int64_t deadline_ms)
{
    uint8_t prev = 0;

    while (now_ms() < deadline_ms) {
        uint8_t byte = 0;
        esp_err_t ret = uart_read_exact(dev, &byte, 1, deadline_ms);
        if (ret != ESP_OK) {
            return ret;
        }

        if (prev == AS608_HEADER_HIGH && byte == AS608_HEADER_LOW) {
            header[0] = AS608_HEADER_HIGH;
            header[1] = AS608_HEADER_LOW;
            return uart_read_exact(dev, &header[2], AS608_PACKET_HEADER_LEN - 2,
                                   deadline_ms);
        }

        prev = byte;
    }

    return ESP_ERR_TIMEOUT;
}

static esp_err_t read_packet(as608_t *dev, uint8_t *pid, uint8_t *payload,
                             size_t payload_size, size_t *payload_len)
{
    uint8_t header[AS608_PACKET_HEADER_LEN];
    uint8_t body[AS608_MAX_BODY_LEN];
    int64_t deadline_ms = now_ms() + dev->rx_timeout_ms;

    esp_err_t ret = uart_read_synced_header(dev, header, deadline_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    uint16_t body_len = get_be16(&header[7]);
    if (body_len < AS608_CHECKSUM_LEN || body_len > sizeof(body)) {
        ESP_LOGW(TAG, "invalid packet length: %u", body_len);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ret = uart_read_exact(dev, body, body_len, deadline_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    uint16_t data_len = body_len - AS608_CHECKSUM_LEN;
    uint16_t checksum = header[6] + header[7] + header[8];
    for (uint16_t i = 0; i < data_len; ++i) {
        checksum += body[i];
    }

    uint16_t packet_checksum = get_be16(&body[data_len]);
    if (checksum != packet_checksum) {
        ESP_LOGW(TAG, "checksum mismatch: got 0x%04x, expected 0x%04x",
                 packet_checksum, checksum);
        return ESP_ERR_INVALID_CRC;
    }

    if (data_len > payload_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    *pid = header[6];
    *payload_len = data_len;
    if (data_len > 0) {
        memcpy(payload, body, data_len);
    }

    return ESP_OK;
}

static esp_err_t build_packet(as608_t *dev, uint8_t pid, const uint8_t *payload,
                              size_t payload_len, uint8_t *packet,
                              size_t packet_size, size_t *packet_len)
{
    if (payload_len > AS608_MAX_PAYLOAD_LEN ||
        packet_size < AS608_PACKET_HEADER_LEN + payload_len + AS608_CHECKSUM_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t length = payload_len + AS608_CHECKSUM_LEN;
    size_t offset = 0;

    packet[offset++] = AS608_HEADER_HIGH;
    packet[offset++] = AS608_HEADER_LOW;
    packet[offset++] = (uint8_t)(dev->address >> 24);
    packet[offset++] = (uint8_t)(dev->address >> 16);
    packet[offset++] = (uint8_t)(dev->address >> 8);
    packet[offset++] = (uint8_t)dev->address;
    packet[offset++] = pid;
    packet[offset++] = (uint8_t)(length >> 8);
    packet[offset++] = (uint8_t)length;

    memcpy(packet + offset, payload, payload_len);
    offset += payload_len;

    uint16_t checksum = pid + (length >> 8) + (length & 0xff);
    for (size_t i = 0; i < payload_len; ++i) {
        checksum += payload[i];
    }

    packet[offset++] = (uint8_t)(checksum >> 8);
    packet[offset++] = (uint8_t)checksum;

    *packet_len = offset;
    return ESP_OK;
}

static esp_err_t command(as608_t *dev, uint8_t instruction, const uint8_t *params,
                         size_t params_len, uint8_t *response,
                         size_t response_size, size_t *response_len)
{
    if (!is_ready(dev)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (params_len > AS608_MAX_COMMAND_LEN - 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t payload[AS608_MAX_COMMAND_LEN];
    uint8_t packet[AS608_MAX_PACKET_LEN];
    uint8_t ack_payload[AS608_MAX_PAYLOAD_LEN];
    size_t packet_len = 0;
    size_t ack_len = 0;
    uint8_t ack_pid = 0;

    payload[0] = instruction;
    if (params_len > 0) {
        memcpy(&payload[1], params, params_len);
    }

    esp_err_t ret = build_packet(dev, AS608_PID_COMMAND, payload, params_len + 1,
                                 packet, sizeof(packet), &packet_len);
    if (ret != ESP_OK) {
        return ret;
    }

    SemaphoreHandle_t lock = (SemaphoreHandle_t)dev->lock;
    if (xSemaphoreTake(lock, pdMS_TO_TICKS(dev->rx_timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    dev->last_status = 0xff;
    uart_flush_input(dev->uart_num);

    int written = uart_write_bytes(dev->uart_num, (const char *)packet, packet_len);
    if (written != (int)packet_len) {
        xSemaphoreGive(lock);
        return ESP_FAIL;
    }

    ret = uart_wait_tx_done(dev->uart_num, pdMS_TO_TICKS(dev->rx_timeout_ms));
    if (ret == ESP_OK) {
        ret = read_packet(dev, &ack_pid, ack_payload, sizeof(ack_payload), &ack_len);
    }

    xSemaphoreGive(lock);

    if (ret != ESP_OK) {
        return ret;
    }
    if (ack_pid != AS608_PID_ACK || ack_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    dev->last_status = ack_payload[0];

    size_t data_len = ack_len - 1;
    if (response_len != NULL) {
        *response_len = data_len;
    }
    if (response != NULL && data_len > 0) {
        if (data_len > response_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(response, &ack_payload[1], data_len);
    }

    ESP_LOGD(TAG, "cmd 0x%02x -> status 0x%02x (%s)", instruction,
             dev->last_status, as608_status_to_name(dev->last_status));
    return ESP_OK;
}

static esp_err_t command_expect_ok(as608_t *dev, uint8_t instruction,
                                   const uint8_t *params, size_t params_len,
                                   uint8_t *response, size_t response_size,
                                   size_t *response_len)
{
    esp_err_t ret = command(dev, instruction, params, params_len,
                            response, response_size, response_len);
    if (ret != ESP_OK) {
        return ret;
    }

    return status_to_esp_err(dev->last_status);
}

esp_err_t as608_init(as608_t *dev, const as608_config_t *config)
{
    if (dev == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->uart_num < UART_NUM_0 || config->uart_num >= UART_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(dev, 0, sizeof(*dev));

    dev->uart_num = config->uart_num;
    dev->address = config->address != 0 ? config->address : AS608_DEFAULT_ADDRESS;
    dev->password = config->password;
    dev->rx_timeout_ms = config->rx_timeout_ms != 0 ?
                         config->rx_timeout_ms : AS608_DEFAULT_TIMEOUT_MS;
    dev->last_status = 0xff;

    size_t rx_buffer_size = config->rx_buffer_size != 0 ?
                            config->rx_buffer_size : 1024;

    dev->lock = xSemaphoreCreateMutex();
    if (dev->lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uart_config_t uart_config = {
        .baud_rate = config->baud_rate != 0 ?
                     (int)config->baud_rate : (int)AS608_DEFAULT_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(dev->uart_num, rx_buffer_size, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        vSemaphoreDelete((SemaphoreHandle_t)dev->lock);
        dev->lock = NULL;
        return ret;
    }

    ret = uart_param_config(dev->uart_num, &uart_config);
    if (ret == ESP_OK) {
        ret = uart_set_pin(dev->uart_num, config->tx_gpio_num, config->rx_gpio_num,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }

    if (ret != ESP_OK) {
        uart_driver_delete(dev->uart_num);
        vSemaphoreDelete((SemaphoreHandle_t)dev->lock);
        dev->lock = NULL;
        return ret;
    }

    dev->initialized = true;

    uint32_t delay_ms = config->power_on_delay_ms != 0 ?
                        config->power_on_delay_ms : AS608_DEFAULT_POWER_DELAY_MS;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    ret = as608_verify_password(dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "verify password failed: %s, module status 0x%02x (%s)",
                 esp_err_to_name(ret), dev->last_status,
                 as608_status_to_name(dev->last_status));
        as608_deinit(dev);
        return ret;
    }

    ESP_LOGI(TAG, "AS608 initialized on UART%d", dev->uart_num);
    return ESP_OK;
}

esp_err_t as608_deinit(as608_t *dev)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!dev->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uart_driver_delete(dev->uart_num);
    if (dev->lock != NULL) {
        vSemaphoreDelete((SemaphoreHandle_t)dev->lock);
    }

    memset(dev, 0, sizeof(*dev));
    return ESP_OK;
}

esp_err_t as608_verify_password(as608_t *dev)
{
    if (!is_ready(dev)) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t params[4] = {
        (uint8_t)(dev->password >> 24),
        (uint8_t)(dev->password >> 16),
        (uint8_t)(dev->password >> 8),
        (uint8_t)dev->password,
    };

    return command_expect_ok(dev, AS608_CMD_VERIFY_PWD, params, sizeof(params),
                             NULL, 0, NULL);
}

esp_err_t as608_read_sys_params(as608_t *dev, as608_sys_params_t *params)
{
    if (!is_ready(dev) || params == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t response[16];
    size_t response_len = 0;
    esp_err_t ret = command_expect_ok(dev, AS608_CMD_READ_PARAMS, NULL, 0,
                                      response, sizeof(response), &response_len);
    if (ret != ESP_OK) {
        return ret;
    }
    if (response_len < sizeof(response)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    params->status_register = get_be16(&response[0]);
    params->system_id = get_be16(&response[2]);
    params->library_size = get_be16(&response[4]);
    params->security_level = get_be16(&response[6]);
    params->address = get_be32(&response[8]);
    params->packet_size_code = get_be16(&response[12]);
    params->packet_size = packet_size_from_code(params->packet_size_code);
    params->baud_rate_code = get_be16(&response[14]);
    params->baud_rate = (uint32_t)params->baud_rate_code * 9600u;

    return ESP_OK;
}

esp_err_t as608_read_template_count(as608_t *dev, uint16_t *count)
{
    if (!is_ready(dev) || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t response[2];
    size_t response_len = 0;
    esp_err_t ret = command_expect_ok(dev, AS608_CMD_TEMPLATE_NUM, NULL, 0,
                                      response, sizeof(response), &response_len);
    if (ret != ESP_OK) {
        return ret;
    }
    if (response_len < sizeof(response)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *count = get_be16(response);
    return ESP_OK;
}

esp_err_t as608_get_image(as608_t *dev)
{
    return command_expect_ok(dev, AS608_CMD_GET_IMAGE, NULL, 0, NULL, 0, NULL);
}

esp_err_t as608_gen_char(as608_t *dev, uint8_t buffer_id)
{
    if (buffer_id != AS608_CHAR_BUFFER_1 && buffer_id != AS608_CHAR_BUFFER_2) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t params[1] = { buffer_id };
    return command_expect_ok(dev, AS608_CMD_GEN_CHAR, params, sizeof(params),
                             NULL, 0, NULL);
}

esp_err_t as608_match(as608_t *dev, uint16_t *score)
{
    if (score == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t response[2];
    size_t response_len = 0;
    esp_err_t ret = command_expect_ok(dev, AS608_CMD_MATCH, NULL, 0,
                                      response, sizeof(response), &response_len);
    if (ret != ESP_OK) {
        return ret;
    }
    if (response_len < sizeof(response)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *score = get_be16(response);
    return ESP_OK;
}

esp_err_t as608_reg_model(as608_t *dev)
{
    return command_expect_ok(dev, AS608_CMD_REG_MODEL, NULL, 0, NULL, 0, NULL);
}

esp_err_t as608_store_char(as608_t *dev, uint8_t buffer_id, uint16_t page_id)
{
    if (buffer_id != AS608_CHAR_BUFFER_1 && buffer_id != AS608_CHAR_BUFFER_2) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t params[3] = {
        buffer_id,
        (uint8_t)(page_id >> 8),
        (uint8_t)page_id,
    };

    return command_expect_ok(dev, AS608_CMD_STORE_CHAR, params, sizeof(params),
                             NULL, 0, NULL);
}

esp_err_t as608_load_char(as608_t *dev, uint8_t buffer_id, uint16_t page_id)
{
    if (buffer_id != AS608_CHAR_BUFFER_1 && buffer_id != AS608_CHAR_BUFFER_2) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t params[3] = {
        buffer_id,
        (uint8_t)(page_id >> 8),
        (uint8_t)page_id,
    };

    return command_expect_ok(dev, AS608_CMD_LOAD_CHAR, params, sizeof(params),
                             NULL, 0, NULL);
}

esp_err_t as608_delete_char(as608_t *dev, uint16_t page_id, uint16_t count)
{
    if (count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t params[4] = {
        (uint8_t)(page_id >> 8),
        (uint8_t)page_id,
        (uint8_t)(count >> 8),
        (uint8_t)count,
    };

    return command_expect_ok(dev, AS608_CMD_DELETE_CHAR, params, sizeof(params),
                             NULL, 0, NULL);
}

esp_err_t as608_empty_library(as608_t *dev)
{
    return command_expect_ok(dev, AS608_CMD_EMPTY, NULL, 0, NULL, 0, NULL);
}

static esp_err_t search_impl(as608_t *dev, uint8_t instruction, uint8_t buffer_id,
                             uint16_t start_page, uint16_t page_count,
                             as608_search_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (buffer_id != AS608_CHAR_BUFFER_1 && buffer_id != AS608_CHAR_BUFFER_2) {
        return ESP_ERR_INVALID_ARG;
    }
    if (page_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t params[5] = {
        buffer_id,
        (uint8_t)(start_page >> 8),
        (uint8_t)start_page,
        (uint8_t)(page_count >> 8),
        (uint8_t)page_count,
    };
    uint8_t response[4];
    size_t response_len = 0;

    esp_err_t ret = command_expect_ok(dev, instruction, params, sizeof(params),
                                      response, sizeof(response), &response_len);
    if (ret != ESP_OK) {
        return ret;
    }
    if (response_len < sizeof(response)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    result->page_id = get_be16(&response[0]);
    result->score = get_be16(&response[2]);
    return ESP_OK;
}

esp_err_t as608_search(as608_t *dev, uint8_t buffer_id, uint16_t start_page,
                       uint16_t page_count, as608_search_result_t *result)
{
    return search_impl(dev, AS608_CMD_SEARCH, buffer_id, start_page, page_count,
                       result);
}

esp_err_t as608_high_speed_search(as608_t *dev, uint8_t buffer_id,
                                  uint16_t start_page, uint16_t page_count,
                                  as608_search_result_t *result)
{
    return search_impl(dev, AS608_CMD_HISPEED_SRCH, buffer_id, start_page,
                       page_count, result);
}

esp_err_t as608_wait_for_finger(as608_t *dev, uint32_t timeout_ms)
{
    int64_t deadline_ms = timeout_ms == 0 ? INT64_MAX : now_ms() + timeout_ms;

    while (now_ms() < deadline_ms) {
        esp_err_t ret = as608_get_image(dev);
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        if (ret != ESP_ERR_NOT_FOUND || as608_get_last_status(dev) != AS608_STATUS_NO_FINGER) {
            return ret;
        }

        vTaskDelay(pdMS_TO_TICKS(150));
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t as608_wait_for_no_finger(as608_t *dev, uint32_t timeout_ms)
{
    int64_t deadline_ms = timeout_ms == 0 ? INT64_MAX : now_ms() + timeout_ms;

    while (now_ms() < deadline_ms) {
        esp_err_t ret = as608_get_image(dev);
        if (ret == ESP_ERR_NOT_FOUND && as608_get_last_status(dev) == AS608_STATUS_NO_FINGER) {
            return ESP_OK;
        }
        if (ret != ESP_OK) {
            return ret;
        }

        vTaskDelay(pdMS_TO_TICKS(150));
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t as608_enroll(as608_t *dev, uint16_t page_id,
                       uint32_t finger_timeout_ms, uint32_t remove_timeout_ms)
{
    ESP_LOGI(TAG, "place finger for step 1");
    esp_err_t ret = as608_wait_for_finger(dev, finger_timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = as608_gen_char(dev, AS608_CHAR_BUFFER_1);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "remove finger");
    ret = as608_wait_for_no_finger(dev, remove_timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "place the same finger for step 2");
    ret = as608_wait_for_finger(dev, finger_timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = as608_gen_char(dev, AS608_CHAR_BUFFER_2);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = as608_reg_model(dev);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = as608_store_char(dev, AS608_CHAR_BUFFER_1, page_id);
    if (ret != ESP_OK) {
        return ret;
    }

    (void)as608_wait_for_no_finger(dev, 3000);
    return ESP_OK;
}

esp_err_t as608_verify(as608_t *dev, uint16_t library_size,
                       uint32_t finger_timeout_ms, as608_search_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = as608_wait_for_finger(dev, finger_timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = as608_gen_char(dev, AS608_CHAR_BUFFER_1);
    if (ret != ESP_OK) {
        return ret;
    }

    uint16_t page_count = library_size == 0 ? AS608_DEFAULT_LIBRARY_SIZE : library_size;
    return as608_high_speed_search(dev, AS608_CHAR_BUFFER_1, 0, page_count, result);
}

uint8_t as608_get_last_status(const as608_t *dev)
{
    return dev == NULL ? 0xff : dev->last_status;
}

const char *as608_status_to_name(uint8_t status)
{
    switch (status) {
    case AS608_STATUS_OK:
        return "ok";
    case AS608_STATUS_PACKET_ERROR:
        return "packet receive error";
    case AS608_STATUS_NO_FINGER:
        return "no finger";
    case AS608_STATUS_IMAGE_FAIL:
        return "image capture fail";
    case AS608_STATUS_IMAGE_TOO_BLURRY:
        return "image too blurry";
    case AS608_STATUS_IMAGE_TOO_MESSY:
        return "image too messy";
    case AS608_STATUS_FEATURE_FAIL:
        return "feature generation fail";
    case AS608_STATUS_NO_MATCH:
        return "two fingers do not match";
    case AS608_STATUS_NOT_FOUND:
        return "not found";
    case AS608_STATUS_MODEL_FAIL:
        return "model generation fail";
    case AS608_STATUS_BAD_LOCATION:
        return "bad library location";
    case AS608_STATUS_LOAD_FAIL:
        return "template load fail";
    case AS608_STATUS_UPLOAD_FEATURE_FAIL:
        return "feature upload fail";
    case AS608_STATUS_PACKET_RESPONSE_FAIL:
        return "packet response fail";
    case AS608_STATUS_UPLOAD_IMAGE_FAIL:
        return "image upload fail";
    case AS608_STATUS_DELETE_FAIL:
        return "delete fail";
    case AS608_STATUS_CLEAR_FAIL:
        return "clear library fail";
    case AS608_STATUS_WRONG_PASSWORD:
        return "wrong password";
    case AS608_STATUS_INVALID_IMAGE:
        return "invalid image";
    case AS608_STATUS_FLASH_ERROR:
        return "flash error";
    case AS608_STATUS_INVALID_REGISTER:
        return "invalid register";
    case AS608_STATUS_REGISTER_CONFIG_ERROR:
        return "register config error";
    case AS608_STATUS_NOTEPAD_PAGE_ERROR:
        return "notepad page error";
    case AS608_STATUS_COMM_PORT_ERROR:
        return "communication port error";
    default:
        return "unknown";
    }
}
