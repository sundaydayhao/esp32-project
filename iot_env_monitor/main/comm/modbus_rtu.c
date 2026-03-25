/**
 * @file modbus_rtu.c
 * @brief Modbus RTU 从站实现
 *
 * 手动实现轻量级 Modbus RTU 协议栈，避免依赖第三方库。
 * CRC-16 使用查表法，帧间隔检测使用 3.5 字符时间。
 */
#include "modbus_rtu.h"
#include "config.h"
#include "sensors/sensor_manager.h"
#include "alarm/alarm_manager.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* ---- Modbus 功能码 ---- */
#define FC_READ_HOLDING_REGS    0x03
#define FC_READ_INPUT_REGS      0x04
#define FC_WRITE_SINGLE_REG     0x06
#define FC_WRITE_MULTI_REGS     0x10

/* ---- 异常码 ---- */
#define EX_ILLEGAL_FUNCTION     0x01
#define EX_ILLEGAL_DATA_ADDR    0x02
#define EX_ILLEGAL_DATA_VALUE   0x03
#define EX_SLAVE_DEVICE_FAILURE 0x04

#define MB_MAX_FRAME_LEN        256
#define MB_FRAME_TIMEOUT_MS     4       /* 3.5 字符时间 @ 9600 baud */

static uint16_t s_input_regs[MODBUS_REG_COUNT];
static uint16_t s_holding_regs[MODBUS_REG_COUNT];

/* ---- CRC-16 查表法 ---- */
static const uint16_t crc16_table[256] = {
    0x0000,0xC0C1,0xC181,0x0140,0xC301,0x03C0,0x0280,0xC241,
    0xC601,0x06C0,0x0780,0xC741,0x0500,0xC5C1,0xC481,0x0440,
    0xCC01,0x0CC0,0x0D80,0xCD41,0x0F00,0xCFC1,0xCE81,0x0E40,
    0x0A00,0xCAC1,0xCB81,0x0B40,0xC901,0x09C0,0x0880,0xC841,
    0xD801,0x18C0,0x1980,0xD941,0x1B00,0xDBC1,0xDA81,0x1A40,
    0x1E00,0xDEC1,0xDF81,0x1F40,0xDD01,0x1DC0,0x1C80,0xDC41,
    0x1400,0xD4C1,0xD581,0x1540,0xD701,0x17C0,0x1680,0xD641,
    0xD201,0x12C0,0x1380,0xD341,0x1100,0xD1C1,0xD081,0x1040,
    0xF001,0x30C0,0x3180,0xF141,0x3300,0xF3C1,0xF281,0x3240,
    0x3600,0xF6C1,0xF781,0x3740,0xF501,0x35C0,0x3480,0xF441,
    0x3C00,0xFCC1,0xFD81,0x3D40,0xFF01,0x3FC0,0x3E80,0xFE41,
    0xFA01,0x3AC0,0x3B80,0xFB41,0x3900,0xF9C1,0xF881,0x3840,
    0x2800,0xE8C1,0xE981,0x2940,0xEB01,0x2BC0,0x2A80,0xEA41,
    0xEE01,0x2EC0,0x2F80,0xEF41,0x2D00,0xEDC1,0xEC81,0x2C40,
    0xE401,0x24C0,0x2580,0xE541,0x2700,0xE7C1,0xE681,0x2640,
    0x2200,0xE2C1,0xE381,0x2340,0xE101,0x21C0,0x2080,0xE041,
    0xA001,0x60C0,0x6180,0xA141,0x6300,0xA3C1,0xA281,0x6240,
    0x6600,0xA6C1,0xA781,0x6740,0xA501,0x65C0,0x6480,0xA441,
    0x6C00,0xACC1,0xAD81,0x6D40,0xAF01,0x6FC0,0x6E80,0xAE41,
    0xAA01,0x6AC0,0x6B80,0xAB41,0x6900,0xA9C1,0xA881,0x6840,
    0x7800,0xB8C1,0xB981,0x7940,0xBB01,0x7BC0,0x7A80,0xBA41,
    0xBE01,0x7EC0,0x7F80,0xBF41,0x7D00,0xBDC1,0xBC81,0x7C40,
    0x7400,0xB4C1,0xB581,0x7540,0xB701,0x77C0,0x7680,0xB641,
    0xB201,0x72C0,0x7380,0xB341,0x7100,0xB1C1,0xB081,0x7040,
    0x5000,0x90C1,0x9181,0x5140,0x9301,0x53C0,0x5280,0x9241,
    0x9601,0x56C0,0x5780,0x9741,0x5500,0x95C1,0x9481,0x5440,
    0x9C01,0x5CC0,0x5D80,0x9D41,0x5F00,0x9FC1,0x9E81,0x5E40,
    0x5A00,0x9AC1,0x9B81,0x5B40,0x9901,0x59C0,0x5880,0x9841,
    0x8801,0x48C0,0x4980,0x8941,0x4B00,0x8BC1,0x8A81,0x4A40,
    0x4E00,0x8EC1,0x8F81,0x4F40,0x8D01,0x4DC0,0x4C80,0x8C41,
    0x4400,0x84C1,0x8581,0x4540,0x8701,0x47C0,0x4680,0x8641,
    0x8201,0x42C0,0x4380,0x8341,0x4100,0x81C1,0x8081,0x4040,
};

static uint16_t crc16(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
        crc = (crc >> 8) ^ crc16_table[(crc ^ buf[i]) & 0xFF];
    return crc;
}

static inline void rs485_tx_enable(void) { gpio_set_level(MODBUS_DE_PIN, 1); }
static inline void rs485_rx_enable(void) { gpio_set_level(MODBUS_DE_PIN, 0); }

static void send_response(const uint8_t *buf, size_t len)
{
    uint16_t crc = crc16(buf, len);
    uint8_t frame[MB_MAX_FRAME_LEN + 2];
    memcpy(frame, buf, len);
    frame[len]     = (uint8_t)(crc & 0xFF);
    frame[len + 1] = (uint8_t)(crc >> 8);

    rs485_tx_enable();
    uart_write_bytes(MODBUS_UART_PORT, (const char *)frame, len + 2);
    uart_wait_tx_done(MODBUS_UART_PORT, pdMS_TO_TICKS(100));
    rs485_rx_enable();
}

static void send_exception(uint8_t addr, uint8_t fc, uint8_t ex_code)
{
    uint8_t resp[3] = { addr, (uint8_t)(fc | 0x80), ex_code };
    send_response(resp, 3);
    ESP_LOGW(TAG_MODBUS, "发送异常响应 FC=0x%02X EX=0x%02X", fc, ex_code);
}

/* ---- 处理读寄存器请求 (FC03/FC04) ---- */
static void handle_read_regs(const uint8_t *req, uint16_t *regs, uint16_t reg_count)
{
    uint8_t  addr      = req[0];
    uint8_t  fc        = req[1];
    uint16_t start_reg = ((uint16_t)req[2] << 8) | req[3];
    uint16_t num_regs  = ((uint16_t)req[4] << 8) | req[5];

    if (num_regs == 0 || num_regs > 125) {
        send_exception(addr, fc, EX_ILLEGAL_DATA_VALUE);
        return;
    }
    if (start_reg + num_regs > reg_count) {
        send_exception(addr, fc, EX_ILLEGAL_DATA_ADDR);
        return;
    }

    uint8_t resp[3 + 125 * 2];
    resp[0] = addr;
    resp[1] = fc;
    resp[2] = (uint8_t)(num_regs * 2);

    for (uint16_t i = 0; i < num_regs; i++) {
        resp[3 + i * 2]     = (uint8_t)(regs[start_reg + i] >> 8);
        resp[3 + i * 2 + 1] = (uint8_t)(regs[start_reg + i] & 0xFF);
    }
    send_response(resp, 3 + num_regs * 2);
    ESP_LOGD(TAG_MODBUS, "FC%02X 读寄存器 %u~%u", fc, start_reg, start_reg + num_regs - 1);
}

/* ---- 处理写单个寄存器 (FC06) ---- */
static void handle_write_single(const uint8_t *req)
{
    uint8_t  addr  = req[0];
    uint16_t reg   = ((uint16_t)req[2] << 8) | req[3];
    uint16_t value = ((uint16_t)req[4] << 8) | req[5];

    if (reg >= MODBUS_REG_COUNT) {
        send_exception(addr, FC_WRITE_SINGLE_REG, EX_ILLEGAL_DATA_ADDR);
        return;
    }
    s_holding_regs[reg] = value;
    /* 回显请求帧作为响应 */
    send_response(req, 6);
    ESP_LOGI(TAG_MODBUS, "FC06 写寄存器 %u = %u", reg, value);
}

/* ---- 处理写多个寄存器 (FC10) ---- */
static void handle_write_multi(const uint8_t *req)
{
    uint8_t  addr      = req[0];
    uint16_t start_reg = ((uint16_t)req[2] << 8) | req[3];
    uint16_t num_regs  = ((uint16_t)req[4] << 8) | req[5];
    uint8_t  byte_cnt  = req[6];

    if (num_regs == 0 || num_regs > 123 || byte_cnt != num_regs * 2) {
        send_exception(addr, FC_WRITE_MULTI_REGS, EX_ILLEGAL_DATA_VALUE);
        return;
    }
    if (start_reg + num_regs > MODBUS_REG_COUNT) {
        send_exception(addr, FC_WRITE_MULTI_REGS, EX_ILLEGAL_DATA_ADDR);
        return;
    }
    for (uint16_t i = 0; i < num_regs; i++) {
        s_holding_regs[start_reg + i] =
            ((uint16_t)req[7 + i * 2] << 8) | req[7 + i * 2 + 1];
    }
    uint8_t resp[6] = { addr, FC_WRITE_MULTI_REGS,
                        req[2], req[3], req[4], req[5] };
    send_response(resp, 6);
    ESP_LOGI(TAG_MODBUS, "FC10 写多寄存器 %u~%u", start_reg, start_reg + num_regs - 1);
}

/* ---- 处理一帧请求 ---- */
static void process_frame(const uint8_t *buf, size_t len)
{
    if (len < 4) return;

    /* CRC 校验 */
    uint16_t recv_crc = ((uint16_t)buf[len - 1] << 8) | buf[len - 2];
    uint16_t calc_crc = crc16(buf, len - 2);
    if (recv_crc != calc_crc) {
        ESP_LOGW(TAG_MODBUS, "CRC 错误 recv=0x%04X calc=0x%04X", recv_crc, calc_crc);
        return;
    }

    uint8_t addr = buf[0];
    uint8_t fc   = buf[1];

    /* 地址过滤：仅响应本机地址或广播地址 0x00 */
    if (addr != MODBUS_SLAVE_ADDR && addr != 0x00) return;

    switch (fc) {
    case FC_READ_HOLDING_REGS:
        handle_read_regs(buf, s_holding_regs, MODBUS_REG_COUNT);
        break;
    case FC_READ_INPUT_REGS:
        handle_read_regs(buf, s_input_regs, MODBUS_REG_COUNT);
        break;
    case FC_WRITE_SINGLE_REG:
        if (len >= 8) handle_write_single(buf);
        break;
    case FC_WRITE_MULTI_REGS:
        if (len >= 9) handle_write_multi(buf);
        break;
    default:
        if (addr != 0x00)
            send_exception(addr, fc, EX_ILLEGAL_FUNCTION);
        break;
    }
}

esp_err_t modbus_rtu_init(void)
{
    /* 配置 DE/RE 引脚 */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MODBUS_DE_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    rs485_rx_enable();

    uart_config_t cfg = {
        .baud_rate  = MODBUS_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_driver_install(MODBUS_UART_PORT, 512, 0, 0, NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    ret = uart_param_config(MODBUS_UART_PORT, &cfg);
    if (ret != ESP_OK) return ret;

    ret = uart_set_pin(MODBUS_UART_PORT, MODBUS_TX_PIN, MODBUS_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) return ret;

    memset(s_input_regs,   0, sizeof(s_input_regs));
    memset(s_holding_regs, 0, sizeof(s_holding_regs));

    ESP_LOGI(TAG_MODBUS, "Modbus RTU 从站初始化成功 (addr=0x%02X, baud=%d)",
             MODBUS_SLAVE_ADDR, MODBUS_BAUD);
    return ESP_OK;
}

void modbus_rtu_update_registers(void)
{
    env_data_t data;
    sensor_manager_get_latest(&data);
    if (!data.valid) return;

    /* 温度 × 100 (有符号) */
    s_input_regs[MODBUS_REG_TEMPERATURE] = (uint16_t)(int16_t)(data.temperature * 100.0f);
    /* 湿度 × 100 */
    s_input_regs[MODBUS_REG_HUMIDITY]    = (uint16_t)(data.humidity * 100.0f);
    /* 气压整数 / 小数 */
    s_input_regs[MODBUS_REG_PRESSURE_I]  = (uint16_t)data.pressure;
    s_input_regs[MODBUS_REG_PRESSURE_F]  = (uint16_t)((data.pressure - (uint16_t)data.pressure) * 100.0f);
    /* CO₂ 高/低字 */
    s_input_regs[MODBUS_REG_CO2_H]       = (uint16_t)(data.co2_ppm >> 16);
    s_input_regs[MODBUS_REG_CO2_L]       = (uint16_t)(data.co2_ppm & 0xFFFF);
    /* 颗粒物 */
    s_input_regs[MODBUS_REG_PM1_0]       = data.pm1_0;
    s_input_regs[MODBUS_REG_PM2_5]       = data.pm2_5;
    s_input_regs[MODBUS_REG_PM10]        = data.pm10;
    s_input_regs[MODBUS_REG_AQI]         = data.aqi_level;

    /* 报警状态位图 */
    s_input_regs[MODBUS_REG_ALARM_BITS]  = (uint16_t)alarm_manager_get_active_bits();

    /* 传感器健康状态位图 */
    uint16_t health = 0;
    health |= (data.status_sht31   & 0x03) << 0;
    health |= (data.status_bme280  & 0x03) << 2;
    health |= (data.status_mhz19   & 0x03) << 4;
    health |= (data.status_pms5003 & 0x03) << 6;
    s_input_regs[MODBUS_REG_HEALTH_BITS] = health;
}

void modbus_task(void *arg)
{
    ESP_LOGI(TAG_MODBUS, "Modbus RTU 从站任务启动");

    uint8_t  buf[MB_MAX_FRAME_LEN];
    size_t   buf_len = 0;
    TickType_t last_rx = 0;

    while (1) {
        uint8_t byte;
        int len = uart_read_bytes(MODBUS_UART_PORT, &byte, 1, pdMS_TO_TICKS(1));

        if (len > 0) {
            TickType_t now = xTaskGetTickCount();
            /* 检测帧间隔：超过 3.5 字符时间则认为是新帧 */
            if (buf_len > 0 &&
                (now - last_rx) > pdMS_TO_TICKS(MB_FRAME_TIMEOUT_MS)) {
                process_frame(buf, buf_len);
                buf_len = 0;
            }
            if (buf_len < MB_MAX_FRAME_LEN) {
                buf[buf_len++] = byte;
            }
            last_rx = now;
        } else {
            /* 无新数据，检查是否有待处理帧 */
            if (buf_len > 0) {
                TickType_t now = xTaskGetTickCount();
                if ((now - last_rx) > pdMS_TO_TICKS(MB_FRAME_TIMEOUT_MS * 2)) {
                    process_frame(buf, buf_len);
                    buf_len = 0;
                }
            }
            /* 更新寄存器 */
            modbus_rtu_update_registers();
        }
    }
}