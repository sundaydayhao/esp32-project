# 🤖 AI玩具 - 基于ESP32-S3的智能语音助手

> 基于虾哥小智方案，使用ESP32-S3开发的AI语音玩具

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.1+-blue)](https://docs.espressif.com/projects/esp-idf/)
[![License](https://img.shields.io/badge/License-MIT-green)](LICENSE)

## 📖 项目简介

本项目是一个基于ESP32-S3的AI语音玩具，参考虾哥小智开源方案实现。支持：

- 🎤 **语音唤醒** - 支持离线唤醒词检测（"你好小智"）
- 🗣️ **语音识别** - 实时语音转文字（ASR）
- 🧠 **AI对话** - 接入大语言模型（LLM）进行智能对话
- 🔊 **语音合成** - 文字转语音（TTS）播放回复
- 💡 **状态指示** - RGB LED显示工作状态
- 📡 **WiFi连接** - 通过WebSocket与后端服务通信

## 🏗️ 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                    ESP32-S3 固件                          │
│                                                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐  │
│  │ 唤醒词   │  │ 音频采集  │  │ WebSocket│  │  TTS   │  │
│  │ 检测     │→ │ (I2S MIC)│→ │ 客户端   │→ │ 播放   │  │
│  └──────────┘  └──────────┘  └──────────┘  └────────┘  │
│                                    ↕                     │
└────────────────────────────────────┼────────────────────┘
                                     │ WiFi / WebSocket
                                     ↓
┌─────────────────────────────────────────────────────────┐
│                    后端服务器 (Python)                    │
│                                                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐              │
│  │  ASR     │→ │  LLM     │→ │  TTS     │              │
│  │ 语音识别  │  │ 大模型   │  │ 语音合成  │              │
│  └──────────┘  └──────────┘  └──────────┘              │
└─────────────────────────────────────────────────────────┘
```

## 🔧 硬件清单

| 组件 | 型号 | 数量 | 说明 |
|------|------|------|------|
| 主控 | ESP32-S3-WROOM-1 | 1 | 主控芯片，带WiFi/BT |
| 麦克风 | INMP441 | 1 | I2S数字麦克风 |
| 功放+喇叭 | MAX98357A + 3W喇叭 | 1 | I2S数字功放 |
| LED | WS2812B RGB LED | 1 | 状态指示灯 |
| 按键 | 轻触开关 | 1 | 手动唤醒/打断 |
| 电源 | USB-C 5V | 1 | 供电 |

## 📁 项目结构

```
ai-toy/
├── firmware/                    # ESP32-S3 固件 (ESP-IDF)
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults
│   ├── partitions.csv
│   └── main/
│       ├── CMakeLists.txt
│       ├── main.cpp             # 主程序入口
│       ├── app_config.h         # 配置文件
│       ├── audio_codec.h/.cpp   # 音频编解码（I2S）
│       ├── wifi_manager.h/.cpp  # WiFi管理
│       ├── websocket_client.h/.cpp  # WebSocket客户端
│       ├── ai_assistant.h/.cpp  # AI助手状态机
│       ├── led_indicator.h/.cpp # LED状态指示
│       └── button_handler.h/.cpp # 按键处理
├── server/                      # 后端服务器 (Python)
│   ├── requirements.txt
│   ├── server.py                # WebSocket服务器主程序
│   ├── asr_handler.py           # 语音识别处理
│   ├── llm_handler.py           # 大模型对话处理
│   ├── tts_handler.py           # 语音合成处理
│   └── config.py                # 服务器配置
├── docs/
│   ├── hardware.md              # 硬件接线说明
│   ├── setup.md                 # 环境搭建指南
│   └── protocol.md              # 通信协议说明
└── tools/
    └── flash.sh                 # 烧录脚本
```

## 🚀 快速开始

### 1. 硬件准备

参考 [硬件接线说明](docs/hardware.md) 完成硬件连接。

### 2. 固件编译烧录

```bash
# 安装 ESP-IDF v5.1+
# https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/

# 进入固件目录
cd firmware

# 配置项目（设置WiFi、服务器地址等）
idf.py menuconfig

# 编译
idf.py build

# 烧录
idf.py -p COM3 flash monitor
```

### 3. 启动后端服务器

```bash
cd server

# 安装依赖
pip install -r requirements.txt

# 配置 API Keys（复制并编辑配置文件）
cp config.example.py config.py
# 编辑 config.py，填入你的 API Keys

# 启动服务器
python server.py
```

### 4. 使用

1. 上电后等待WiFi连接（LED蓝色闪烁）
2. 连接成功后LED变为绿色常亮
3. 说出唤醒词 **"你好小智"** 或按下按键
4. LED变为红色，开始录音
5. 说出你的问题
6. 停止说话后，LED变为黄色，等待AI回复
7. AI回复通过喇叭播放，LED变为绿色

## 📡 通信协议

参考 [通信协议说明](docs/protocol.md)

## 🔑 支持的AI服务

| 服务类型 | 支持平台 |
|---------|---------|
| ASR语音识别 | 百度智能云、阿里云、讯飞、Whisper |
| LLM大模型 | OpenAI GPT、文心一言、通义千问、智谱AI |
| TTS语音合成 | 百度智能云、阿里云、讯飞、Edge TTS |

## 📄 License

MIT License - 详见 [LICENSE](LICENSE)

## 🙏 致谢

- [虾哥小智 (XiaoZhi)](https://github.com/78/xiaozhi-esp32) - 原始方案参考
- [ESP-IDF](https://github.com/espressif/esp-idf) - Espressif官方开发框架
- [ESP-SR](https://github.com/espressif/esp-sr) - 语音识别库