/**
 * @file sht31.c
 * @brief SHT31 驱动实现
 */
#include "sht31.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* ---- SHT31 命令字 ---- */
#define CMD_MEAS_HIGHREP_STRETCH    0x2C06
#define CMD_MEAS_MEDREP_STRETCH     0x2C0D
#define CMD_MEAS_LOWREP_STRETCH     0x2C10
#define CMD_MEAS_HIGHREP            0x2400
#define CMD_MEAS_MEDREP             0x240B
#define CMD_MEAS_LOWREP             0x2416
#define CMD_READ_STATUS             0xF32D
#define CMD_CLEAR_STATUS            0x3041
#define CMD_SOFT_RESET              0x30A2
#define CMD_HEATER_ENABLE           0x306D
#define CMD_HEATER_DISABLE          0x3066

/* ---- 测量等待时间 (ms) ---- */
#define MEAS_WAIT_HIGH_MS   16
#define MEAS_WAIT_MED_MS    7
#define MEAS_WAIT_LOW_MS    5

static i2c_port_t s_port;
static uint8_t    s_addr;

/* ---- CRC-8 (多项式 0x31, 初值 0xFF) ---- */
static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc;
}

static esp_err_t send_cmd(uint16_t cmd)
{
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    return i2c_master_write_to_device(s_port, s_addr, buf, 2,
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

esp_err_t sht31_init(i2c_port_t i2c_port, uint8_t i2c_addr)
{
    s_port = i2c_port;
    s_addr = i2c_addr;

    esp_err_t ret = sht31_soft_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SHT31, "软复位失败: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    ret = send_cmd(CMD_CLEAR_STATUS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SHT31, "清除状态寄存器失败");
        return ret;
    }

#if SHT31_HEATER_EN
    sht31_heater_ctrl(true);
#endif

    ESP_LOGI(TAG_SHT31, "初始化成功 (addr=0x%02X)", s_addr);
    return ESP_OK;
}

esp_err_t sht31_read(sht31_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;

    /* 发送单次高精度测量命令 */
    esp_err_t ret = send_cmd(CMD_MEAS_HIGHREP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SHT31, "发送测量命令失败");
        data->valid = false;
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(MEAS_WAIT_HIGH_MS));

    /* 读取 6 字节: T_MSB T_LSB T_CRC H_MSB H_LSB H_CRC */
    uint8_t buf[6];
    ret = i2c_master_read_from_device(s_port, s_addr, buf, sizeof(buf),
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SHT31, "读取数据失败");
        data->valid = false;
        return ret;
    }

    /* CRC 校验 */
    if (crc8(buf, 2) != buf[2]) {
        ESP_LOGE(TAG_SHT31, "温度 CRC 校验失败");
        data->valid = false;
        return ESP_ERR_INVALID_CRC;
    }
    if (crc8(buf + 3, 2) != buf[5]) {
        ESP_LOGE(TAG_SHT31, "湿度 CRC 校验失败");
        data->valid = false;
        return ESP_ERR_INVALID_CRC;
    }

    /* 转换公式 (SHT31 数据手册) */
    uint16_t raw_t = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t raw_h = ((uint16_t)buf[3] << 8) | buf[4];

    data->temperature = -45.0f + 175.0f * ((float)raw_t / 65535.0f);
    data->humidity    = 100.0f * ((float)raw_h / 65535.0f);
    data->valid       = true;
    data->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    /* 限幅保护 */
    if (data->humidity > 100.0f) data->humidity = 100.0f;
    if (data->humidity < 0.0f)   data->humidity = 0.0f;

    ESP_LOGD(TAG_SHT31, "T=%.2f°C  RH=%.2f%%", data->temperature, data->humidity);
    return ESP_OK;
}

esp_err_t sht31_soft_reset(void)
{
    return send_cmd(CMD_SOFT_RESET);
}

esp_err_t sht31_heater_ctrl(bool enable)
{
    return send_cmd(enable ? CMD_HEATER_ENABLE : CMD_HEATER_DISABLE);
}

esp_err_t sht31_read_status(uint16_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = send_cmd(CMD_READ_STATUS);
    if (ret != ESP_OK) return ret;

    uint8_t buf[3];
    ret = i2c_master_read_from_device(s_port, s_addr, buf, sizeof(buf),
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (ret != ESP_OK) return ret;

    if (crc8(buf, 2) != buf[2]) return ESP_ERR_INVALID_CRC;

    *status = ((uint16_t)buf[0] << 8) | buf[1];
    return ESP_OK;
}
