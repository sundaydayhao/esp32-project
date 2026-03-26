# ESP32 项目合集

基于 ESP32/ESP32-S3 的嵌入式项目合集，涵盖 AI 应用、IoT 物联网、工具仪器、智能家居、多媒体等多个领域，使用 ESP-IDF 框架开发。

## 📁 项目目录

### 🎓 学习资料

| 项目 | 描述 |
|------|------|
| [01-learn](./01-learn/) | ESP32 入门学习项目，包含 GPIO、PWM、I2C/SPI、WiFi、LVGL 等基础示例 |
| [tinyml](./tinyml/) | TinyML 机器学习示例，包含 hello_world、magic_wand、micro_speech、person_detection |

---

### 🔧 工具仪器

| 项目 | 描述 |
|------|------|
| [02-wifi_analyzer](./02-wifi_analyzer/) | WiFi 信号分析仪，扫描周围网络并图形化显示 SSID、RSSI、信道、加密方式 |
| [05-signal-generator](./05-signal-generator/) | 基于 ESP32 的信号发生器，支持多种波形输出 |
| [06-oscilloscope](./06-oscilloscope/) | 基于 ESP32 的数字示波器，支持 Web 界面实时波形显示 |
| [rtlsdr](./rtlsdr/) | RTL-SDR 软件无线电接收项目 |
| [magnifier](./magnifier/) | 数字放大镜，基于 ESP32-S3 + OV2640 摄像头，支持 2x/4x/8x 多级放大 |

---

### 🤖 AI 应用

| 项目 | 描述 |
|------|------|
| [ai-toy](./ai-toy/) | AI 智能玩具，基于小智方案，支持离线唤醒词、ASR 语音识别、LLM 对话、TTS 语音合成 |
| [xiaozhi](./xiaozhi/) | 小智 AI 语音助手，支持百度/阿里/讯飞/OpenAI 等多平台 |
| [ai-english-trainer](./ai-english-trainer/) | AI 英语口语训练器，基于 ESP32-S3 的智能英语学习设备 |
| [ai-garbage-recognition](./ai-garbage-recognition/) | AI 垃圾分类识别，基于图像识别技术自动分类垃圾 |
| [ai-story-player](./ai-story-player/) | AI 睡前故事机，孩子说出关键词后 AI 实时生成专属故事并朗读，屏幕同步显示插图 |
| [car-display](./car-display/) | CarPal 汽车小摆件，实时车速显示 + 共情语音响应 + 语音聊天，支持 8 种情感状态 |

---

### 🏠 智能家居 / IoT

| 项目 | 描述 |
|------|------|
| [iot_env_monitor](./iot_env_monitor/) | IoT 环境监测系统，监测温湿度、光照强度，通过 MQTT 上传云端并提供 Web 可视化 |
| [delivery-box](./delivery-box/) | 智能快递柜，4 格口设计，支持键盘取件码、OLED 显示、WiFi 远程管理 |
| [desk-clock](./desk-clock/) | 桌面时钟，基于 ESP32 的多功能桌面时钟 |
| [doorman](./doorman/) | 智能门卫系统，基于 ESP32-S3 的门禁管理 |
| [smart-door-lock](./smart-door-lock/) | 智能门锁，支持多种解锁方式 |
| [chaobiao](./chaobiao/) | ESP32-CAM 燃气表拍照推送，定时拍照并推送到企业微信或 ServerChan |

---

### 🎮 多媒体 / 娱乐

| 项目 | 描述 |
|------|------|
| [04-photo_album](./04-photo_album/) | 电子相册，从 SD 卡读取 JPEG 图片并在 LCD 上自动轮播 |
| [music-spectrum](./music-spectrum/) | 音乐频谱显示，实时音频频谱可视化 |
| [gamestick](./gamestick/) | 游戏手柄，基于 ESP32 的蓝牙游戏控制器 |
| [shop-menu](./shop-menu/) | 商店菜单展示系统，基于 ESP32 的电子菜单/价目表 |

---

### 📡 传感器 / 显示

| 项目 | 描述 |
|------|------|
| [03-radar](./03-radar/) | 超声波雷达，使用舵机旋转 HC-SR04 进行 180° 扫描并实时显示雷达图像 |
| [computer_motherboard_display](./computer_motherboard_display/) | 电脑主板副屏，通过串口接收主机数据并在 LCD 上显示系统状态 |
| [esp32-s3-nt35510-example](./esp32-s3-nt35510-example/) | ESP32-S3 + NT35510 显示屏驱动示例 |

---

## 🛠️ 开发环境

| 工具 | 版本要求 |
|------|---------|
| ESP-IDF | v5.0 及以上 |
| Python | 3.8+ |
| CMake | 3.16+ |
| VS Code | 推荐配合 ESP-IDF 插件使用 |

### 安装 ESP-IDF

```bash
# 克隆 ESP-IDF
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.3

# 安装工具链
./install.sh esp32,esp32s3

# 设置环境变量
. ./export.sh
```

---

## 🚀 快速开始

每个子项目均为独立的 ESP-IDF 工程，进入对应目录后按以下步骤操作：

```bash
# 1. 进入项目目录
cd <项目名称>

# 2. 设置目标芯片（根据项目选择 esp32 或 esp32s3）
idf.py set-target esp32s3

# 3. 配置项目参数（WiFi、引脚等）
idf.py menuconfig

# 4. 编译
idf.py build

# 5. 烧录并监视串口输出
idf.py -p <串口号> flash monitor
```

> **Windows 串口示例：** `idf.py -p COM3 flash monitor`  
> **Linux/macOS 串口示例：** `idf.py -p /dev/ttyUSB0 flash monitor`

---

## 📦 常用硬件

本项目合集中使用的主要硬件模块：

- **主控芯片：** ESP32、ESP32-S3
- **显示屏：** ST7789（240×240）、NT35510（800×480）、ILI9341、SSD1306 OLED
- **摄像头：** OV2640
- **麦克风：** INMP441（I2S）
- **功放：** MAX98357A（I2S）
- **传感器：** DHT22（温湿度）、BH1750（光照）、HC-SR04（超声波）、MPU6050（姿态）
- **存储：** SD 卡模块
- **其他：** SG90 舵机、WS2812B RGB LED、4×4 矩阵键盘

---

## 📂 项目结构

```
esp32-project/
├── 01-learn/               # ESP32 入门学习
├── 02-wifi_analyzer/       # WiFi 分析仪
├── 03-radar/               # 超声波雷达
├── 04-photo_album/         # 电子相册
├── 05-signal-generator/    # 信号发生器
├── 06-oscilloscope/        # 数字示波器
├── ai-english-trainer/     # AI 英语训练器
├── ai-garbage-recognition/ # AI 垃圾分类
├── ai-story-player/        # AI 故事机
├── ai-toy/                 # AI 智能玩具
├── car-display/            # 汽车小摆件
├── chaobiao/               # 燃气表拍照推送
├── computer_motherboard_display/ # 主板副屏
├── delivery-box/           # 智能快递柜
├── desk-clock/             # 桌面时钟
├── doorman/                # 智能门卫
├── esp32-s3-nt35510-example/ # NT35510 示例
├── gamestick/              # 游戏手柄
├── iot_env_monitor/        # IoT 环境监测
├── magnifier/              # 数字放大镜
├── music-spectrum/         # 音乐频谱
├── rtlsdr/                 # RTL-SDR 软件无线电
├── shop-menu/              # 商店菜单
├── smart-door-lock/        # 智能门锁
├── tinyml/                 # TinyML 示例
└── xiaozhi/                # 小智 AI 助手
```

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

1. Fork 本仓库
2. 创建特性分支：`git checkout -b feature/your-feature`
3. 提交更改：`git commit -m 'Add some feature'`
4. 推送分支：`git push origin feature/your-feature`
5. 提交 Pull Request

---

## 📄 许可证

本项目采用 [MIT License](LICENSE) 开源协议。

---

## 🔗 相关资源

- [ESP-IDF 官方文档](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/)
- [乐鑫官网](https://www.espressif.com/zh-hans)
- [LVGL 图形库](https://lvgl.io/)
- [ESP32 技术规格书](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_cn.pdf)