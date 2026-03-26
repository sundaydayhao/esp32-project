# AI English Trainer 🎓

ESP32-S3 智能英语外教设备 - 专为5-10岁儿童设计

AI扮演外教 **Miss Lily**，实时进行：
- 🎤 **语音识别** (OpenAI Whisper)
- 📝 **发音评分 + 纠音** (GPT-4)
- 🔊 **语音回复** (OpenAI TTS)

---

## 硬件清单

| 组件 | 型号 | 说明 |
|------|------|------|
| 主控 | ESP32-S3-DevKitC-1 | 8MB Flash + 8MB PSRAM |
| 麦克风 | INMP441 | I2S MEMS全向麦克风 |
| 功放+扬声器 | MAX98357A | I2S D类功放，3W |
| 按钮 | 轻触开关 | 按住录音，松开发送 |
| LED | WS2812B | 状态指示（DevKitC-1内置GPIO48） |

---

## 接线图

```
ESP32-S3-DevKitC-1
┌─────────────────────────────────────────┐
│                                         │
│  INMP441 麦克风                          │
│  VDD  ──── 3.3V                         │
│  GND  ──── GND                          │
│  L/R  ──── GND  (选左声道)               │
│  SCK  ──── GPIO14                       │
│  WS   ──── GPIO15                       │
│  SD   ──── GPIO16                       │
│                                         │
│  MAX98357A 功放                          │
│  VIN  ──── 5V (或3.3V)                  │
│  GND  ──── GND                          │
│  BCLK ──── GPIO17                       │
│  LRC  ──── GPIO18                       │
│  DIN  ──── GPIO8                        │
│  SD   ──── 悬空 (始终开启)               │
│                                         │
│  按钮 (Push-to-Talk)                    │
│  一端 ──── GPIO0 (BOOT按钮)             │
│  另端 ──── GND                          │
│                                         │
│  WS2812B LED                            │
│  DIN  ──── GPIO48 (DevKitC-1内置)       │
│                                         │
└─────────────────────────────────────────┘
```

---

## 快速开始

### 1. 环境准备

```bash
# 安装 ESP-IDF v5.x
# 参考: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/

# 设置环境变量
. $HOME/esp/esp-idf/export.sh   # Linux/macOS
# 或
%IDF_PATH%\export.bat            # Windows
```

### 2. 配置项目

```bash
cd ai-english-trainer
idf.py menuconfig
```

在菜单中配置：
- **AI English Trainer Configuration → WiFi Settings**
  - `WiFi SSID`: 你的WiFi名称
  - `WiFi Password`: 你的WiFi密码
- **AI English Trainer Configuration → OpenAI Settings**
  - `OpenAI API Key`: 你的OpenAI API密钥 (从 https://platform.openai.com/api-keys 获取)
  - `GPT Model`: 默认 `gpt-4o-mini` (性价比最高)
  - `TTS Voice`: 默认 `nova` (温暖女声，适合儿童)

### 3. 编译烧录

```bash
idf.py build
idf.py -p COM3 flash monitor   # Windows，替换COM3为实际端口
# 或
idf.py -p /dev/ttyUSB0 flash monitor  # Linux
```

---

## 使用方法

| LED颜色 | 状态 | 操作 |
|---------|------|------|
| 🟢 绿色呼吸 | 待机就绪 | 按住按钮开始说英语 |
| 🔴 红色闪烁 | 录音中 | 说英语，松开按钮结束 |
| 🟡 黄色脉冲 | AI处理中 | 等待外教回复 |
| 🔵 蓝色脉冲 | 播放回复 | 听外教讲解 |
| ✅ 绿色闪3下 | 完成 | 准备下一轮 |
| 🔴 红色常亮 | 错误 | 3秒后自动恢复 |

### 操作流程

```
1. 开机 → LED绿色呼吸 (等待WiFi连接)
2. 连接成功 → LED绿色慢呼吸 (就绪)
3. 按住BOOT按钮 → LED红色闪烁 (开始录音)
4. 说英语，例如: "Hello! My name is Xiao Ming."
5. 松开按钮 → LED黄色脉冲 (AI处理中，约3-5秒)
6. 外教Miss Lily回复 → LED蓝色脉冲 (播放音频)
   例如: "Hello Xiao Ming! Your pronunciation score is 8 out of 10!
          Very good! Try to say 'name' like 'naym'. You are doing amazing!"
7. 回到步骤3，继续对话
```

### 音量调节

在待机状态下，**长按按钮3秒**可循环切换音量：50% → 70% → 85% → 100%

---

## AI外教系统提示词

Miss Lily 的角色设定（GPT系统提示）：

```
你是 Miss Lily，一位温暖、热情、鼓励性强的英语外教，
专门教5-10岁的小朋友。

当孩子说话时，你会：
1. 自然地回应孩子说的内容
2. 温柔地纠正发音或语法错误（如有）
3. 给出1-10分的发音评分（慷慨鼓励）
4. 以热情的鼓励结束

返回JSON格式：
{
  "response": "对话回复",
  "correction": "纠音建议（无则为空）",
  "score": 8,
  "encouragement": "鼓励语"
}
```

---

## 项目结构

```
ai-english-trainer/
├── CMakeLists.txt              # 顶层CMake
├── sdkconfig.defaults          # 默认SDK配置
├── partitions.csv              # 分区表 (3MB app + 5MB storage)
├── README.md                   # 本文档
└── main/
    ├── CMakeLists.txt          # 组件CMake
    ├── Kconfig.projbuild       # menuconfig配置项
    ├── main.c                  # 主程序 + 状态机
    ├── audio_capture.c/h       # I2S麦克风驱动 (INMP441)
    ├── audio_playback.c/h      # I2S扬声器驱动 (MAX98357A)
    ├── wifi_manager.c/h        # WiFi连接管理
    ├── openai_client.c/h       # OpenAI API (Whisper+GPT+TTS)
    ├── button_handler.c/h      # 按钮防抖+长按检测
    └── led_indicator.c/h       # WS2812B LED动画
```

---

## 状态机

```
    ┌─────────────────────────────────────────────┐
    │                                             │
    ▼                                             │
  IDLE ──[按下按钮]──► RECORDING                  │
    ▲                      │                      │
    │              [松开/静音/超时]                │
    │                      ▼                      │
    │               PROCESSING                    │
    │              (Whisper→GPT→TTS)              │
    │                      │                      │
    │                  [完成]                      │
    │                      ▼                      │
    │                  PLAYING                    │
    │                      │                      │
    │                  [播完]                      │
    └──────────────────────┘                      │
                                                  │
    任意状态 ──[错误]──► ERROR ──[3秒]────────────┘
```

---

## 技术规格

| 参数 | 值 |
|------|-----|
| 麦克风采样率 | 16kHz, 16-bit, 单声道 |
| TTS输出格式 | 24kHz, 16-bit PCM, 单声道 |
| 最大录音时长 | 10秒 (可配置) |
| 静音自动停止 | 1.5秒静音后自动停止 |
| AI处理延迟 | 约3-8秒 (取决于网络) |
| 音频缓冲区 | PSRAM (最大320KB录音 + 576KB TTS) |

---

## 常见问题

**Q: 编译报错 `esp_err.h not found`**
A: VS Code IntelliSense 找不到ESP-IDF路径，不影响编译。
   安装 [ESP-IDF VS Code Extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension) 可解决。

**Q: WiFi连接失败**
A: 检查 `idf.py menuconfig` 中的SSID和密码是否正确。

**Q: OpenAI API返回401错误**
A: API Key无效或未设置。在menuconfig中重新配置。

**Q: 录音没有声音/声音很小**
A: 检查INMP441的L/R引脚是否接GND（选择左声道）。

**Q: 扬声器没有声音**
A: 检查MAX98357A的SD引脚是否悬空（不要接GND）。

**Q: 内存不足**
A: 确保PSRAM已启用（`CONFIG_ESP32S3_SPIRAM_SUPPORT=y`）。

---

## 费用估算 (OpenAI API)

| API | 价格 | 每次对话估算 |
|-----|------|-------------|
| Whisper | $0.006/分钟 | ~$0.001 (10秒录音) |
| GPT-4o-mini | $0.15/1M tokens | ~$0.0003 |
| TTS-1 | $15/1M字符 | ~$0.003 (200字符) |
| **合计** | | **~$0.004/次对话** |

每天100次对话约 **$0.40**，非常经济实惠。

---

## License

MIT License - 自由使用、修改和分发