# 环境搭建与编译指南

## 一、固件编译 (ESP32-S3)

### 1.1 安装 ESP-IDF

推荐使用 ESP-IDF v5.1 或更高版本。

**Windows (推荐使用官方安装器):**
```
下载: https://dl.espressif.com/dl/esp-idf/
安装 ESP-IDF Tools Installer，选择 ESP-IDF v5.1+
```

**Linux/macOS:**
```bash
# 安装依赖
sudo apt-get install git wget flex bison gperf python3 python3-pip cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

# 克隆 ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.1

# 安装工具链
./install.sh esp32s3

# 设置环境变量
. ./export.sh
```

### 1.2 配置项目

直接编辑 `firmware/main/app_config.h`：
```c
#define WIFI_SSID       "你的WiFi名称"
#define WIFI_PASSWORD   "你的WiFi密码"
#define SERVER_HOST     "192.168.1.100"  // 后端服务器IP
#define SERVER_PORT     8765
```

根据实际接线修改引脚定义（如果与默认不同）：
```c
#define MIC_I2S_SCK_PIN     GPIO_NUM_4
#define MIC_I2S_WS_PIN      GPIO_NUM_5
#define MIC_I2S_SD_PIN      GPIO_NUM_6
#define SPK_I2S_BCK_PIN     GPIO_NUM_15
#define SPK_I2S_WS_PIN      GPIO_NUM_16
#define SPK_I2S_DATA_PIN    GPIO_NUM_17
#define LED_PIN             GPIO_NUM_48
#define BUTTON_PIN          GPIO_NUM_0
```

### 1.3 编译

```bash
cd firmware

# 设置目标芯片
idf.py set-target esp32s3

# 编译
idf.py build
```

### 1.4 烧录

```bash
# Windows 查看串口: 设备管理器 → 端口
# Linux/macOS: ls /dev/ttyUSB* 或 ls /dev/ttyACM*

# 烧录 (替换 COM3 为实际串口)
idf.py -p COM3 flash

# 烧录并打开串口监视器
idf.py -p COM3 flash monitor

# 仅打开串口监视器
idf.py -p COM3 monitor
```

**串口监视器快捷键:**
- `Ctrl+]` 退出监视器
- `Ctrl+T Ctrl+R` 重启设备

### 1.5 预期启动日志

```
I (xxx) MAIN: ========================================
I (xxx) MAIN:   AI玩具 v1.0 启动
I (xxx) MAIN:   基于ESP32-S3 + 虾哥小智方案
I (xxx) MAIN: ========================================
I (xxx) LED: 初始化LED指示灯 (GPIO 48)
I (xxx) BTN: 初始化按键处理器 (GPIO 0)
I (xxx) WIFI: 初始化WiFi管理器
I (xxx) WIFI: 连接WiFi: YourWiFiSSID
I (xxx) WIFI: 获取IP地址: 192.168.1.xxx
I (xxx) AUDIO: 初始化音频编解码器
I (xxx) WS: 初始化WebSocket客户端: ws://192.168.1.100:8765/ws
I (xxx) WS: WebSocket已连接
I (xxx) AI: WebSocket连接成功，发送握手
I (xxx) MAIN: ========================================
I (xxx) MAIN:   系统启动完成！
I (xxx) MAIN:   说 '你好小智' 或按下按键唤醒
I (xxx) MAIN: ========================================
```

---

## 二、后端服务器搭建

### 2.1 环境要求

- Python 3.9+
- ffmpeg (用于音频格式转换)

**安装 ffmpeg:**
```bash
# Windows: 下载 https://ffmpeg.org/download.html，添加到PATH
# Ubuntu/Debian:
sudo apt install ffmpeg
# macOS:
brew install ffmpeg
```

### 2.2 安装 Python 依赖

```bash
cd server

# 创建虚拟环境 (推荐)
python -m venv venv

# 激活虚拟环境
# Windows:
venv\Scripts\activate
# Linux/macOS:
source venv/bin/activate

# 安装依赖
pip install -r requirements.txt
```

### 2.3 配置 API Keys

编辑 `server/config.py`，填入你的 API Keys：

**最简配置 (使用 Edge TTS + Ollama 本地LLM，完全免费):**
```python
# ASR: 使用 Whisper 本地识别
ASR_PROVIDER = "whisper"
WHISPER_MODEL = "base"  # 下载约 140MB

# LLM: 使用 Ollama 本地模型
LLM_PROVIDER = "ollama"
OLLAMA_BASE_URL = "http://localhost:11434"
OLLAMA_MODEL = "qwen2.5:7b"

# TTS: 使用 Edge TTS (免费)
TTS_PROVIDER = "edge"
EDGE_TTS_VOICE = "zh-CN-XiaoxiaoNeural"
```

**推荐配置 (使用云端API，效果更好):**
```python
# ASR: 百度智能云 (免费额度充足)
ASR_PROVIDER = "baidu"
BAIDU_ASR_APP_ID = "你的AppID"
BAIDU_ASR_API_KEY = "你的APIKey"
BAIDU_ASR_SECRET_KEY = "你的SecretKey"

# LLM: 通义千问 (有免费额度)
LLM_PROVIDER = "qianwen"
QIANWEN_API_KEY = "你的APIKey"
QIANWEN_MODEL = "qwen-turbo"

# TTS: Edge TTS (免费)
TTS_PROVIDER = "edge"
```

### 2.4 安装 Whisper (如果使用本地ASR)

```bash
pip install openai-whisper
# 首次运行会自动下载模型
```

### 2.5 安装 Ollama (如果使用本地LLM)

```bash
# 下载安装 Ollama: https://ollama.ai/
# 拉取模型:
ollama pull qwen2.5:7b
# 启动服务 (默认在 localhost:11434):
ollama serve
```

### 2.6 启动服务器

```bash
cd server
python server.py
```

预期输出：
```
2026-03-18 14:00:00 [INFO] SERVER: 启动AI玩具服务器: ws://0.0.0.0:8765
2026-03-18 14:00:00 [INFO] SERVER:   ASR提供商: baidu
2026-03-18 14:00:00 [INFO] SERVER:   LLM提供商: qianwen
2026-03-18 14:00:00 [INFO] SERVER:   TTS提供商: edge
2026-03-18 14:00:00 [INFO] SERVER: 服务器已启动，等待设备连接...
```

### 2.7 查看服务器IP

ESP32-S3 需要知道服务器的局域网IP地址：

```bash
# Windows:
ipconfig
# 查找 "以太网适配器" 或 "无线局域网适配器" 下的 IPv4 地址

# Linux/macOS:
ip addr show
# 或
ifconfig
```

将查到的IP填入 `firmware/main/app_config.h` 的 `SERVER_HOST`。

---

## 三、快速验证

### 3.1 测试服务器

可以用 Python 脚本测试服务器是否正常工作：

```python
# test_server.py
import asyncio
import websockets
import json

async def test():
    uri = "ws://localhost:8765"
    async with websockets.connect(uri) as ws:
        # 握手
        await ws.send(json.dumps({
            "type": "hello",
            "device_id": "test_device",
            "version": "1.0"
        }))
        resp = await ws.recv()
        print("握手响应:", resp)

asyncio.run(test())
```

### 3.2 测试 TTS

```bash
cd server
python -c "
import asyncio
from tts_handler import EdgeTTS

async def test():
    tts = EdgeTTS()
    chunks = []
    async for chunk in tts.synthesize_stream('你好，我是小智！'):
        chunks.append(chunk)
    print(f'TTS生成 {sum(len(c) for c in chunks)} 字节音频')

asyncio.run(test())
"
```

---

## 四、常见问题

### Q: 编译报错 "cannot open source file"
**A:** 这是 VS Code IntelliSense 的提示，不影响实际编译。确保已安装 ESP-IDF 并设置了环境变量，然后用 `idf.py build` 编译。

### Q: WiFi连接失败
**A:** 检查 `WIFI_SSID` 和 `WIFI_PASSWORD` 是否正确。注意 ESP32-S3 只支持 2.4GHz WiFi，不支持 5GHz。

### Q: WebSocket连接失败
**A:** 
1. 确认服务器已启动
2. 确认 `SERVER_HOST` 填写的是服务器的局域网IP（不是 localhost）
3. 确认防火墙没有阻止 8765 端口
4. ESP32-S3 和服务器需要在同一局域网

### Q: 麦克风采集到的声音很小
**A:** 调整 `app_config.h` 中的 `MIC_GAIN_DB` 值（默认24dB），或在服务器端对音频进行增益处理。

### Q: TTS播放有杂音
**A:** 
1. 检查 MAX98357A 的电源是否稳定
2. 在 INMP441 VDD 引脚加 100nF 去耦电容
3. 尝试降低 `SPK_SAMPLE_RATE`

### Q: Edge TTS 报错
**A:** 确认网络可以访问微软服务器。如果在中国大陆，可能需要代理，或改用百度/讯飞 TTS。