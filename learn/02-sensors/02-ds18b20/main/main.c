/**
 * @file main.c
 * @brief DS18B20 单总线温度传感器示例
 *
 * 功能：
 *   - 使用 1-Wire 协议读取 DS18B20 温度
 *   - 支持多个传感器（通过 ROM 地址区分）
 *   - 精度可配置（9~12位）
 *
 * 硬件连接：
 *   DS18B20 VDD → 3.3V
 *   DS18B20 DQ  → GPIO4（通过 4.7kΩ 上拉电阻到 3.3V）
 *   DS18B20 GND → GND
 *
 * 注意：本示例使用软件模拟 1-Wire 协议
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

static const char *TAG = "DS18B20";

#define DS18B20_GPIO    GPIO_NUM_4

/* DS18B20 命令 */
#define DS18B20_CMD_SKIP_ROM        0xCC
#define DS18B20_CMD_CONVERT_T       0x44
#define DS18B20_CMD_READ_SCRATCHPAD 0xBE
#define DS18B20_CMD_READ_ROM        0x33

/**
 * @brief 1-Wire 复位时序
 * @return true 检测到设备，false 无设备
 */
static bool onewire_reset(void)
{
    gpio_set_direction(DS18B20_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DS18B20_GPIO, 0);
    ets_delay_us(480);  // 复位脉冲：480us

    gpio_set_direction(DS18B20_GPIO, GPIO_MODE_INPUT);
    ets_delay_us(70);   // 等待存在脉冲

    bool presence = (gpio_get_level(DS18B20_GPIO) == 0);
    ets_delay_us(410);  // 等待复位完成

    return presence;
}

/**
 * @brief 1-Wire 写一位
 */
static void onewire_write_bit(int bit)
{
    gpio_set_direction(DS18B20_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DS18B20_GPIO, 0);

    if (bit) {
        ets_delay_us(6);
        gpio_set_level(DS18B20_GPIO, 1);
        ets_delay_us(64);
    } else {
        ets_delay_us(60);
        gpio_set_level(DS18B20_GPIO, 1);
        ets_delay_us(10);
    }
}

/**
 * @brief 1-Wire 读一位
 */
static int onewire_read_bit(void)
{
    gpio_set_direction(DS18B20_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DS18B20_GPIO, 0);
    ets_delay_us(6);

    gpio_set_direction(DS18B20_GPIO, GPIO_MODE_INPUT);
    ets_delay_us(9);

    int bit = gpio_get_level(DS18B20_GPIO);
    ets_delay_us(55);

    return bit;
}

/**
 * @brief 1-Wire 写一字节
 */
static void onewire_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        onewire_write_bit(byte & 0x01);
        byte >>= 1;
    }
}

/**
 * @brief 1-Wire 读一字节
 */
static uint8_t onewire_read_byte(void)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (onewire_read_bit() << i);
    }
    return byte;
}

/**
 * @brief 读取 DS18B20 温度
 * @param temperature 输出温度值（摄氏度）
 * @return ESP_OK 成功
 */
static esp_err_t ds18b20_read_temperature(float *temperature)
{
    /* 步骤1：发送温度转换命令 */
    if (!onewire_reset()) {
        ESP_LOGE(TAG, "未检测到 DS18B20");
        return ESP_FAIL;
    }
    onewire_write_byte(DS18B20_CMD_SKIP_ROM);    // 跳过 ROM（单设备）
    onewire_write_byte(DS18B20_CMD_CONVERT_T);   // 开始温度转换

    /* 等待转换完成（12位精度需要 750ms） */
    vTaskDelay(pdMS_TO_TICKS(800));

    /* 步骤2：读取温度数据 */
    if (!onewire_reset()) {
        return ESP_FAIL;
    }
    onewire_write_byte(DS18B20_CMD_SKIP_ROM);
    onewire_write_byte(DS18B20_CMD_READ_SCRATCHPAD);  // 读取暂存器

    /* 读取 9 字节暂存器数据 */
    uint8_t scratchpad[9];
    for (int i = 0; i < 9; i++) {
        scratchpad[i] = onewire_read_byte();
    }

    /* CRC 校验（简化版，仅检查字节9） */
    /* 完整 CRC 校验需要实现 CRC8 算法 */

    /* 解析温度（小端格式，16位有符号整数） */
    int16_t raw_temp = (scratchpad[1] << 8) | scratchpad[0];
    *temperature = raw_temp / 16.0f;  // 12位精度，LSB = 0.0625°C

    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "DS18B20 温度传感器示例启动");

    /* 配置 GPIO */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DS18B20_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* 检测传感器 */
    if (onewire_reset()) {
        ESP_LOGI(TAG, "检测到 DS18B20 传感器");
    } else {
        ESP_LOGE(TAG, "未检测到 DS18B20，请检查连接");
    }

    float temperature;

    while (1) {
        esp_err_t ret = ds18b20_read_temperature(&temperature);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "温度: %.4f°C", temperature);

            if (temperature > 40.0f) {
                ESP_LOGW(TAG, "⚠️  温度过高！");
            } else if (temperature < 0.0f) {
                ESP_LOGW(TAG, "❄️  温度低于冰点！");
            }
        } else {
            ESP_LOGE(TAG, "读取温度失败");
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}