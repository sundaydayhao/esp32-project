/**
 * @file mhz19.c
 * @brief MH-Z19B 驱动实现
 */
#include "mhz19.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define MHZ19_FRAME_LEN     9
#define MHZ19_START_BYTE    0xFF
#define MHZ19_SENSOR_NUM    0x01
#define MHZ19_READ_CO2      0x86
#define MHZ19_ABC_ON        0x79
#define MHZ19_CAL_ZERO      0x87
#define MHZ19_CAL_SPAN      0x88
#define MHZ19_SET_RANGE     0x99
#define MHZ19_UART_TIMEOUT  pdMS_TO_TICKS(500)

static uart_port_t s_uart;

/* ---- 校验和 ---- */
static uint8_t checksum(const uint8_t *buf)
{
    uint8_t sum = 0;
    for (int i = 1; i < 8; i++) sum += buf[i];
    return (~sum) + 1;
}

static esp_err_t send_cmd(const uint8_t *cmd, uint8_t *resp)
{
    /* 清空接收缓冲区 */
    uart_flush_input(s_uart);

    int written = uart_write_bytes(s_uart, (const char *)cmd, MHZ19_FRAME_LEN);
    if (written != MHZ19_FRAME_LEN) {
        ESP_LOGE(TAG_MHZ19, "发送失败");
        return ESP_FAIL;
    }

    if (resp == NULL) return ESP_OK;

    int len = uart_read_bytes(s_uart, resp, MHZ19_FRAME_LEN, MHZ19_UART_TIMEOUT);
    if (len != MHZ19_FRAME_LEN) {
        ESP_LOGE(TAG_MHZ19, "响应超时或长度错误 (%d)", len);
        return ESP_ERR_TIMEOUT;
    }

    if (resp[0] != MHZ19_START_BYTE || resp[1] != cmd[2]) {
        ESP_LOGE(TAG_MHZ19, "响应帧头错误");
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (resp[8] != checksum(resp)) {
        ESP_LOGE(TAG_MHZ19, "校验和错误");
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}

esp_err_t mhz19_init(uart_port_t uart_port, int tx_pin, int rx_pin)
{
    s_uart = uart_port;

    uart_config_t cfg = {
        .baud_rate  = MHZ19_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(uart_port, 256, 0, 0, NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    ret = uart_param_config(uart_port, &cfg);
    if (ret != ESP_OK) return ret;

    ret = uart_set_pin(uart_port, tx_pin, rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) return ret;

    /* 传感器上电预热 */
    ESP_LOGI(TAG_MHZ19, "等待传感器预热 (3s)...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* 工业场景关闭 ABC */
    mhz19_set_abc(false);

    ESP_LOGI(TAG_MHZ19, "初始化成功");
    return ESP_OK;
}

esp_err_t mhz19_read(mhz19_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;

    uint8_t cmd[9] = { 0xFF, 0x01, MHZ19_READ_CO2, 0, 0, 0, 0, 0, 0 };
    cmd[8] = checksum(cmd);

    uint8_t resp[9];
    esp_err_t ret = send_cmd(cmd, resp);
    if (ret != ESP_OK) {
        data->valid = false;
        return ret;
    }

    data->co2_ppm    = ((int32_t)resp[2] << 8) | resp[3];
    data->temperature = (int8_t)resp[4] - 40;
    data->valid       = true;
    data->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    /* 合理性检查 */
    if (data->co2_ppm < 0 || data->co2_ppm > 10000) {
        ESP_LOGW(TAG_MHZ19, "CO₂ 值异常: %ld ppm", data->co2_ppm);
        data->valid = false;
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGD(TAG_MHZ19, "CO₂=%ld ppm  T=%d°C", data->co2_ppm, data->temperature);
    return ESP_OK;
}

esp_err_t mhz19_set_abc(bool enable)
{
    uint8_t cmd[9] = { 0xFF, 0x01, MHZ19_ABC_ON,
                       (uint8_t)(enable ? 0xA0 : 0x00),
                       0, 0, 0, 0, 0 };
    cmd[8] = checksum(cmd);
    ESP_LOGI(TAG_MHZ19, "ABC %s", enable ? "开启" : "关闭");
    return send_cmd(cmd, NULL);
}

esp_err_t mhz19_calibrate_zero(void)
{
    uint8_t cmd[9] = { 0xFF, 0x01, MHZ19_CAL_ZERO, 0, 0, 0, 0, 0, 0 };
    cmd[8] = checksum(cmd);
    ESP_LOGI(TAG_MHZ19, "执行零点校准");
    return send_cmd(cmd, NULL);
}

esp_err_t mhz19_calibrate_span(uint16_t span_ppm)
{
    uint8_t cmd[9] = { 0xFF, 0x01, MHZ19_CAL_SPAN,
                       (uint8_t)(span_ppm >> 8), (uint8_t)(span_ppm & 0xFF),
                       0, 0, 0, 0 };
    cmd[8] = checksum(cmd);
    ESP_LOGI(TAG_MHZ19, "执行跨度校准 (%u ppm)", span_ppm);
    return send_cmd(cmd, NULL);
}

esp_err_t mhz19_set_range(uint16_t range)
{
    uint8_t cmd[9] = { 0xFF, 0x01, MHZ19_SET_RANGE,
                       0, 0, 0, 0,
                       (uint8_t)(range >> 8), (uint8_t)(range & 0xFF) };
    cmd[8] = checksum(cmd);
    ESP_LOGI(TAG_MHZ19, "设置量程 %u ppm", range);
    return send_cmd(cmd, NULL);
}