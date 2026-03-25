/**
 * @file main.c
 * @brief I2C 通信示例 - 扫描 I2C 总线设备
 *
 * 功能：
 *   - 初始化 I2C 主机
 *   - 扫描 I2C 总线上的所有设备
 *   - 演示 I2C 读写操作
 *
 * 硬件连接：
 *   SDA → GPIO21（内部上拉或外接 4.7kΩ 上拉到 3.3V）
 *   SCL → GPIO22（内部上拉或外接 4.7kΩ 上拉到 3.3V）
 *
 * 常见 I2C 设备地址：
 *   0x3C / 0x3D - SSD1306 OLED
 *   0x76 / 0x77 - BMP280 气压传感器
 *   0x68 / 0x69 - MPU6050 陀螺仪
 *   0x27        - PCF8574 I2C 扩展器（LCD1602）
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "I2C";

#define I2C_MASTER_SCL_IO       GPIO_NUM_22
#define I2C_MASTER_SDA_IO       GPIO_NUM_21
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      100000   // 100kHz 标准模式
#define I2C_MASTER_TIMEOUT_MS   50

/**
 * @brief 初始化 I2C 主机
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
 * @brief 扫描 I2C 总线，查找所有设备
 */
static void i2c_scan(void)
{
    ESP_LOGI(TAG, "开始扫描 I2C 总线...");
    ESP_LOGI(TAG, "     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F");

    int device_count = 0;

    for (int i = 0; i < 128; i += 16) {
        printf("I2C: %02X: ", i);
        for (int j = 0; j < 16; j++) {
            uint8_t addr = i + j;
            if (addr < 0x08 || addr > 0x77) {
                printf("   ");
                continue;
            }

            /* 尝试向该地址发送数据，检查是否有 ACK */
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
            i2c_master_stop(cmd);

            esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd,
                                                   pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
            i2c_cmd_link_delete(cmd);

            if (ret == ESP_OK) {
                printf("%02X ", addr);
                device_count++;
            } else {
                printf("-- ");
            }
        }
        printf("\n");
    }

    ESP_LOGI(TAG, "扫描完成，发现 %d 个设备", device_count);
}

/**
 * @brief 向 I2C 设备写入数据
 */
static esp_err_t i2c_write_bytes(uint8_t dev_addr, uint8_t reg_addr,
                                   const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd,
                                          pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/**
 * @brief 从 I2C 设备读取数据
 */
static esp_err_t i2c_read_bytes(uint8_t dev_addr, uint8_t reg_addr,
                                  uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);  // 重复起始条件
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd,
                                          pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

void app_main(void)
{
    ESP_LOGI(TAG, "I2C 通信示例启动");
    ESP_LOGI(TAG, "SDA: GPIO%d, SCL: GPIO%d, 频率: %dHz",
             I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO, I2C_MASTER_FREQ_HZ);

    /* 初始化 I2C */
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C 主机初始化完成");

    /* 扫描 I2C 总线 */
    i2c_scan();

    /* 主循环：每 10 秒重新扫描一次 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "重新扫描 I2C 总线...");
        i2c_scan();
    }
}