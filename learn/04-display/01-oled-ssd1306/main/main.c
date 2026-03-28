/**
 * @file main.c
 * @brief SSD1306 OLED 显示屏示例
 *
 * 功能：
 *   - 显示文字、图形
 *   - 实现滚动动画
 *   - 显示传感器数据（模拟）
 *
 * 硬件连接：
 *   SSD1306 VCC → 3.3V
 *   SSD1306 GND → GND
 *   SSD1306 SDA → GPIO21
 *   SSD1306 SCL → GPIO22
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "ssd1306.h"

static const char *TAG = "OLED";

#define I2C_MASTER_SCL_IO   GPIO_NUM_22
#define I2C_MASTER_SDA_IO   GPIO_NUM_21
#define I2C_MASTER_NUM      I2C_NUM_0
#define I2C_MASTER_FREQ_HZ  400000
#define SSD1306_I2C_ADDR    0x3C

/**
 * @brief 初始化 I2C
 */
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
    ESP_LOGI(TAG, "SSD1306 OLED 示例启动");

    /* 初始化 I2C */
    i2c_master_init();

    /* 初始化 OLED */
    ESP_ERROR_CHECK(ssd1306_init(I2C_MASTER_NUM, SSD1306_I2C_ADDR));
    ESP_LOGI(TAG, "OLED 初始化完成");

    /* 显示欢迎信息 */
    ssd1306_clear();
    ssd1306_draw_string(0, 0,  "ESP32 Learning Kit", true);
    ssd1306_draw_string(0, 16, "ESP-IDF Framework", true);
    ssd1306_draw_string(0, 32, "SSD1306 OLED Demo", true);
    ssd1306_draw_hline(0, 48, 128, true);
    ssd1306_draw_string(0, 52, "Hello, World!", true);
    ssd1306_refresh();
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* 演示图形绘制 */
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "Graphics Demo:", true);
    ssd1306_draw_rect(0, 12, 40, 30, true);       // 空心矩形
    ssd1306_fill_rect(50, 12, 30, 30, true);       // 填充矩形
    ssd1306_draw_hline(90, 27, 35, true);          // 水平线
    ssd1306_draw_vline(110, 12, 30, true);         // 垂直线
    ssd1306_refresh();
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* 主循环：模拟传感器数据显示 */
    int count = 0;
    float temp = 25.0f;
    float humi = 60.0f;

    while (1) {
        /* 模拟数据变化 */
        temp = 25.0f + (count % 10) * 0.5f;
        humi = 60.0f + (count % 20) * 0.5f;
        count++;

        /* 更新显示 */
        ssd1306_clear();

        /* 标题栏 */
        ssd1306_draw_string(0, 0, "ESP32 Sensor Data", true);
        ssd1306_draw_hline(0, 10, 128, true);

        /* 温度 */
        char buf[32];
        snprintf(buf, sizeof(buf), "Temp: %.1f C", temp);
        ssd1306_draw_string(0, 16, buf, true);

        /* 湿度 */
        snprintf(buf, sizeof(buf), "Humi: %.1f %%", humi);
        ssd1306_draw_string(0, 28, buf, true);

        /* 计数器 */
        snprintf(buf, sizeof(buf), "Count: %d", count);
        ssd1306_draw_string(0, 40, buf, true);

        /* 进度条 */
        int bar_width = (count % 128);
        ssd1306_draw_rect(0, 54, 128, 10, true);
        ssd1306_fill_rect(1, 55, bar_width, 8, true);

        ssd1306_refresh();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}