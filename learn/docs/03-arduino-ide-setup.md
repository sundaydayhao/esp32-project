# 🔧 Arduino IDE 配置指南

## 安装步骤

### 1. 下载 Arduino IDE 2.x
访问 https://www.arduino.cc/en/software 下载最新版本

### 2. 安装 ESP32 开发板支持

打开 Arduino IDE → 文件 → 首选项

在"附加开发板管理器网址"中添加：
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

然后：工具 → 开发板 → 开发板管理器 → 搜索 "esp32" → 安装

### 3. 选择开发板和端口

- 工具 → 开发板 → ESP32 Arduino → **ESP32 Dev Module**
- 工具 → 端口 → 选择对应 COM 口

### 4. 推荐设置

| 参数 | 推荐值 |
|------|--------|
| Upload Speed | 921600 |
| CPU Frequency | 240MHz |
| Flash Frequency | 80MHz |
| Flash Mode | QIO |
| Flash Size | 4MB |
| Partition Scheme | Default 4MB |
| Core Debug Level | None |

## 常用库安装

工具 → 管理库，搜索并安装以下库：

```
DHT sensor library by Adafruit
Adafruit SSD1306
Adafruit GFX Library
LiquidCrystal I2C by Frank de Brabander
ArduinoJson by Benoit Blanchon
PubSubClient by Nick O'Leary
OneWire by Paul Stoffregen
DallasTemperature by Miles Burton
MPU6050 by Electronic Cats
ESP32Servo by Kevin Harrington
WebSockets by Markus Sattler
```

## 代码模板

```cpp
// ESP32 基础代码模板
// 作者: ESP32学习套件
// 日期: 2024

// ============ 引入库 ============
#include <Arduino.h>

// ============ 引脚定义 ============
#define LED_PIN     2    // 内置LED
#define BUTTON_PIN  0    // 按键

// ============ 全局变量 ============
bool ledState = false;

// ============ 初始化 ============
void setup() {
  // 串口初始化
  Serial.begin(115200);
  Serial.println("\n=== ESP32 程序启动 ===");
  
  // 引脚初始化
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  Serial.println("初始化完成！");
}

// ============ 主循环 ============
void loop() {
  // 在这里写你的代码
  
  delay(10); // 避免看门狗超时
}
```

## 调试技巧

### 串口调试
```cpp
Serial.println("普通输出");
Serial.printf("格式化输出: %d, %.2f\n", intVal, floatVal);
Serial.print("不换行输出");
```

### 查看内存
```cpp
Serial.printf("可用堆内存: %d bytes\n", ESP.getFreeHeap());
Serial.printf("最大可用块: %d bytes\n", ESP.getMaxAllocHeap());
```

### 查看芯片信息
```cpp
Serial.printf("芯片型号: %s\n", ESP.getChipModel());
Serial.printf("芯片版本: %d\n", ESP.getChipRevision());
Serial.printf("CPU频率: %d MHz\n", ESP.getCpuFreqMHz());
Serial.printf("Flash大小: %d bytes\n", ESP.getFlashChipSize());