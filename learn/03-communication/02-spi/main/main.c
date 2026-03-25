/**
 * @file main.c
 * @brief SPI 通信示例
 *
 * 功能：
 *   - 初始化 SPI 主机（VSPI）
 *   - 演示 SPI 全双工数据传输
 *   - 演示 DMA 传输大数据块
 *
 * 硬件连接（VSPI）：
 *   MOSI → GPIO23
 *   MISO → GPIO19
 *   SCK  → GPIO18
 *   CS   → GPIO5
 *
 * 本示例演示 SPI 自回环测试（MOSI 连接 MISO）
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "SPI";

/* SPI 引脚配置 */
#define SPI_MOSI_PIN    GPIO_NUM_23
#define SPI_MISO_PIN    GPIO_NUM_19
#define SPI_SCK_PIN     GPIO_NUM_18
#define SPI_CS_PIN      GPIO_NUM_5

#define SPI_HOST        SPI2_HOST   // VSPI
#define SPI_CLOCK_HZ    1000000     // 1MHz

static spi_device_handle_t spi_handle;

/**
 * @brief 初始化 SPI 主机
 */
static esp_err_t spi_master_init(void)
{
    /* 配置 SPI 总线 */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = SPI_MOSI_PIN,
        .miso_io_num     = SPI_MISO_PIN,
        .sclk_io_num     = SPI_SCK_PIN,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4096,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    /* 添加 SPI 设备 */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_CLOCK_HZ,
        .mode           = 0,            // SPI 模式 0 (CPOL=0, CPHA=0)
        .spics_io_num   = SPI_CS_PIN,
        .queue_size     = 7,
        .flags          = 0,
    };
    return spi_bus_add_device(SPI_HOST, &dev_cfg, &spi_handle);
}

/**
 * @brief SPI 传输数据（全双工）
 */
static esp_err_t spi_transfer(const uint8_t *tx_data, uint8_t *rx_data, size_t len)
{
    spi_transaction_t trans = {
        .length    = len * 8,   // 位数
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    return spi_device_transmit(spi_handle, &trans);
}

/**
 * @brief SPI 发送单字节
 */
static uint8_t spi_send_byte(uint8_t byte)
{
    uint8_t rx_byte = 0;
    spi_transaction_t trans = {
        .length    = 8,
        .tx_buffer = &byte,
        .rx_buffer = &rx_byte,
    };
    spi_device_transmit(spi_handle, &trans);
    return rx_byte;
}

void app_main(void)
{
    ESP_LOGI(TAG, "SPI 通信示例启动");
    ESP_LOGI(TAG, "MOSI: GPIO%d, MISO: GPIO%d, SCK: GPIO%d, CS: GPIO%d",
             SPI_MOSI_PIN, SPI_MISO_PIN, SPI_SCK_PIN, SPI_CS_PIN);

    /* 初始化 SPI */
    ESP_ERROR_CHECK(spi_master_init());
    ESP_LOGI(TAG, "SPI 主机初始化完成，时钟: %dHz", SPI_CLOCK_HZ);

    /* 自回环测试（需要将 MOSI 连接到 MISO） */
    ESP_LOGI(TAG, "开始 SPI 自回环测试（请将 MOSI 连接到 MISO）...");

    uint8_t tx_buf[16];
    uint8_t rx_buf[16];

    int test_count = 0;
    int pass_count = 0;

    while (1) {
        /* 准备测试数据 */
        for (int i = 0; i < 16; i++) {
            tx_buf[i] = (test_count + i) & 0xFF;
        }
        memset(rx_buf, 0, sizeof(rx_buf));

        /* 执行 SPI 传输 */
        esp_err_t ret = spi_transfer(tx_buf, rx_buf, 16);

        if (ret == ESP_OK) {
            /* 验证回环数据 */
            bool match = (memcmp(tx_buf, rx_buf, 16) == 0);
            test_count++;
            if (match) pass_count++;

            ESP_LOGI(TAG, "测试 #%d: %s", test_count, match ? "通过" : "失败");

            if (!match) {
                ESP_LOGW(TAG, "发送: %02X %02X %02X %02X ...",
                         tx_buf[0], tx_buf[1], tx_buf[2], tx_buf[3]);
                ESP_LOGW(TAG, "接收: %02X %02X %02X %02X ...",
                         rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);
            }
        } else {
            ESP_LOGE(TAG, "SPI 传输失败: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}