# ESP-IDF 项目结构说明

## 标准项目结构

```
my-project/
├── CMakeLists.txt          # 根 CMake 配置文件
├── sdkconfig               # 项目配置（由 menuconfig 生成）
├── sdkconfig.defaults      # 默认配置（可选，提交到版本控制）
├── partitions.csv          # 分区表（可选）
├── main/                   # 主组件（必须）
│   ├── CMakeLists.txt      # main 组件 CMake 配置
│   ├── main.c              # 主程序入口
│   └── Kconfig.projbuild   # 项目配置选项（可选）
├── components/             # 自定义组件（可选）
│   └── my-component/
│       ├── CMakeLists.txt
│       ├── include/
│       │   └── my-component.h
│       └── my-component.c
└── build/                  # 编译输出目录（自动生成）
```

## CMakeLists.txt 详解

### 根目录 CMakeLists.txt

```cmake
# 最低 CMake 版本要求
cmake_minimum_required(VERSION 3.16)

# 包含 ESP-IDF 项目 CMake 文件
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 项目名称
project(my-project)
```

### main/CMakeLists.txt

```cmake
# 注册 main 组件
idf_component_register(
    SRCS "main.c" "helper.c"        # 源文件列表
    INCLUDE_DIRS "."                 # 头文件目录
    REQUIRES driver nvs_flash        # 依赖的组件
)
```

### 自定义组件 CMakeLists.txt

```cmake
idf_component_register(
    SRCS "my-component.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
    PRIV_REQUIRES esp_wifi
)
```

## 入口函数

ESP-IDF 程序的入口是 `app_main()` 函数：

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    // 程序从这里开始执行
    // app_main 运行在 main task 中
    // 可以创建其他任务，也可以直接在这里运行
    
    while (1) {
        // 主循环
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // 如果 app_main 返回，main task 会被删除
}
```

## 常用 idf.py 命令

```bash
# 设置目标芯片
idf.py set-target esp32          # ESP32
idf.py set-target esp32s2        # ESP32-S2
idf.py set-target esp32s3        # ESP32-S3
idf.py set-target esp32c3        # ESP32-C3

# 配置
idf.py menuconfig                # 图形化配置界面

# 编译
idf.py build                     # 编译项目
idf.py size                      # 查看固件大小
idf.py size-components           # 查看各组件大小

# 烧录
idf.py -p PORT flash             # 烧录固件
idf.py -p PORT erase-flash       # 擦除 Flash
idf.py -p PORT flash monitor     # 烧录并监视

# 监视
idf.py -p PORT monitor           # 串口监视（115200 baud）

# 清理
idf.py clean                     # 清理编译文件
idf.py fullclean                 # 完全清理（包括 sdkconfig）

# 组件管理
idf.py add-dependency "idf-component-manager"  # 添加组件依赖
```

## 分区表

ESP32 Flash 默认分区表：

```
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 1M,
```

自定义分区表示例（partitions.csv）：

```csv
# Name,   Type, SubType,  Offset,   Size,    Flags
nvs,      data, nvs,      0x9000,   0x4000,
otadata,  data, ota,      0xd000,   0x2000,
phy_init, data, phy,      0xf000,   0x1000,
factory,  app,  factory,  0x10000,  1M,
ota_0,    app,  ota_0,    0x110000, 1M,
ota_1,    app,  ota_1,    0x210000, 1M,
storage,  data, spiffs,   0x310000, 0xF0000,
```

## 组件依赖

常用 ESP-IDF 内置组件：

| 组件 | 功能 |
|------|------|
| `driver` | GPIO、UART、SPI、I2C 等驱动 |
| `esp_wifi` | WiFi 功能 |
| `esp_bt` | 蓝牙功能 |
| `nvs_flash` | NVS 非易失存储 |
| `esp_http_server` | HTTP 服务器 |
| `esp_http_client` | HTTP 客户端 |
| `mqtt` | MQTT 客户端 |
| `esp_ota_ops` | OTA 升级 |
| `spiffs` | SPIFFS 文件系统 |
| `fatfs` | FAT 文件系统 |
| `esp_adc` | ADC 驱动 |
| `esp_timer` | 高精度定时器 |
| `esp_sleep` | 睡眠模式 |
| `esp_log` | 日志系统 |

## sdkconfig.defaults

可以创建 `sdkconfig.defaults` 文件来设置默认配置：

```ini
# 设置 CPU 频率为 240MHz
CONFIG_ESP32_DEFAULT_CPU_FREQ_240=y

# 设置 Flash 大小为 4MB
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y

# 启用详细日志
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y

# 设置 FreeRTOS tick 频率
CONFIG_FREERTOS_HZ=1000