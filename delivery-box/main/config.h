/**
 * @file config.h
 * @brief 智能快递柜 - 硬件引脚与应用参数配置
 *
 * 硬件连接说明:
 * ┌─────────────────────────────────────────────────────────┐
 * │  ESP32-S3 智能快递柜 硬件配置                            │
 * ├──────────────────┬──────────────────────────────────────┤
 * │  舵机 (锁控制)    │  GPIO 1, 2, 4, 5                    │
 * │  4x4矩阵键盘     │  行: GPIO 6,7,8,9                    │
 * │                  │  列: GPIO 10,11,12,13                │
 * │  OLED SSD1306    │  SDA: GPIO 21, SCL: GPIO 22          │
 * │  状态LED         │  GPIO 14, 15, 16, 17                 │
 * │  蜂鸣器          │  GPIO 18                             │
 * │  门磁传感器      │  GPIO 38, 39, 40, 41                 │
 * └──────────────────┴──────────────────────────────────────┘
 */

#pragma once

#include "driver/ledc.h"
#include "driver/i2c.h"

// ============================================================
// 格口数量
// ============================================================
#define LOCKER_COUNT            4

// ============================================================
// 舵机配置 (控制锁的开关)
// ============================================================
#define SERVO_GPIO_0            1
#define SERVO_GPIO_1            2
#define SERVO_GPIO_2            4
#define SERVO_GPIO_3            5

static const int SERVO_GPIOS[LOCKER_COUNT] = {
    SERVO_GPIO_0, SERVO_GPIO_1, SERVO_GPIO_2, SERVO_GPIO_3
};

#define SERVO_LEDC_TIMER        LEDC_TIMER_0
#define SERVO_LEDC_MODE         LEDC_LOW_SPEED_MODE
#define SERVO_FREQ_HZ           50          // 50Hz PWM for servo
#define SERVO_RESOLUTION        LEDC_TIMER_14_BIT
#define SERVO_LOCKED_US         1000        // 1ms = 锁定位置 (0°)
#define SERVO_UNLOCKED_US       2000        // 2ms = 解锁位置 (90°)

// ============================================================
// 4x4 矩阵键盘配置
// ============================================================
#define KEYPAD_ROW_0            6
#define KEYPAD_ROW_1            7
#define KEYPAD_ROW_2            8
#define KEYPAD_ROW_3            9
#define KEYPAD_COL_0            10
#define KEYPAD_COL_1            11
#define KEYPAD_COL_2            12
#define KEYPAD_COL_3            13

#define KEYPAD_ROWS             4
#define KEYPAD_COLS             4
#define KEYPAD_DEBOUNCE_MS      50

static const int KEYPAD_ROW_GPIOS[KEYPAD_ROWS] = {
    KEYPAD_ROW_0, KEYPAD_ROW_1, KEYPAD_ROW_2, KEYPAD_ROW_3
};
static const int KEYPAD_COL_GPIOS[KEYPAD_COLS] = {
    KEYPAD_COL_0, KEYPAD_COL_1, KEYPAD_COL_2, KEYPAD_COL_3
};

// 键盘布局
// 1 2 3 A
// 4 5 6 B
// 7 8 9 C
// * 0 # D
static const char KEYPAD_MAP[KEYPAD_ROWS][KEYPAD_COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

// ============================================================
// OLED SSD1306 I2C 配置
// ============================================================
#define OLED_I2C_PORT           I2C_NUM_0
#define OLED_SDA_GPIO           21
#define OLED_SCL_GPIO           22
#define OLED_I2C_FREQ_HZ        400000
#define OLED_I2C_ADDR           0x3C
#define OLED_WIDTH              128
#define OLED_HEIGHT             64
#define OLED_PAGES              (OLED_HEIGHT / 8)

// ============================================================
// LED 状态指示灯配置 (每个格口一个, 高电平亮)
// ============================================================
#define LED_GPIO_0              14
#define LED_GPIO_1              15
#define LED_GPIO_2              16
#define LED_GPIO_3              17

static const int LED_GPIOS[LOCKER_COUNT] = {
    LED_GPIO_0, LED_GPIO_1, LED_GPIO_2, LED_GPIO_3
};

// ============================================================
// 蜂鸣器配置 (PWM驱动)
// ============================================================
#define BUZZER_GPIO             18
#define BUZZER_LEDC_TIMER       LEDC_TIMER_1
#define BUZZER_LEDC_CHANNEL     LEDC_CHANNEL_4
#define BUZZER_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BUZZER_FREQ_HZ          2000        // 2kHz 蜂鸣频率

// ============================================================
// 门磁传感器配置 (低电平=门关闭, 内部上拉)
// ============================================================
#define DOOR_GPIO_0             38
#define DOOR_GPIO_1             39
#define DOOR_GPIO_2             40
#define DOOR_GPIO_3             41

static const int DOOR_GPIOS[LOCKER_COUNT] = {
    DOOR_GPIO_0, DOOR_GPIO_1, DOOR_GPIO_2, DOOR_GPIO_3
};

// ============================================================
// 应用参数配置
// ============================================================

// 取件码长度 (纯数字)
#define CODE_LENGTH             6

// 最大失败尝试次数
#define MAX_FAILED_ATTEMPTS     3

// 锁定时间 (秒) - 超过最大失败次数后
#define LOCKOUT_DURATION_S      60

// 解锁后自动关锁超时 (秒)
#define AUTO_LOCK_TIMEOUT_S     30

// 取件码有效期 (小时)
#define CODE_EXPIRE_HOURS       72

// ============================================================
// WiFi 配置
// ============================================================

// AP 模式配置 (无STA连接时启用)
#define WIFI_AP_SSID            "SmartLocker_AP"
#define WIFI_AP_PASSWORD        "admin1234"
#define WIFI_AP_CHANNEL         6
#define WIFI_AP_MAX_CONN        4

// STA 模式默认配置 (可通过HTTP API修改并保存到NVS)
#define WIFI_STA_SSID_DEFAULT   ""
#define WIFI_STA_PASS_DEFAULT   ""
#define WIFI_STA_MAX_RETRY      5

// HTTP 服务器端口
#define HTTP_SERVER_PORT        80

// ============================================================
// NVS 存储配置
// ============================================================
#define NVS_NAMESPACE           "smart_locker"

// ============================================================
// FreeRTOS 任务配置
// ============================================================
#define KEYPAD_TASK_STACK       4096
#define DISPLAY_TASK_STACK      4096
#define LOCKER_TASK_STACK       4096

#define KEYPAD_TASK_PRIORITY    5
#define DISPLAY_TASK_PRIORITY   4
#define LOCKER_TASK_PRIORITY    6