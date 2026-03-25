/**
 * @file dht11.c
 * @brief DHT11 温湿度传感器驱动实现
 *
 * DHT11 通信协议：
 *   1. 主机发送起始信号（低电平 18ms，高电平 20-40us）
 *   2. DHT11 响应（低电平 80us，高电平 80us）
 *   3. 传输 40 位数据（8位湿度整数 + 8位湿度小数 + 8位温度整数 + 8位温度小数 + 8位校验）
 *   4. 每位数据：低电平 50us + 高电平（26-28us=0，70us=1）
 */

#include "dht11.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

static const char *TAG = "DHT11";
static gpio_num_t dht11_gpio;

void dht11_init(gpio_num_t gpio_num)
{
    dht11_gpio = gpio_num;
    gpio_set_direction(dht11_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(dht11_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));  // 等待传感器稳定
    ESP_LOGI(TAG, "DHT11 初始化完成，GPIO%d", gpio_num);
}

/**
 * @brief 等待 GPIO 变为指定电平，超时返回 -1
 */
static int wait_for_level(int level, int timeout_us)
{
    int elapsed = 0;
    while (gpio_get_level(dht11_gpio) != level) {
        if (elapsed >= timeout_us) {
            return -1;
        }
        ets_delay_us(1);
        elapsed++;
    }
    return elapsed;
}

esp_err_t dht11_read(dht11_data_t *data)
{
    uint8_t bits[40] = {0};
    uint8_t bytes[5] = {0};

    /* 发送起始信号 */
    gpio_set_direction(dht11_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(dht11_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(20));  // 低电平 20ms
    gpio_set_level(dht11_gpio, 1);
    ets_delay_us(30);               // 高电平 30us

    /* 切换为输入模式，等待 DHT11 响应 */
    gpio_set_direction(dht11_gpio, GPIO_MODE_INPUT);

    /* 等待 DHT11 拉低（响应开始） */
    if (wait_for_level(0, 100) < 0) {
        ESP_LOGE(TAG, "DHT11 无响应（等待低电平超时）");
        return ESP_FAIL;
    }

    /* 等待 DHT11 拉高 */
    if (wait_for_level(1, 100) < 0) {
        ESP_LOGE(TAG, "DHT11 响应错误（等待高电平超时）");
        return ESP_FAIL;
    }

    /* 等待 DHT11 再次拉低（数据开始） */
    if (wait_for_level(0, 100) < 0) {
        ESP_LOGE(TAG, "DHT11 数据开始错误");
        return ESP_FAIL;
    }

    /* 读取 40 位数据 */
    for (int i = 0; i < 40; i++) {
        /* 等待高电平（每位开始） */
        if (wait_for_level(1, 60) < 0) {
            ESP_LOGE(TAG, "读取第 %d 位超时", i);
            return ESP_FAIL;
        }

        /* 测量高电平持续时间 */
        int high_time = wait_for_level(0, 80);
        if (high_time < 0) {
            ESP_LOGE(TAG, "第 %d 位高电平超时", i);
            return ESP_FAIL;
        }

        /* 高电平 > 40us 为 1，否则为 0 */
        bits[i] = (high_time > 40) ? 1 : 0;
    }

    /* 将位数据转换为字节 */
    for (int i = 0; i < 5; i++) {
        bytes[i] = 0;
        for (int j = 0; j < 8; j++) {
            bytes[i] = (bytes[i] << 1) | bits[i * 8 + j];
        }
    }

    /* 校验和验证 */
    uint8_t checksum = bytes[0] + bytes[1] + bytes[2] + bytes[3];
    if (checksum != bytes[4]) {
        ESP_LOGE(TAG, "校验和错误: 计算值=%d, 接收值=%d", checksum, bytes[4]);
        return ESP_FAIL;
    }

    /* 解析数据 */
    data->humidity    = bytes[0] + bytes[1] * 0.1f;
    data->temperature = bytes[2] + bytes[3] * 0.1f;

    /* 恢复输出模式 */
    gpio_set_direction(dht11_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(dht11_gpio, 1);

    return ESP_OK;
}