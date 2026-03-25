ba# ⚡ PlatformIO 配置指南

## 为什么选择 PlatformIO？

- 更强大的代码补全和智能提示
- 内置库管理器
- 支持多种框架（Arduino、ESP-IDF）
- 更好的项目管理
- 内置调试器

## 安装步骤

### 1. 安装 VS Code
下载：https://code.visualstudio.com/

### 2. 安装 PlatformIO 扩展
- 打开 VS Code
- 点击左侧扩展图标（Ctrl+Shift+X）
- 搜索 "PlatformIO IDE"
- 点击安装
- 重启 VS Code

### 3. 创建新项目
- 点击 PlatformIO 图标（蚂蚁头像）
- 点击 "New Project"
- 填写项目名称
- 选择开发板：ESP32 Dev Module
- 选择框架：Arduino
- 点击 Finish

## 项目结构

```
my_project/
├── .pio/               # PlatformIO 构建目录
├── .vscode/            # VS Code 配置
├── include/            # 头文件
├── lib/                # 本地库
├── src/
│   └── main.cpp        # 主程序
├── test/               # 单元测试
└── platformio.ini      # 项目配置
```

## platformio.ini 配置示例

### 基础配置
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600
```

### 带库依赖的配置
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600

; 库依赖
lib_deps = 
    adafruit/DHT sensor library @ ^1.4.4
    adafruit/Adafruit SSD1306 @ ^2.5.7
    adafruit/Adafruit GFX Library @ ^1.11.5
    bblanchon/ArduinoJson @ ^6.21.3
    knolleary/PubSubClient @ ^2.8
    paulstoffregen/OneWire @ ^2.3.7
    milesburton/DallasTemperature @ ^3.11.0

; 构建标志
build_flags = 
    -DCORE_DEBUG_LEVEL=0
    -DBOARD_HAS_PSRAM
```

### 多环境配置
```ini
[platformio]
default_envs = esp32dev

[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200

[env:esp32s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
```

## 常用命令

| 操作 | 快捷键 / 命令 |
|------|--------------|
| 编译 | Ctrl+Alt+B |
| 上传 | Ctrl+Alt+U |
| 串口监视器 | Ctrl+Alt+S |
| 清理 | PlatformIO: Clean |
| 更新库 | PlatformIO: Update All Libraries |

## main.cpp 模板

```cpp
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("PlatformIO + ESP32 启动成功！");
}

void loop() {
  Serial.println("Hello from PlatformIO!");
  delay(1000);
}
```

## 调试配置

在 `platformio.ini` 中添加：
```ini
debug_tool = esp-prog
debug_init_break = tbreak setup
```

## 常见问题

### 上传失败
```ini
; 尝试降低上传速度
upload_speed = 115200
```

### 串口乱码
```ini
; 确认波特率
monitor_speed = 115200
```

### 库找不到
```bash
# 在 PlatformIO 终端中运行
pio lib search "库名"
pio lib install "库名"