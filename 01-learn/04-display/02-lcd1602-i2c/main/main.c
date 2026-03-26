/**
 * @file main.c
 * @brief LCD1602 液晶屏示例（I2C 接口）
 *
 * 硬件连接：
 *   PCF8574 VCC → 5V（LCD1602 需要 5V）
 *   PCF8574 GND → GND
 *   PCF8574 SDA → GPIO21
 *   PCF8574 SCL → GPIO22
 *
 * 注意：PCF8574 使用 5V 供电，但 I2C 信号为 3.3V 兼容
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "lcd1602.h"

static const char *TAG = "LCD1602_Main";

#define I2C_MASTER_SCL_IO   GPIO_NUM_22
#define I2C_MASTER_SDA_IO   GPIO_NUM_21
#define I2C_MASTER_NUM      I2C_NUM_0
#define I2C_MASTER_FREQ_HZ  100000

static void i2c_master_init(void)
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
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));
}

void app_main(void)
{
    ESP_LOGI(TAG, "LCD1602 示例启动");

    i2c_master_init();
    ESP_ERROR_CHECK(lcd1602_init(I2C_MASTER_NUM, LCD1602_I2C_ADDR));

    /* 显示欢迎信息 */
    lcd1602_set_cursor(0, 0);
    lcd1602_print("  ESP32 Learning");
    lcd1602_set_cursor(0, 1);
    lcd1602_print("   Hello World!");
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* 主循环：显示计数器 */
    int count = 0;
    while (1) {
        char buf[17];

        lcd1602_set_cursor(0, 0);
        lcd1602_print("ESP32 ESP-IDF   ");

        snprintf(buf, sizeof(buf), "Count: %-8d", count++);
        lcd1602_set_cursor(0, 1);
        lcd1602_print(buf);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}