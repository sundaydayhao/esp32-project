#pragma once

/**
 * @file config.h
 * @brief 汽车小摆件 - 全局配置文件
 * 
 * ESP32-S3 Car Display Companion
 * 硬件引脚定义和全局参数配置
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 显示屏配置 (ST7789 240x240 SPI LCD)
 * ============================================================ */
#define LCD_HOST            SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000)  // 40MHz SPI时钟
#define LCD_BK_LIGHT_ON_LEVEL  1

#define PIN_NUM_LCD_MOSI    11   // SPI MOSI
#define PIN_NUM_LCD_SCLK    12   // SPI Clock
#define PIN_NUM_LCD_CS      10   // Chip Select
#define PIN_NUM_LCD_DC      13   // Data/Command
#define PIN_NUM_LCD_RST     14   // Reset
#define PIN_NUM_LCD_BL      15   // Backlight

#define LCD_H_RES           240  // 水平分辨率
#define LCD_V_RES           240  // 垂直分辨率
#define LCD_CMD_BITS        8
#define LCD_PARAM_BITS      8

/* ============================================================
 * I2C 配置 (MPU6050 IMU)
 * ============================================================ */
#define I2C_MASTER_NUM      I2C_NUM_0
#define I2C_MASTER_SDA_IO   8
#define I2C_MASTER_SCL_IO   9
#define I2C_MASTER_FREQ_HZ  400000   // 400kHz Fast Mode
#define I2C_MASTER_TIMEOUT_MS 1000

#define MPU6050_ADDR        0x68     // AD0=GND -> 0x68, AD0=VCC -> 0x69

/* ============================================================
 * GPS UART 配置
 * ============================================================ */
#define GPS_UART_NUM        UART_NUM_1
#define GPS_UART_TX_PIN     17
#define GPS_UART_RX_PIN     18
#define GPS_UART_BAUD_RATE  9600
#define GPS_UART_BUF_SIZE   1024

/* ============================================================
 * 语音模块配置
 * SYN6288 TTS (TX) + HLK-V20 ASR (RX) 共用 UART2
 * ============================================================ */
#define VOICE_UART_NUM      UART_NUM_2
#define VOICE_TTS_TX_PIN    19   // SYN6288 RX 引脚（ESP32发送）
#define VOICE_ASR_RX_PIN    20   // HLK-V20 TX 引脚（ESP32接收）
#define VOICE_UART_BAUD     9600
#define VOICE_UART_BUF_SIZE 512

/* ============================================================
 * 应用参数
 * ============================================================ */
#define SPEED_UPDATE_INTERVAL_MS    100   // 速度更新间隔
#define IMU_UPDATE_INTERVAL_MS      50    // IMU更新间隔
#define UI_UPDATE_INTERVAL_MS       33    // UI刷新间隔 (~30fps)
#define EMPATHY_CHECK_INTERVAL_MS   500   // 共情检查间隔
#define VOICE_AWAKE_TIMEOUT_MS      30000 // 语音唤醒超时（30秒）

// 速度阈值 (km/h)
#define SPEED_SLOW          30.0f    // 慢速
#define SPEED_NORMAL        60.0f    // 正常
#define SPEED_FAST          100.0f   // 快速
#define SPEED_VERY_FAST     120.0f   // 超速警告

// 加速度阈值 (km/h/s)
#define ACCEL_GENTLE        5.0f     // 温和加速
#define ACCEL_MODERATE      15.0f    // 中等加速
#define ACCEL_HARD          25.0f    // 急加速
#define DECEL_GENTLE        -5.0f    // 温和减速
#define DECEL_MODERATE      -15.0f   // 中等减速
#define DECEL_HARD          -25.0f   // 急刹车

// 转向阈值 (度/秒)
#define TURN_SLIGHT         15.0f    // 轻微转向
#define TURN_MODERATE       45.0f    // 中等转向
#define TURN_SHARP          90.0f    // 急转弯

/* ============================================================
 * 颜色定义 (RGB565)
 * ============================================================ */
#define COLOR_BG            0x0000   // 黑色背景
#define COLOR_SPEED         0x07FF   // 青色速度数字
#define COLOR_NORMAL        0x07E0   // 绿色
#define COLOR_WARNING       0xFFE0   // 黄色警告
#define COLOR_DANGER        0xF800   // 红色危险
#define COLOR_WHITE         0xFFFF   // 白色
#define COLOR_GRAY          0x8410   // 灰色

#ifdef __cplusplus
}
#endif