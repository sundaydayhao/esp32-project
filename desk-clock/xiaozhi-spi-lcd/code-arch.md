# 小智 ESP32 语音聊天实现原理

## 整体架构

小智采用**端云协同**架构：ESP32 负责音频采集/播放、唤醒词检测、VAD（语音活动检测），云端服务器负责 ASR（语音识别）、LLM（大语言模型推理）、TTS（语音合成）。

```
麦克风 → [唤醒词检测] → [VAD] → [Opus编码] → WebSocket/MQTT → 云端
扬声器 ← [Opus解码]  ←────────────────────────────────────── 云端TTS
```

---

## 一、状态机（核心流程控制）

`main/application.cc` 中定义了完整的设备状态机，定义在 `main/device_state.h`：

| 状态 | 含义 |
|------|------|
| `kIdle` | 空闲，等待唤醒 |
| `kConnecting` | 正在连接服务器 |
| `kListening` | 正在监听用户说话（麦克风开启，发送音频） |
| `kSpeaking` | 正在播放 TTS 回复 |
| `kUpgrading` | OTA 升级中 |

状态转换由 `Application::SetDeviceState()` 驱动，每次状态变化都会触发 LED、显示屏、音频服务的联动更新。

---

## 二、音频采集与处理

### 2.1 AudioCodec（硬件抽象层）
`main/audio/audio_codec.cc` 封装了 ESP-ADF 的 `audio_pipeline`，提供：
- `InputData()`：从麦克风读取 PCM 原始数据（16kHz, 16bit, 单声道）
- `OutputData()`：向扬声器写入 PCM 数据

不同硬件板（ES8311、ES7210、INMP441 等）通过继承 `AudioCodec` 实现各自的驱动，在 `main/audio/codecs/` 目录下。

### 2.2 AudioProcessor（信号处理）
`main/audio/audio_processor.h` 定义接口，实现在 `main/audio/processors/` 下：
- **AEC（回声消除）**：防止扬声器播放的声音被麦克风重新采集
- **VAD（语音活动检测）**：判断用户是否在说话，避免发送静音数据

### 2.3 唤醒词检测
`main/audio/wake_words/` 下实现，基于 **ESP-SR** 库（乐鑫官方语音识别框架）：
- 持续监听麦克风输入
- 检测到"小智小智"等唤醒词后，触发 `on_wake_word_detected` 回调
- Application 收到回调后切换到 `kConnecting` → `kListening` 状态

---

## 三、音频编码与传输

### 3.1 Opus 编码
采集到的 PCM 数据经 **Opus 编解码器**压缩（大幅降低带宽）：
- 采样率：16kHz
- 帧长：60ms（960 samples）
- 编码后约 2~4KB/s，适合嵌入式网络传输

### 3.2 传输协议

项目支持两种协议（`main/protocols/`）：

#### WebSocket 协议（`websocket_protocol.cc`）
- **控制消息**：JSON 格式，通过 WebSocket 文本帧发送
  ```json
  // 客户端 → 服务器：开始监听
  {"type": "listen", "state": "start", "mode": "auto"}
  
  // 服务器 → 客户端：TTS 开始
  {"type": "tts", "state": "start"}
  
  // 服务器 → 客户端：ASR 识别结果
  {"type": "stt", "text": "今天天气怎么样"}
  
  // 服务器 → 客户端：LLM 回复文本
  {"type": "llm", "text": "今天北京晴，气温..."}
  ```
- **音频数据**：二进制帧，直接发送 Opus 编码后的字节流

#### MQTT 协议（`mqtt_protocol.cc`）
- 用于 4G 模组（ML307）等无 WiFi 场景
- 控制消息发布到特定 Topic，音频数据通过 UDP 旁路传输（见 `docs/mqtt-udp.md`）

---

## 四、完整语音聊天流程

```
1. 设备上电 → 连接 WiFi → 空闲状态(kIdle)
                                    ↓
2. 麦克风持续采集 → 唤醒词检测("小智小智")
                                    ↓
3. 建立 WebSocket 连接 → 发送 hello 握手消息（含设备信息、音频参数）
                                    ↓
4. 切换到 kListening 状态
   - AudioService 开始读取麦克风 PCM 数据
   - VAD 检测语音活动
   - Opus 编码 → WebSocket 二进制帧 → 发送给服务器
                                    ↓
5. 服务器端处理：
   ASR（Whisper等）→ LLM（GPT/Qwen等）→ TTS（语音合成）
   服务器实时推送：
   - stt 消息（识别文本，显示在屏幕上）
   - llm 消息（回复文本，显示在屏幕上）
   - tts 音频二进制帧（Opus 编码的语音）
                                    ↓
6. 切换到 kSpeaking 状态
   - 接收 Opus 音频帧 → 解码为 PCM → 写入扬声器播放
   - 显示屏显示 LLM 回复文本
                                    ↓
7. TTS 播放完毕 → 回到 kListening（继续对话）或 kIdle（超时）
```

---

## 五、关键代码位置速查

| 功能 | 文件 |
|------|------|
| 主流程/状态机 | `main/application.cc` |
| 音频硬件抽象 | `main/audio/audio_codec.cc` |
| 音频服务（采集/播放循环） | `main/audio/audio_service.cc` |
| 唤醒词检测 | `main/audio/wake_words/` |
| VAD/AEC 处理 | `main/audio/processors/` |
| WebSocket 通信协议 | `main/protocols/websocket_protocol.cc` |
| MQTT+UDP 通信协议 | `main/protocols/mqtt_protocol.cc` |
| MCP 工具调用扩展 | `main/mcp_server.cc` |
| OTA 升级 | `main/ota.cc` |

---

## 六、设计亮点

1. **全双工通信**：发送音频和接收 TTS 可以同时进行，支持打断播放
2. **VAD 驱动**：只在检测到语音时才发送数据，节省带宽
3. **Opus 编码**：相比 PCM 节省约 10 倍带宽，适合嵌入式网络
4. **硬件抽象**：通过继承 `AudioCodec`、`AudioProcessor` 支持数十种硬件板
5. **双协议支持**：WiFi 用 WebSocket，4G 模组用 MQTT+UDP，无缝切换
6. **MCP 扩展**：支持 Model Context Protocol，LLM 可调用设备本地工具（控制 LED、获取系统信息等）