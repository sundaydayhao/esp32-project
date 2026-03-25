/**
 * @file bme280.c
 * @brief BME280 驱动实现（含完整补偿算法）
 */
#include "bme280.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

/* ---- 寄存器地址 ---- */
#define REG_CHIP_ID         0xD0
#define REG_RESET           0xE0
#define REG_CTRL_HUM        0xF2
#define REG_STATUS          0xF3
#define REG_CTRL_MEAS       0xF4
#define REG_CONFIG          0xF5
#define REG_PRESS_MSB       0xF7
#define REG_CALIB_00        0x88
#define REG_CALIB_26        0xE1

#define BME280_CHIP_ID      0x60
#define BME280_RESET_VAL    0xB6

/* ---- 补偿参数 ---- */
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4, dig_H5;
    int8_t   dig_H6;
} bme280_calib_t;

static i2c_port_t    s_port;
static uint8_t       s_addr;
static bme280_calib_t s_calib;
static int32_t       s_t_fine;

/* ---- I2C 辅助 ---- */
static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_write_read_device(s_port, s_addr, &reg, 1, buf, len,
                                        pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(s_port, s_addr, buf, 2,
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

/* ---- 读取补偿参数 ---- */
static esp_err_t load_calibration(void)
{
    uint8_t c[26];
    esp_err_t ret = reg_read(REG_CALIB_00, c, 26);
    if (ret != ESP_OK) return ret;

    s_calib.dig_T1 = (uint16_t)(c[1]  << 8 | c[0]);
    s_calib.dig_T2 = (int16_t) (c[3]  << 8 | c[2]);
    s_calib.dig_T3 = (int16_t) (c[5]  << 8 | c[4]);
    s_calib.dig_P1 = (uint16_t)(c[7]  << 8 | c[6]);
    s_calib.dig_P2 = (int16_t) (c[9]  << 8 | c[8]);
    s_calib.dig_P3 = (int16_t) (c[11] << 8 | c[10]);
    s_calib.dig_P4 = (int16_t) (c[13] << 8 | c[12]);
    s_calib.dig_P5 = (int16_t) (c[15] << 8 | c[14]);
    s_calib.dig_P6 = (int16_t) (c[17] << 8 | c[16]);
    s_calib.dig_P7 = (int16_t) (c[19] << 8 | c[18]);
    s_calib.dig_P8 = (int16_t) (c[21] << 8 | c[20]);
    s_calib.dig_P9 = (int16_t) (c[23] << 8 | c[22]);
    s_calib.dig_H1 = c[25];

    uint8_t h[7];
    ret = reg_read(REG_CALIB_26, h, 7);
    if (ret != ESP_OK) return ret;

    s_calib.dig_H2 = (int16_t)(h[1] << 8 | h[0]);
    s_calib.dig_H3 = h[2];
    s_calib.dig_H4 = (int16_t)((h[3] << 4) | (h[4] & 0x0F));
    s_calib.dig_H5 = (int16_t)((h[5] << 4) | (h[4] >> 4));
    s_calib.dig_H6 = (int8_t)h[6];
    return ESP_OK;
}

/* ---- 补偿算法（来自 BME280 数据手册 4.2.3） ---- */
static float compensate_temperature(int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)s_calib.dig_T1 << 1))) *
                    ((int32_t)s_calib.dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)s_calib.dig_T1)) *
                      ((adc_T >> 4) - ((int32_t)s_calib.dig_T1))) >> 12) *
                    ((int32_t)s_calib.dig_T3)) >> 14;
    s_t_fine = var1 + var2;
    return (float)((s_t_fine * 5 + 128) >> 8) / 100.0f;
}

static float compensate_pressure(int32_t adc_P)
{
    int64_t var1 = ((int64_t)s_t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)s_calib.dig_P6;
    var2 += (var1 * (int64_t)s_calib.dig_P5) << 17;
    var2 += ((int64_t)s_calib.dig_P4) << 35;
    var1  = ((var1 * var1 * (int64_t)s_calib.dig_P3) >> 8) +
            ((var1 * (int64_t)s_calib.dig_P2) << 12);
    var1  = (((int64_t)1 << 47) + var1) * ((int64_t)s_calib.dig_P1) >> 33;
    if (var1 == 0) return 0.0f;
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)s_calib.dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)s_calib.dig_P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)s_calib.dig_P7 << 4);
    return (float)p / 25600.0f;
}

static float compensate_humidity(int32_t adc_H)
{
    int32_t v = s_t_fine - 76800;
    v = (((((adc_H << 14) - ((int32_t)s_calib.dig_H4 << 20) -
            ((int32_t)s_calib.dig_H5 * v)) + 16384) >> 15) *
         (((((((v * (int32_t)s_calib.dig_H6) >> 10) *
              (((v * (int32_t)s_calib.dig_H3) >> 11) + 32768)) >> 10) +
            2097152) * (int32_t)s_calib.dig_H2 + 8192) >> 14));
    v -= (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)s_calib.dig_H1) >> 4);
    v  = (v < 0) ? 0 : v;
    v  = (v > 419430400) ? 419430400 : v;
    return (float)(v >> 12) / 1024.0f;
}

/* ================================================================ */

esp_err_t bme280_init(i2c_port_t i2c_port, uint8_t i2c_addr)
{
    s_port = i2c_port;
    s_addr = i2c_addr;

    uint8_t id;
    esp_err_t ret = bme280_chip_id(&id);
    if (ret != ESP_OK || id != BME280_CHIP_ID) {
        ESP_LOGE(TAG_BME280, "芯片 ID 错误: 0x%02X (期望 0x60)", id);
        return ESP_ERR_NOT_FOUND;
    }

    ret = bme280_soft_reset();
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10));

    ret = load_calibration();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_BME280, "读取补偿参数失败");
        return ret;
    }

    /* 湿度过采样 ×1 */
    ret = reg_write(REG_CTRL_HUM, 0x01);
    if (ret != ESP_OK) return ret;

    /* 配置寄存器: t_sb=0.5ms, filter=4, spi3w=off */
    ret = reg_write(REG_CONFIG, 0x10);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG_BME280, "初始化成功 (addr=0x%02X)", s_addr);
    return ESP_OK;
}

esp_err_t bme280_read(bme280_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;

    /* 强制模式: T×2, P×4, H×1 */
    esp_err_t ret = reg_write(REG_CTRL_MEAS, 0x57);
    if (ret != ESP_OK) { data->valid = false; return ret; }

    /* 等待测量完成 (status.measuring = 0) */
    uint8_t status;
    int timeout = 100;
    do {
        vTaskDelay(pdMS_TO_TICKS(5));
        ret = reg_read(REG_STATUS, &status, 1);
        if (ret != ESP_OK) { data->valid = false; return ret; }
    } while ((status & 0x08) && --timeout > 0);

    if (timeout == 0) {
        ESP_LOGE(TAG_BME280, "测量超时");
        data->valid = false;
        return ESP_ERR_TIMEOUT;
    }

    /* 读取 8 字节原始数据 */
    uint8_t raw[8];
    ret = reg_read(REG_PRESS_MSB, raw, 8);
    if (ret != ESP_OK) { data->valid = false; return ret; }

    int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    int32_t adc_H = ((int32_t)raw[6] << 8)  |  (int32_t)raw[7];

    data->temperature = compensate_temperature(adc_T);
    data->pressure    = compensate_pressure(adc_P);
    data->humidity    = compensate_humidity(adc_H);

    /* 海拔估算 (国际标准大气模型) */
    data->altitude = 44330.0f * (1.0f - powf(data->pressure / 1013.25f, 0.1903f));

    data->valid        = true;
    data->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    ESP_LOGD(TAG_BME280, "T=%.2f°C  P=%.2fhPa  H=%.2f%%  Alt=%.1fm",
             data->temperature, data->pressure, data->humidity, data->altitude);
    return ESP_OK;
}

esp_err_t bme280_soft_reset(void)
{
    return reg_write(REG_RESET, BME280_RESET_VAL);
}

esp_err_t bme280_chip_id(uint8_t *id)
{
    return reg_read(REG_CHIP_ID, id, 1);
}