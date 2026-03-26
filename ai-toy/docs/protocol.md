# 通信协议说明

## 概述

ESP32-S3 与后端服务器通过 **WebSocket** 协议通信，使用 JSON 格式传输控制消息，使用二进制格式传输音频数据。

- **连接地址**: `ws://<server_ip>:8765/ws`
- **音频格式**: PCM, 16bit, 16kHz, 单声道

---

## 消息流程

```
ESP32-S3 (客户端)              后端服务器
      │                              │
      │──── {"type":"hello"} ───────►│  握手
      │◄─── {"type":"hello"} ────────│  握手响应
      │                              │
      │  [用户说话唤醒词/按键]         │
      │                              │
      │──── {"type":"listen_start"} ►│  开始录音
      │──── [PCM音频数据 binary] ────►│  流式发送音频
      │──── [PCM音频数据 binary] ────►│
      │──── ... (持续发送)            │
      │──── {"type":"listen_stop"} ─►│  停止录音
      │                              │
      │                              │  [服务器进行ASR识别]
      │◄─── {"type":"asr_result"} ───│  ASR识别结果
      │                              │
      │                              │  [服务器调用LLM]
      │◄─── {"type":"llm_response"} ─│  LLM流式回复
      │◄─── {"type":"llm_response"} ─│
      │◄─── {"type":"llm_response",  │
      │      "done":true} ───────────│  LLM回复完成
      │                              │
      │                              │  [服务器进行TTS合成]
      │◄─── {"type":"tts_start"} ────│  TTS开始
      │◄─── [PCM音频数据 binary] ────│  流式发送TTS音频
      │◄─── [PCM音频数据 binary] ────│
      │◄─── ... (持续接收)            │
      │◄─── {"type":"tts_end"} ──────│  TTS结束
      │                              │
      │  [用户打断]                   │
      │──── {"type":"abort"} ───────►│  打断
      │                              │
```

---

## 消息格式详解

### 客户端 → 服务器

#### 1. 握手 (hello)
```json
{
    "type": "hello",
    "device_id": "esp32s3_aabbccddeeff",
    "version": "1.0"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| type | string | 固定为 "hello" |
| device_id | string | 设备唯一ID (基于MAC地址) |
| version | string | 固件版本号 |

#### 2. 开始录音 (listen_start)
```json
{
    "type": "listen_start",
    "device_id": "esp32s3_aabbccddeeff"
}
```

#### 3. 停止录音 (listen_stop)
```json
{
    "type": "listen_stop",
    "device_id": "esp32s3_aabbccddeeff"
}
```

#### 4. 打断 (abort)
```json
{
    "type": "abort",
    "device_id": "esp32s3_aabbccddeeff"
}
```
打断当前正在进行的操作（录音/LLM/TTS）。

#### 5. 音频数据 (Binary)
- 格式: 原始 PCM 数据
- 采样率: 16000 Hz
- 位深: 16 bit (有符号小端)
- 声道: 单声道
- 每帧大小: 960 字节 (30ms × 16000Hz × 2字节 = 960字节)

---

### 服务器 → 客户端

#### 1. 握手响应 (hello)
```json
{
    "type": "hello",
    "session_id": "a1b2c3d4",
    "server_version": "1.0"
}
```

#### 2. ASR识别结果 (asr_result)
```json
{
    "type": "asr_result",
    "text": "今天天气怎么样"
}
```

#### 3. LLM流式回复 (llm_response)
```json
{
    "type": "llm_response",
    "text": "今天",
    "done": false
}
```
```json
{
    "type": "llm_response",
    "text": "天气晴朗，适合出门玩耍！",
    "done": true
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| text | string | 本次回复的文字片段 |
| done | bool | true 表示回复完成 |

#### 4. TTS开始 (tts_start)
```json
{
    "type": "tts_start",
    "sample_rate": 16000
}
```

#### 5. TTS音频数据 (Binary)
- 格式: 原始 PCM 数据
- 采样率: 16000 Hz (与 tts_start 中的 sample_rate 一致)
- 位深: 16 bit
- 声道: 单声道

#### 6. TTS结束 (tts_end)
```json
{
    "type": "tts_end"
}
```

#### 7. 错误 (error)
```json
{
    "type": "error",
    "message": "ASR识别失败，请重试"
}
```

---

## 状态机

### ESP32-S3 端状态

```
                    ┌─────────────────────────────────────┐
                    │                                     │
                    ▼                                     │
              ┌──────────┐   唤醒词/按键    ┌──────────┐  │
              │  IDLE    │ ──────────────► │  WAKEUP  │  │
              │  (空闲)  │                 │  (唤醒)  │  │
              └──────────┘                 └──────────┘  │
                    ▲                           │         │
                    │                           ▼         │
                    │                    ┌──────────────┐ │
                    │                    │  LISTENING   │ │
                    │                    │  (录音中)    │ │
                    │                    └──────────────┘ │
                    │                           │         │
                    │                    VAD静音超时       │
                    │                           ▼         │
                    │                    ┌──────────────┐ │
                    │                    │  PROCESSING  │ │
                    │                    │  (处理中)    │ │
                    │                    └──────────────┘ │
                    │                           │         │
                    │                    收到tts_start    │
                    │                           ▼         │
                    │                    ┌──────────────┐ │
                    │   tts_end/abort    │  SPEAKING    │ │
                    │ ◄──────────────── │  (播放中)    │ │
                    │                    └──────────────┘ │
                    │                                     │
                    └─────────────────────────────────────┘
                                   abort
```

---

## 错误处理

| 错误场景 | 处理方式 |
|---------|---------|
| WiFi断开 | 自动重连，LED蓝色闪烁 |
| WebSocket断开 | 自动重连 |
| ASR识别失败 | 服务器发送 error 消息，设备回到 IDLE |
| LLM调用失败 | 服务器发送默认回复 |
| TTS合成失败 | 服务器发送 error 消息 |
| 用户打断 | 发送 abort，立即停止所有操作 |