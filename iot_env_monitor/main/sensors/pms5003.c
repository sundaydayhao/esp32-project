/**
 * @file pms5003.c
 * @brief PMS5003 激光颗粒物传感器驱动实现
 */
#include "pms5003.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* ---- 帧格式常量 ---- */
#define PMS_FRAME_LEN       32
#define PMS_START1          0x42
#define PMS_START2          0x4D
#define PMS_UART_TIMEOUT    pdMS_TO_TICKS(2000)

/* ---- 被动模式命令 ---- */
static const uint8_t CMD_PASSIVE_MODE[] = { 0x42, 0x4D, 0xE1, 0x00, 0x00, 0x01, 0x70 };
static const uint8_t CMD_READ_DATA[]    = { 0x42, 0x4D, 0xE2, 0x00, 0x00, 0x01, 0x71 };
static const uint8_t CMD_SLEEP[]        = { 0x42, 0x4D, 0xE4, 0x00, 0x00, 0x01, 0x73 };
static const uint8_t CMD_WAKEUP[]       = { 0x42, 0x4D, 0xE4, 0x00, 0x01, 0x01, 0x74 };

static uart_port_t s_uart;
static int         s_set_pin;

/* ---- 帧校验 ---- */
static bool frame_valid(const uint8_t *buf)
{
    if (buf[0] != PMS_START1 || buf[1] != PMS_START2) return false;
    uint16_t checksum = 0;
    for (int i = 0; i < 30; i++) checksum += buf[i];
    uint16_t frame_check = ((uint16_t)buf[30] << 8) | buf[31];
    return checksum == frame_check;
}

static uint16_t parse_u16(const uint8_t *buf, int offset)
{
    return ((uint16_t)buf[offset] << 8) | buf[offset + 1];
}

esp_err_t pms5003_init(uart_port_t uart_port, int tx_pin, int rx_pin, int set_pin)
{
    s_uart    = uart_port;
    s_set_pin = set_pin;

    /* 配置 SET 引脚（高电平=正常工作，低电平=睡眠） */
    if (set_pin >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << set_pin),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(set_pin, 1);  /* 唤醒 */
    }

    uart_config_t cfg = {
        .baud_rate  = PMS5003_BAUD,
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

    /* 切换到被动模式 */
    vTaskDelay(pdMS_TO_TICKS(100));
    uart_write_bytes(uart_port, (const char *)CMD_PASSIVE_MODE,
                     sizeof(CMD_PASSIVE_MODE));
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 等待传感器稳定（激光器预热） */
    ESP_LOGI(TAG_PMS5003, "等待传感器稳定 (5s)...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG_PMS5003, "初始化成功");
    return ESP_OK;
}

esp_err_t pms5003_read(pms5003_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;

    /* 发送读取命令 */
    uart_flush_input(s_uart);
    uart_write_bytes(s_uart, (const char *)CMD_READ_DATA, sizeof(CMD_READ_DATA));

    /* 读取 32 字节响应帧 */
    uint8_t buf[PMS_FRAME_LEN];
    int len = uart_read_bytes(s_uart, buf, PMS_FRAME_LEN, PMS_UART_TIMEOUT);

    if (len != PMS_FRAME_LEN) {
        ESP_LOGE(TAG_PMS5003, "响应长度错误: %d", len);
        data->valid = false;
        return ESP_ERR_TIMEOUT;
    }

    if (!frame_valid(buf)) {
        ESP_LOGE(TAG_PMS5003, "帧校验失败");
        data->valid = false;
        return ESP_ERR_INVALID_CRC;
    }

    /* 解析数据（偏移量参考 PMS5003 数据手册） */
    data->pm1_0_std  = parse_u16(buf, 4);
    data->pm2_5_std  = parse_u16(buf, 6);
    data->pm10_std   = parse_u16(buf, 8);
    data->pm1_0_atm  = parse_u16(buf, 10);
    data->pm2_5_atm  = parse_u16(buf, 12);
    data->pm10_atm   = parse_u16(buf, 14);
    data->cnt_0_3um  = parse_u16(buf, 16);
    data->cnt_0_5um  = parse_u16(buf, 18);
    data->cnt_1_0um  = parse_u16(buf, 20);
    data->cnt_2_5um  = parse_u16(buf, 22);
    data->cnt_5_0um  = parse_u16(buf, 24);
    data->cnt_10_0um = parse_u16(buf, 26);

    data->aqi_level    = pms5003_calc_aqi_level(data->pm2_5_atm);
    data->valid        = true;
    data->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    ESP_LOGD(TAG_PMS5003, "PM1.0=%u  PM2.5=%u  PM10=%u µg/m³  AQI=%u",
             data->pm1_0_atm, data->pm2_5_atm, data->pm10_atm, data->aqi_level);
    return ESP_OK;
}

esp_err_t pms5003_sleep(void)
{
    if (s_set_pin >= 0) {
        gpio_set_level(s_set_pin, 0);
        ESP_LOGI(TAG_PMS5003, "进入睡眠模式 (GPIO)");
        return ESP_OK;
    }
    uart_write_bytes(s_uart, (const char *)CMD_SLEEP, sizeof(CMD_SLEEP));
    ESP_LOGI(TAG_PMS5003, "进入睡眠模式 (UART)");
    return ESP_OK;
}

esp_err_t pms5003_wakeup(void)
{
    if (s_set_pin >= 0) {
        gpio_set_level(s_set_pin, 1);
        ESP_LOGI(TAG_PMS5003, "唤醒传感器 (GPIO)，等待 30s 稳定...");
        return ESP_OK;
    }
    uart_write_bytes(s_uart, (const char *)CMD_WAKEUP, sizeof(CMD_WAKEUP));
    ESP_LOGI(TAG_PMS5003, "唤醒传感器 (UART)，等待 30s 稳定...");
    return ESP_OK;
}

/* ---- 中国 AQI PM2.5 分级标准 (GB 3095-2012) ---- */
uint8_t pms5003_calc_aqi_level(uint16_t pm25_atm)
{
    if (pm25_atm <= 35)  return 0;  /* 优 */
    if (pm25_atm <= 75)  return 1;  /* 良 */
    if (pm25_atm <= 115) return 2;  /* 轻度污染 */
    if (pm25_atm <= 150) return 3;  /* 中度污染 */
    if (pm25_atm <= 250) return 4;  /* 重度污染 */
    return 5;                        /* 严重污染 */
}