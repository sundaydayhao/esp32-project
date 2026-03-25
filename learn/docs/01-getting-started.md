# 快速入门指南

## ESP32 简介

ESP32 是乐鑫科技（Espressif）推出的一款低成本、低功耗的系统级芯片（SoC），集成了 Wi-Fi 和蓝牙功能，广泛应用于物联网（IoT）领域。

### 主要特性

| 特性 | 参数 |
|------|------|
| CPU | Xtensa LX6 双核，最高 240 MHz |
| RAM | 520 KB SRAM |
| Flash | 4 MB（外部，可扩展） |
| Wi-Fi | 802.11 b/g/n |
| 蓝牙 | BT 4.2 + BLE |
| GPIO | 34 个可编程 GPIO |
| ADC | 12 位，18 通道 |
| DAC | 8 位，2 通道 |
| UART | 3 个 |
| SPI | 4 个 |
| I2C | 2 个 |
| I2S | 2 个 |
| PWM | 16 通道 |
| 工作电压 | 3.3V |
| 工作温度 | -40°C ~ 85°C |

## ESP-IDF 框架介绍

ESP-IDF（Espressif IoT Development Framework）是乐鑫官方提供的开发框架，基于 FreeRTOS 实时操作系统，提供了丰富的 API 和组件。

### ESP-IDF vs Arduino

| 对比项 | ESP-IDF | Arduino |
|--------|---------|---------|
| 性能 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| 功能完整性 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| 学习曲线 | 较陡 | 平缓 |
| 代码控制 | 精细 | 简单 |
| 适合场景 | 产品开发 | 快速原型 |
| 实时性 | 好 | 一般 |

## 第一个程序：Hello World

### 项目结构

```
hello-world/
├── CMakeLists.txt          # 根 CMake 文件
├── main/
│   ├── CMakeLists.txt      # main 组件 CMake 文件
│   └── main.c              # 主程序
└── sdkconfig               # 配置文件（编译后生成）
```

### 代码示例

**main/main.c**
```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "HelloWorld";

void app_main(void)
{
    ESP_LOGI(TAG, "Hello, ESP32!");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    
    int count = 0;
    while (1) {
        ESP_LOGI(TAG, "Running... count = %d", count++);
        vTaskDelay(pdMS_TO_TICKS(1000));  // 延时 1 秒
    }
}
```

**CMakeLists.txt**（根目录）
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hello-world)
```

**main/CMakeLists.txt**
```cmake
idf_component_register(SRCS "main.c"
                        INCLUDE_DIRS ".")
```

### 编译和烧录

```bash
# 1. 设置目标芯片
idf.py set-target esp32

# 2. 配置项目（可选）
idf.py menuconfig

# 3. 编译
idf.py build

# 4. 烧录（替换 COM3 为实际串口）
idf.py -p COM3 flash

# 5. 监视串口输出
idf.py -p COM3 monitor

# 4+5 合并命令
idf.py -p COM3 flash monitor
```

## 日志系统

ESP-IDF 提供了强大的日志系统：

```c
#include "esp_log.h"

static const char *TAG = "MyApp";

ESP_LOGE(TAG, "错误信息");    // Error   - 红色
ESP_LOGW(TAG, "警告信息");    // Warning - 黄色
ESP_LOGI(TAG, "普通信息");    // Info    - 绿色
ESP_LOGD(TAG, "调试信息");    // Debug   - 白色
ESP_LOGV(TAG, "详细信息");    // Verbose - 白色
```

## 常用快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+]` | 退出 idf.py monitor |
| `Ctrl+T Ctrl+R` | 重启设备 |
| `Ctrl+T Ctrl+H` | 显示帮助 |

## 下一步

- 查看 [ESP32 引脚说明](02-esp32-pinout.md)
- 搭建 [ESP-IDF 开发环境](03-esp-idf-setup.md)
- 开始 [第一个 LED 闪烁实验](../01-basics/01-blink/)