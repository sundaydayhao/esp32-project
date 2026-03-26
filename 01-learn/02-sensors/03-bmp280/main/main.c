/**
 * @file main.c
 * @brief BMP280 气压/温度传感器示例（I2C 接口）
 *
 * 功能：
 *   - 通过 I2C 读取 BMP280 气压和温度数据
 *   - 计算海拔高度
 *   - 使用 BMP280 内置校准数据进行精确测量
 *
 * 硬件连接：
 *   BMP280 VCC → 3.3V
 *   BMP280 GND → GND
 *   BMP280 SDA → GPIO21
 *   BMP280 SCL → GPIO22
 *   BMP280 SDO → GND（I2C 地址 0x76）或 3.3V（地址 0x77）
 */

#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "BMP280";

/* I2C 配置 */
#define I2C_MASTER_SCL_IO       GPIO_NUM_22
#define I2C_MASTER_SDA_IO       GPIO_NUM_21
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   1000

/* BMP280 I2C 地址 */
#define BMP280_ADDR             0x76  // SDO 接 GND

/* BMP280 寄存器地址 */
#define BMP280_REG_CHIP_ID      0xD0
#define BMP280_REG_RESET        0xE0
#define BMP280_REG_STATUS       0xF3
#define BMP280_REG_CTRL_MEAS    0xF4
#define BMP280_REG_CONFIG       0xF5
#define BMP280_REG_PRESS_MSB    0xF7
#define BMP280_REG_CALIB_START  0x88

/* BMP280 芯片 ID */
#define BMP280_CHIP_ID          0x60

/* 校准数据结构 */
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
} bmp280_calib_t;

static bmp280_calib_t calib_data;
static int32_t t_fine;  // 用于气压补偿的温度精细值

/**
 * @brief I2C 写寄存器
 */
static esp_err_t i2c_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(I2C_MASTER_NUM, BMP280_ADDR,
                                       data, sizeof(data),
                                       pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}

/**
 * @brief I2C 读寄存器
 */
static esp_err_t i2c_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_MASTER_NUM, BMP280_ADDR,
                                         &reg, 1, data, len,
                                         pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}

/**
 * @brief 初始化 I2C
 */
static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_MASTER_SDA_IO,
        .scl_io_num       = I2C_MASTER_SCL_IO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

/**
 * @brief 读取 BMP280 校准数据
 */
static esp_err_t bmp280_read_calibration(void)
{
    uint8_t calib[24];
    ESP_ERROR_CHECK(i2c_read_reg(BMP280_REG_CALIB_START, calib, 24));

    calib_data.dig_T1 = (calib[1] << 8) | calib[0];
    calib_data.dig_T2 = (calib[3] << 8) | calib[2];
    calib_data.dig_T3 = (calib[5] << 8) | calib[4];
    calib_data.dig_P1 = (calib[7] << 8) | calib[6];
    calib_data.dig_P2 = (calib[9] << 8) | calib[8];
    calib_data.dig_P3 = (calib[11] << 8) | calib[10];
    calib_data.dig_P4 = (calib[13] << 8) | calib[12];
    calib_data.dig_P5 = (calib[15] << 8) | calib[14];
    calib_data.dig_P6 = (calib[17] << 8) | calib[16];
    calib_data.dig_P7 = (calib[19] << 8) | calib[18];
    calib_data.dig_P8 = (calib[21] << 8) | calib[20];
    calib_data.dig_P9 = (calib[23] << 8) | calib[22];

    return ESP_OK;
}

/**
 * @brief 温度补偿计算（BMP280 数据手册算法）
 * @return 温度（摄氏度 × 100）
 */
static int32_t bmp280_compensate_temperature(int32_t adc_T)
{
    int32_t var1, var2, T;
    var1 = ((((adc_T >> 3) - ((int32_t)calib_data.dig_T1 << 1))) *
            ((int32_t)calib_data.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)calib_data.dig_T1)) *
              ((adc_T >> 4) - ((int32_t)calib_data.dig_T1))) >> 12) *
            ((int32_t)calib_data.dig_T3)) >> 14;
    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8;
    return T;
}

/**
 * @brief 气压补偿计算（BMP280 数据手册算法）
 * @return 气压（Pa × 256）
 */
static uint32_t bmp280_compensate_pressure(int32_t adc_P)
{
    int64_t var1, var2, p;
    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)calib_data.dig_P6;
    var2 = var2 + ((var1 * (int64_t)calib_data.dig_P5) << 17);
    var2 = var2 + (((int64_t)calib_data.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)calib_data.dig_P3) >> 8) +
           ((var1 * (int64_t)calib_data.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)calib_data.dig_P1) >> 33;
    if (var1 == 0) return 0;
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)calib_data.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calib_data.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)calib_data.dig_P7) << 4);
    return (uint32_t)p;
}

void app_main(void)
{
    ESP_LOGI(TAG, "BMP280 气压传感器示例启动");

    /* 初始化 I2C */
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C 初始化完成");

    /* 检查芯片 ID */
    uint8_t chip_id;
    ESP_ERROR_CHECK(i2c_read_reg(BMP280_REG_CHIP_ID, &chip_id, 1));
    ESP_LOGI(TAG, "芯片 ID: 0x%02X（期望: 0x%02X）", chip_id, BMP280_CHIP_ID);

    if (chip_id != BMP280_CHIP_ID) {
        ESP_LOGE(TAG, "BMP280 未找到，请检查连接");
        return;
    }

    /* 读取校准数据 */
    ESP_ERROR_CHECK(bmp280_read_calibration());

    /* 配置 BMP280：正常模式，过采样 x2 温度，x16 气压，滤波系数 4 */
    ESP_ERROR_CHECK(i2c_write_reg(BMP280_REG_CONFIG, 0x10));    // 滤波系数 4
    ESP_ERROR_CHECK(i2c_write_reg(BMP280_REG_CTRL_MEAS, 0x57)); // 正常模式

    ESP_LOGI(TAG, "BMP280 配置完成，开始读取数据...");

    while (1) {
        /* 读取原始数据（6字节：气压3字节 + 温度3字节） */
        uint8_t raw[6];
        ESP_ERROR_CHECK(i2c_read_reg(BMP280_REG_PRESS_MSB, raw, 6));

        int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
        int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);

        /* 补偿计算 */
        int32_t temp_raw = bmp280_compensate_temperature(adc_T);
        uint32_t press_raw = bmp280_compensate_pressure(adc_P);

        float temperature = temp_raw / 100.0f;
        float pressure = press_raw / 25600.0f;  // 转换为 hPa

        /* 计算海拔（标准大气压 1013.25 hPa） */
        float altitude = 44330.0f * (1.0f - powf(pressure / 1013.25f, 0.1903f));

        ESP_LOGI(TAG, "温度: %.2f°C  气压: %.2f hPa  海拔: %.1f m",
                 temperature, pressure, altitude);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}