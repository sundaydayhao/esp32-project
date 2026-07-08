# ESP32 项目合集

这是一个基于 ESP32 / ESP32-S3 和 ESP-IDF 的项目集合，包含学习示例、TinyML、AI 语音与视觉、IoT 设备、仪器工具、桌面显示和交互类应用。仓库中的多数目录都是相互独立的 ESP-IDF 工程；少数目录是项目组，内部再包含多个可单独构建的子工程。

## 仓库内容

### 学习与示例

| 路径 | 内容 |
| --- | --- |
| [learn](./learn/) | ESP32 学习套件，覆盖基础外设、传感器、显示、调试和 AI/ML 入门示例。 |
| [tinyml](./tinyml/) | ESP32-S3 TinyML 示例，包含 `hello_world`、`magic_wand`、`micro_speech`、`person_detection`。 |

### AI 与语音应用

| 路径 | 内容 |
| --- | --- |
| [ai-toy](./ai-toy/) | ESP32-S3 智能语音玩具，包含固件、Python 后端、ASR/LLM/TTS 流程和硬件说明。 |
| [ai-english-trainer](./ai-english-trainer/) | AI 英语口语训练器，集成录音、播放、按键、LED、WiFi 和 OpenAI 客户端。 |
| [ai-story-player](./ai-story-player/) | AI 睡前故事机，包含 ESP32 固件、Python 后端和 Web 页面。 |
| [ai-garbage-recognition](./ai-garbage-recognition/) | AI 智能垃圾分类系统，包含摄像头采集、在线/LLM 分类、语音提示、舵机/电机控制。 |
| [car-display/ai-player](./car-display/ai-player/) | CarPal 汽车小摆件，包含车速、IMU、语音、聊天和情绪响应相关模块。 |
| [desk-clock/xiaozhi-spi-lcd](./desk-clock/xiaozhi-spi-lcd/) | 基于小智方案的 SPI LCD AI 聊天/语音设备工程。 |

### 摄像头与视觉

| 路径 | 内容 |
| --- | --- |
| [magnifier](./magnifier/) | ESP32 电路板放大镜，基于摄像头采集和本地 Web/显示服务。 |
| [doorman](./doorman/) | ESP32-S3 门卫系统，包含摄像头、运动检测、图片服务、WiFi 和通知模块。 |
| [chaobiao](./chaobiao/) | ESP32-CAM 燃气表拍照推送工程，包含摄像头初始化、WiFi 和邮件/通知发送。 |
| [car-display/camera](./car-display/camera/) | 车辆左右盲区视觉显示系统，包含摄像头、LCD、盲区 UI 和转向灯输入。 |

### IoT 与智能设备

| 路径 | 内容 |
| --- | --- |
| [iot_env_monitor](./iot_env_monitor/) | 工业级 IoT 环境监测节点，包含多传感器采集、显示、告警、MQTT、Modbus、Web 和 OTA。 |
| [delivery-box](./delivery-box/) | ESP32-S3 智能快递柜，包含键盘、OLED、蜂鸣器、储物柜控制和 Web 管理。 |
| [medication-reminder](./medication-reminder/) | 老年人用药提醒系统，包含 RTC、提醒、蜂鸣器、显示、存储和 Web 配置。 |
| [human-fall-off](./human-fall-off/) | ESP32-S3 人体跌倒检测系统，包含 WiFi CSI、运动门控、事件检测和数据上传。 |
| [desk-clock/lcd1602](./desk-clock/lcd1602/) | LCD1602 桌面时钟，支持 NTP、天气、DHT11、蜂鸣器整点报时。 |
| [desk-clock/lcd12864](./desk-clock/lcd12864/) | LCD12864 时钟、天气、室内传感器与整点报时工程。 |
| [desk-clock/nt35510-rgb-lcd](./desk-clock/nt35510-rgb-lcd/) | ESP32-S3 + NT35510 RGB LCD 桌面时钟工程，包含 LVGL/Web 配网页面等内容。 |
| [desk-clock/weixue-touch-lcd-7](./desk-clock/weixue-touch-lcd-7/) | 7 英寸触摸屏桌面时钟工程，包含 LVGL 界面、Web 配网和音乐播放器说明。 |

### 仪器、显示与交互

| 路径 | 内容 |
| --- | --- |
| [oscilloscope](./oscilloscope/) | ESP32-S3 模拟示波器，包含 ADC 采样、WiFi AP、HTTP 服务和 Web 波形界面。 |
| [signal-generator](./signal-generator/) | ESP32-S3 信号发生器工程。 |
| [music-spectrum](./music-spectrum/) | ESP32-S3 音乐频谱播放器，包含音频播放、FFT 分析和 Web 控制界面。 |
| [game-stick](./game-stick/) | ESP32 GameStick，包含摇杆、WiFi AP、HTTP 服务和内置 Web 游戏页面。 |
| [shop-menu](./shop-menu/) | A 型桌面屏餐厅菜单显示控制器，包含 LCD 显示、Web 管理、WiFi 和内容存储。 |

### 文档与评估

| 路径 | 内容 |
| --- | --- |
| [课程设计.md](./课程设计.md) | 课程设计相关说明。 |
| [项目评估分类报告.md](./项目评估分类报告.md) | 项目分类与评估报告。 |
| [ESP32性能瓶颈与替代平台建议.md](./ESP32性能瓶颈与替代平台建议.md) | ESP32 性能瓶颈分析和替代平台建议。 |
| [oscilloscope/docs](./oscilloscope/docs/) | 示波器项目的架构、调用图、设计细节和图表文档。 |

## 目录形态

- 直接可构建工程：根目录下带 `CMakeLists.txt` 的项目，如 `iot_env_monitor`、`oscilloscope`、`shop-menu`、`delivery-box` 等。
- 子工程集合：`car-display`、`desk-clock`、`tinyml`、`learn` 等目录下包含多个独立工程，需要进入具体子目录构建。
- 固件加后端项目：`ai-toy`、`ai-story-player` 同时包含 ESP32 固件和 Python 后端/Web 资源，构建固件时进入对应 `firmware` 目录。
- Web 资源：多个项目包含 `web`、`spiffs_image` 或嵌入式 HTML/CSS/JS，用于本地配置、控制或数据展示。

## 开发环境

建议使用以下环境：

| 工具 | 建议版本 |
| --- | --- |
| ESP-IDF | v5.x，优先使用各子项目 README 或 `sdkconfig.defaults` 指定的版本要求 |
| Python | 3.8+ |
| CMake / Ninja | 随 ESP-IDF 安装 |
| 串口驱动 | 按开发板 USB-UART 芯片安装 |
| 编辑器 | VS Code + ESP-IDF 插件，或 ESP-IDF 命令行环境 |

安装 ESP-IDF 后，先在终端加载环境：

```bash
. $IDF_PATH/export.sh
```

Windows PowerShell 通常使用 ESP-IDF 安装器提供的快捷入口，或执行对应的 `export.ps1`。

## 构建与烧录

进入具体工程目录后构建。不要在仓库根目录直接执行 `idf.py build`。

```bash
cd <工程目录>
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COM3 flash monitor
```

如果项目使用 ESP32 而不是 ESP32-S3，把目标改为：

```bash
idf.py set-target esp32
```

Linux/macOS 串口示例：

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

部分项目包含自定义分区表、SPIFFS 资源或后端服务，常见文件如下：

- `sdkconfig.defaults`：默认工程配置。
- `partitions.csv`：分区表。
- `main/Kconfig.projbuild`：`menuconfig` 中的项目参数。
- `main/idf_component.yml`：ESP-IDF Component Manager 依赖。
- `web/`、`spiffs_image/`：需要打包或嵌入的前端资源。
- `backend/`、`server/`：Python 后端服务。

## 配置提示

- WiFi SSID、密码、API Key、服务器地址等通常在 `idf.py menuconfig`、`main/config.h`、`main/app_config.h` 或子项目 README 中配置。
- 摄像头、LCD、I2S 麦克风/功放、键盘、蜂鸣器、传感器等引脚以对应项目源码和 README 为准。
- 带 AI 云端能力的项目可能需要 OpenAI 或其他模型服务的 API Key。
- 带 Web 管理界面的项目通常会在日志中输出 AP 信息或设备 IP。

## 常见硬件

仓库中出现较多的硬件模块包括：

- 主控：ESP32、ESP32-S3、ESP32-CAM。
- 显示：LCD1602、LCD12864、SSD1306 OLED、SPI LCD、NT35510 RGB LCD、7 英寸触摸屏。
- 摄像头：OV2640 / ESP32-CAM 相关模块。
- 音频：I2S 麦克风、I2S 功放、扬声器。
- 传感器：DHT11、BME280、SHT31、PMS5003、MH-Z19、IMU、超声波/距离类模块等。
- 交互与执行器：按键、矩阵键盘、摇杆、蜂鸣器、RGB LED、舵机/电机、门锁/储物柜执行机构。

## 使用建议

1. 先读目标子项目 README，确认开发板型号、外设接线和配置项。
2. 进入有 `CMakeLists.txt` 的工程根目录。
3. 执行 `idf.py set-target`，目标芯片按项目硬件选择。
4. 运行 `idf.py menuconfig` 补齐 WiFi、密钥、引脚或服务地址。
5. 编译、烧录并通过 `monitor` 查看启动日志。

## 相关资源

- [ESP-IDF 官方文档](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/)
- [Espressif 官网](https://www.espressif.com/zh-hans)
- [ESP-IDF Component Registry](https://components.espressif.com/)
- [LVGL](https://lvgl.io/)
