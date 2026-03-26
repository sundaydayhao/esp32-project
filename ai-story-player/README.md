# 🌙 AI 睡前故事机 (AI Bedtime Story Player)

一个基于 ESP32-S3 的智能睡前故事机，孩子只需说出关键词（如"恐龙"、"太空"、"公主"），AI 就会实时生成专属故事并朗读，同时在屏幕上显示精美插图。

## ✨ 功能特性

- 🎤 **语音关键词识别**：孩子说出关键词，自动触发故事生成
- 🤖 **AI 实时故事生成**：基于 OpenAI GPT-4 生成个性化睡前故事
- 🔊 **语音朗读**：使用 TTS 技术将故事转为温柔的语音播放
- 🖼️ **屏幕插图显示**：在 TFT 屏幕上显示与故事匹配的精美插图
- 📡 **WiFi 连接**：ESP32-S3 通过 WiFi 与后端服务器通信
- 🌙 **睡眠模式**：故事结束后自动进入低功耗睡眠模式

## 🏗️ 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                    ESP32-S3 设备                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐  │
│  │ INMP441  │  │ MAX98357A│  │ ST7789   │  │ WiFi   │  │
│  │ 麦克风   │  │ 扬声器   │  │ TFT屏幕  │  │ 模块   │  │
│  └──────────┘  └──────────┘  └──────────┘  └────────┘  │
└─────────────────────────────────────────────────────────┘
                          │ WebSocket / HTTP
                          ▼
┌─────────────────────────────────────────────────────────┐
│                  Python 后端服务器                        │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐  │
│  │ Whisper  │  │  GPT-4   │  │  TTS     │  │ 图片   │  │
│  │ 语音识别 │  │ 故事生成 │  │ 语音合成 │  │ 生成   │  │
│  └──────────┘  └──────────┘  └──────────┘  └────────┘  │
└─────────────────────────────────────────────────────────┘
```

## 🔧 硬件清单

| 组件 | 型号 | 说明 |
|------|------|------|
| 主控 | ESP32-S3 DevKit | 主控制器，带 WiFi/BT |
| 屏幕 | ST7789 1.54" TFT | 240x240 彩色显示屏 |
| 麦克风 | INMP441 | I2S 数字麦克风 |
| 功放 | MAX98357A | I2S 数字功放 |
| 扬声器 | 3W 4Ω | 小型扬声器 |
| 电源 | 5V USB | 或 3.7V 锂电池 |

## 📌 硬件接线

### ESP32-S3 引脚定义

```
ESP32-S3          ST7789 TFT屏幕
GPIO 40  ──────── MOSI (SDA)
GPIO 39  ──────── SCK (SCL)
GPIO 38  ──────── CS
GPIO 37  ──────── DC
GPIO 36  ──────── RST
3.3V     ──────── VCC
GND      ──────── GND

ESP32-S3          INMP441 麦克风
GPIO 14  ──────── WS (LRCK)
GPIO 13  ──────── SCK (BCLK)
GPIO 12  ──────── SD (DATA)
3.3V     ──────── VDD
GND      ──────── GND
GND      ──────── L/R (左声道)

ESP32-S3          MAX98357A 功放
GPIO 8   ──────── LRC
GPIO 9   ──────── BCLK
GPIO 10  ──────── DIN
3.3V     ──────── VDD
GND      ──────── GND
```

## 🚀 快速开始

### 1. 后端服务器部署

```bash
cd backend
pip install -r requirements.txt

# 配置环境变量
cp .env.example .env
# 编辑 .env 文件，填入 OpenAI API Key

# 启动服务器
python app.py
```

### 2. ESP32-S3 固件烧录

```bash
cd firmware

# 安装 ESP-IDF (v5.1+)
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/

# 配置项目
idf.py menuconfig
# 设置 WiFi SSID/密码 和 服务器地址

# 编译并烧录
idf.py build flash monitor
```

### 3. 使用方法

1. 开机后，屏幕显示欢迎界面
2. 按下按钮或说"开始"唤醒设备
3. 说出故事关键词，如：
   - "我想听恐龙的故事"
   - "讲一个太空冒险"
   - "公主和魔法"
4. AI 自动生成故事并开始朗读
5. 屏幕同步显示故事插图

## 📁 项目结构

```
ai-story-player/
├── firmware/              # ESP32-S3 固件 (ESP-IDF)
│   ├── main/
│   │   ├── main.c         # 主程序入口
│   │   ├── audio_input.c  # 麦克风音频采集
│   │   ├── audio_output.c # 扬声器音频播放
│   │   ├── display.c      # TFT 屏幕显示
│   │   ├── wifi_manager.c # WiFi 连接管理
│   │   ├── websocket.c    # WebSocket 通信
│   │   └── CMakeLists.txt
│   ├── components/        # 第三方组件
│   ├── CMakeLists.txt
│   └── sdkconfig.defaults
├── backend/               # Python 后端服务器
│   ├── app.py             # FastAPI 主应用
│   ├── story_generator.py # AI 故事生成
│   ├── tts_engine.py      # 语音合成
│   ├── stt_engine.py      # 语音识别
│   ├── image_generator.py # 插图生成
│   ├── keyword_extractor.py # 关键词提取
│   ├── requirements.txt
│   └── .env.example
├── web/                   # Web 管理界面
│   ├── index.html
│   ├── style.css
│   └── app.js
├── assets/                # 静态资源
│   └── illustrations/     # 预置插图
└── README.md
```

## 🔑 环境变量配置

```env
OPENAI_API_KEY=your_openai_api_key
OPENAI_BASE_URL=https://api.openai.com/v1  # 可替换为国内代理
SERVER_HOST=0.0.0.0
SERVER_PORT=8000
TTS_VOICE=zh-CN-XiaoxiaoNeural  # Azure TTS 声音
```

## 📝 故事生成示例

**输入关键词**: 恐龙、森林、友谊

**生成故事**:
> 在很久很久以前，有一片神奇的森林，住着一只叫小雷的小恐龙...
> 小雷虽然长得很大，但他有一颗温柔的心...
> 他和小兔子、小鸟成为了最好的朋友...

## 🛠️ 技术栈

- **固件**: ESP-IDF v5.1, FreeRTOS, LVGL
- **后端**: Python 3.11, FastAPI, WebSocket
- **AI**: OpenAI GPT-4o, Whisper, TTS
- **音频**: I2S, PCM, MP3
- **显示**: SPI, ST7789, LVGL

## 📄 许可证

MIT License