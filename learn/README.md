# ESP32 学习套件 (ESP-IDF 框架)

> 🚀 面向科技创客和学生的 ESP32 完整学习资料，基于 **ESP-IDF** 框架

## 📋 目录结构

```
esp32-learn/
├── 01-basics/              # 基础外设
│   ├── 01-blink/           # LED 闪烁
│   ├── 02-gpio-input/      # GPIO 输入（按键）
│   ├── 03-uart/            # UART 串口通信
│   ├── 04-pwm/             # PWM 脉宽调制（呼吸灯）
│   ├── 05-adc/             # ADC 模数转换
│   └── 06-timer/           # 定时器（硬件/软件）
│
├── 02-sensors/             # 传感器
│   ├── 01-dht11/           # DHT11 温湿度传感器
│   ├── 02-ds18b20/         # DS18B20 单总线温度传感器
│   ├── 03-bmp280/          # BMP280 气压/温度传感器（I2C）
│   └── 04-hcsr04/          # HC-SR04 超声波测距
│
├── 03-communication/       # 通信协议
│   ├── 01-i2c/             # I2C 总线扫描与读写
│   ├── 02-spi/             # SPI 全双工通信
│   ├── 03-wifi-sta/        # WiFi Station 模式
│   ├── 04-wifi-ap/         # WiFi Access Point 热点
│   └── 05-bluetooth-ble/   # 蓝牙 BLE GATT 服务器
│
├── 04-display/             # 显示屏
│   ├── 01-oled-ssd1306/    # SSD1306 OLED 128x64（I2C）
│   └── 02-lcd1602-i2c/     # LCD1602 液晶屏（I2C/PCF8574）
│
├── 05-iot/                 # 物联网
│   ├── 01-http-server/     # HTTP Web 服务器（控制 LED）
│   ├── 02-http-client/     # HTTP 客户端（GET/POST）
│   └── 03-mqtt/            # MQTT 消息队列（发布/订阅）
│
├── 06-advanced/            # 进阶功能
│   ├── 01-freertos-tasks/  # FreeRTOS 多任务
│   ├── 02-freertos-queue/  # FreeRTOS 队列（生产者-消费者）
│   ├── 03-nvs-storage/     # NVS 非易失性存储
│   ├── 04-deep-sleep/      # 深度睡眠（低功耗）
│   └── 05-watchdog/        # 看门狗定时器
│
├── 07-projects/            # 综合项目
│   ├── 01-weather-station/ # WiFi 气象站（DHT11 + HTTP）
│   └── 02-smart-led/       # WiFi 智能 LED 控制器（PWM + HTTP）
│
├── 08-ai-ml/               # 🤖 AI/ML 边缘智能
│   ├── 01-tflite-hello-world/  # TFLite Micro 神经网络推理
│   ├── 02-anomaly-detection/   # 传感器数据异常检测（Z-Score）
│   ├── 03-neural-network/      # 纯C神经网络推理引擎（MLP）
│   └── 04-kalman-filter/       # 卡尔曼滤波器（传感器融合）
│
└── docs/                   # 文档
    ├── 01-getting-started.md
    ├── 02-esp32-pinout.md
    ├── 03-esp-idf-setup.md
    ├── 04-project-structure.md
    ├── 05-debugging.md
    └── 06-ai-ml.md         # AI/ML 开发指南
```

## 🛠️ 开发环境

| 工具 | 版本 | 说明 |
|------|------|------|
| ESP-IDF | v5.x | 官方开发框架 |
| CMake | ≥ 3.16 | 构建系统 |
| Python | ≥ 3.8 | ESP-IDF 依赖 |
| VS Code | 最新 | 推荐编辑器 |
| ESP-IDF Extension | 最新 | VS Code 插件 |

## ⚡ 快速开始

### 1. 安装 ESP-IDF

```bash
# Windows：下载 ESP-IDF 离线安装包
# https://dl.espressif.com/dl/esp-idf/

# Linux/macOS：
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32
. ./export.sh
```

### 2. 编译并烧录示例

```bash
# 进入示例目录
cd 01-basics/01-blink

# 设置目标芯片
idf.py set-target esp32

# 编译
idf.py build

# 烧录（替换 COM3 为实际端口）
idf.py -p COM3 flash

# 监视串口输出
idf.py -p COM3 monitor

# 一键编译+烧录+监视
idf.py -p COM3 flash monitor
```

### 3. VS Code 开发

安装 **ESP-IDF** 插件后，可以直接在 VS Code 中：
- 点击底部状态栏的 `ESP-IDF: Build` 编译
- 点击 `ESP-IDF: Flash` 烧录
- 点击 `ESP-IDF: Monitor` 查看串口

## 📚 示例说明

### 01-basics 基础外设

| 示例 | 功能 | 关键 API |
|------|------|----------|
| 01-blink | LED 闪烁 | `gpio_set_level()` |
| 02-gpio-input | 按键检测（中断） | `gpio_isr_handler_add()` |
| 03-uart | 串口收发 | `uart_write_bytes()` |
| 04-pwm | 呼吸灯 | `ledc_set_duty()` |
| 05-adc | 读取电位器 | `adc_oneshot_read()` |
| 06-timer | 定时器回调 | `esp_timer_create()` |

### 02-sensors 传感器

| 示例 | 传感器 | 接口 |
|------|--------|------|
| 01-dht11 | 温湿度 | 单总线 |
| 02-ds18b20 | 精密温度 | 1-Wire |
| 03-bmp280 | 气压/温度 | I2C |
| 04-hcsr04 | 超声波测距 | GPIO |

### 03-communication 通信

| 示例 | 协议 | 说明 |
|------|------|------|
| 01-i2c | I2C | 扫描总线设备 |
| 02-spi | SPI | 全双工自回环测试 |
| 03-wifi-sta | WiFi | 连接路由器 |
| 04-wifi-ap | WiFi | 创建热点 |
| 05-bluetooth-ble | BLE | GATT 服务器 |

### 05-iot 物联网

| 示例 | 功能 | 测试方法 |
|------|------|----------|
| 01-http-server | Web 控制 LED | 浏览器访问 IP |
| 02-http-client | 发送 HTTP 请求 | httpbin.org |
| 03-mqtt | 消息发布/订阅 | MQTTX 工具 |

### 08-ai-ml 边缘 AI/ML

| 示例 | 功能 | 算法/框架 |
|------|------|----------|
| 01-tflite-hello-world | 神经网络推理正弦波 | TFLite Micro / 手写MLP |
| 02-anomaly-detection | 传感器异常检测 | Z-Score 滑动窗口 |
| 03-neural-network | MLP 分类器（XOR/鸢尾花） | 纯C推理引擎 |
| 04-kalman-filter | 传感器去噪与融合 | 卡尔曼/互补滤波 |

### 07-projects 综合项目

#### 气象站
- 硬件：DHT11 + LED
- 功能：实时温湿度 + Web 界面
- 访问：`http://<ESP32-IP>/`

#### 智能 LED
- 硬件：RGB LED（GPIO25/26/27）
- 功能：Web 控制颜色/亮度/灯效
- 灯效：常亮/呼吸/闪烁/彩虹

## 🔌 常用硬件连接

```
ESP32 引脚说明：
┌─────────────────────────────────┐
│  GPIO2  - 板载 LED（低电平亮）   │
│  GPIO0  - BOOT 按键             │
│  GPIO21 - I2C SDA               │
│  GPIO22 - I2C SCL               │
│  GPIO23 - SPI MOSI              │
│  GPIO19 - SPI MISO              │
│  GPIO18 - SPI SCK               │
│  GPIO5  - SPI CS                │
│  GPIO1  - UART0 TX（串口）       │
│  GPIO3  - UART0 RX（串口）       │
│  GPIO34~39 - 仅输入（ADC）       │
└─────────────────────────────────┘
```

## 📖 学习路径

```
初学者路径：
01-blink → 02-gpio-input → 03-uart → 04-pwm → 05-adc

传感器路径：
01-dht11 → 03-bmp280 → 04-hcsr04

通信路径：
01-i2c → 03-wifi-sta → 05-iot/01-http-server → 05-iot/03-mqtt

进阶路径：
06-advanced/01-freertos-tasks → 02-queue → 03-nvs → 04-deep-sleep

项目路径：
07-projects/01-weather-station → 02-smart-led

AI/ML 路径：
08-ai-ml/04-kalman-filter → 02-anomaly-detection → 03-neural-network → 01-tflite-hello-world
```

## 🔧 常见问题

**Q: 编译报错 "IDF_PATH not set"**
```bash
# 需要先激活 ESP-IDF 环境
. $IDF_PATH/export.sh  # Linux/macOS
%IDF_PATH%\export.bat  # Windows
```

**Q: 烧录失败**
- 检查 USB 驱动（CP2102/CH340）
- 按住 BOOT 键再烧录
- 检查串口号是否正确

**Q: VS Code 头文件报错（红色波浪线）**
- 这是 IntelliSense 配置问题，不影响编译
- 安装 ESP-IDF VS Code 插件后会自动配置
- 或手动配置 `.vscode/c_cpp_properties.json`

## 🤖 AI/ML 快速入门

ESP32 支持在设备端运行轻量级 AI 模型（边缘 AI / TinyML）：

```bash
# 运行异常检测示例（无需额外依赖）
cd 08-ai-ml/02-anomaly-detection
idf.py set-target esp32
idf.py -p COM3 flash monitor

# 运行神经网络推理示例
cd 08-ai-ml/03-neural-network
idf.py -p COM3 flash monitor
```

| 框架 | 适用场景 | 安装方式 |
|------|---------|---------|
| TFLite Micro | 通用神经网络推理 | `idf.py add-dependency esp-tflite-micro` |
| ESP-DL | ESP32-S3 优化推理 | `idf.py add-dependency esp-dl` |
| 纯C实现 | 小型MLP，无依赖 | 本项目内置 |

详细文档：[docs/06-ai-ml.md](docs/06-ai-ml.md)

## 📝 许可证

MIT License - 自由使用、修改和分发

---
*ESP32 学习套件 | 基于 ESP-IDF v5.x | 适合创客和学生学习*