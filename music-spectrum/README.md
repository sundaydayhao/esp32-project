# ESP32-S3 音乐频谱播放器

基于 ESP32-S3 + ESP-IDF 实现的音乐播放器，支持本地 MP3 文件和在线 HTTP 音频流，并通过内置 Web 服务器在浏览器中实时显示音乐信息和频谱图。

---

## 功能特性

| 功能 | 说明 |
|------|------|
| 🎵 MP3 解码 | 使用 minimp3 软件解码，支持 CBR/VBR |
| 💾 本地播放 | 从 SPIFFS 读取 `.mp3` 文件 |
| 🌐 在线流媒体 | 支持 HTTP/HTTPS MP3 直播流和文件 URL |
| 📊 实时频谱 | 512 点 FFT，64 频段对数刻度，WebSocket 推送 |
| 🖥️ Web 界面 | 深色主题，响应式布局，支持手机/PC |
| 🔊 音量控制 | 软件音量 0–100，支持静音 |
| ⏯️ 播放控制 | 播放 / 暂停 / 停止 |

---

## 硬件要求

### 必需
- **ESP32-S3** 开发板（推荐 16MB Flash + 8MB PSRAM）
- **I2S DAC 模块**（任选其一）：
  - MAX98357A（单声道，免外部电源）
  - PCM5102A（立体声，高音质）
  - UDA1334A（立体声）

### I2S 接线（默认引脚，可在 menuconfig 修改）

| 信号 | ESP32-S3 GPIO | DAC 引脚 |
|------|--------------|---------|
| BCK  | GPIO 14      | BCK / BCLK |
| WS   | GPIO 15      | WS / LRCK  |
| DOUT | GPIO 16      | DIN / DATA |
| GND  | GND          | GND        |
| 3.3V | 3.3V         | VCC（MAX98357A 用 5V）|

---

## 软件依赖

- **ESP-IDF v5.x**（推荐 v5.1 或更高）
- **VS Code** + **ESP-IDF 扩展**
- **minimp3**（CMake 自动下载，或手动放置）

---

## 快速开始

### 1. 克隆 / 打开项目

```bash
cd c:/code/esp32-music-spectrum
code .
```

### 2. 配置 WiFi 和引脚

打开 ESP-IDF 终端，运行：

```bash
idf.py menuconfig
```

进入 **Music Player Configuration**，设置：
- WiFi SSID / Password
- I2S 引脚（BCK / WS / DOUT）
- 采样率（默认 44100 Hz）

或直接编辑 `sdkconfig.defaults`：

```
CONFIG_WIFI_SSID="你的WiFi名称"
CONFIG_WIFI_PASSWORD="你的WiFi密码"
CONFIG_I2S_BCK_PIN=14
CONFIG_I2S_WS_PIN=15
CONFIG_I2S_DOUT_PIN=16
```

### 3. 下载 minimp3（首次构建自动完成）

CMake 配置阶段会自动从 GitHub 下载 `minimp3.h`。  
如果网络不通，手动下载：

```bash
curl -L https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h \
     -o components/minimp3/minimp3.h
```

### 4. 构建并烧录

```bash
idf.py build
idf.py -p COM3 flash monitor
```

> 将 `COM3` 替换为实际串口号。

### 5. 上传本地音乐（可选）

将 MP3 文件放入 `spiffs_image/` 目录，然后烧录 SPIFFS 分区：

```bash
# 生成并烧录 SPIFFS 镜像
idf.py -p COM3 spiffs_image flash
```

或使用 ESP-IDF 工具：

```bash
python $IDF_PATH/components/spiffs/spiffsgen.py \
    0x4F0000 spiffs_image spiffs.bin

esptool.py -p COM3 write_flash 0x310000 spiffs.bin
```

### 6. 打开 Web 界面

串口监视器会打印 IP 地址：

```
I (xxxx) main: Connected! Open http://192.168.1.xxx in your browser
```

在浏览器中打开该地址即可使用播放器。

---

## Web API

| 方法 | 路径 | 说明 |
|------|------|------|
| GET  | `/`            | 播放器主页 |
| GET  | `/api/status`  | 获取播放状态（JSON）|
| POST | `/api/play`    | 播放音乐 `{"url":"...","title":"...","artist":"..."}` |
| POST | `/api/pause`   | 暂停 |
| POST | `/api/resume`  | 继续 |
| POST | `/api/stop`    | 停止 |
| POST | `/api/volume`  | 设置音量 `{"volume":80}` |
| GET  | `/api/files`   | 列出 SPIFFS 中的 MP3 文件 |
| WS   | `/ws`          | WebSocket 频谱数据推送 |

### WebSocket 数据格式

二进制帧：`[0x01][float32 × 64]` = 257 字节  
- 第 1 字节：消息类型（`0x01` = 频谱数据）  
- 后 256 字节：64 个 float32，每个为 0.0–1.0 的频段幅度

---

## 项目结构

```
esp32-music-spectrum/
├── CMakeLists.txt              # 顶层 CMake
├── sdkconfig.defaults          # 默认配置
├── partitions.csv              # 分区表（16MB Flash）
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild       # menuconfig 选项
│   ├── main.c                  # 入口，初始化各模块
│   ├── wifi_manager.c/h        # WiFi 连接管理
│   ├── audio_player.c/h        # 音频播放流水线
│   ├── fft_analyzer.c/h        # FFT 频谱分析
│   ├── web_server.c/h          # HTTP + WebSocket 服务器
│   └── web/
│       ├── index.html          # 播放器主页（嵌入固件）
│       ├── style.css           # 深色主题样式
│       └── app.js              # 前端逻辑 + 频谱渲染
├── components/
│   └── minimp3/
│       ├── CMakeLists.txt      # 自动下载 minimp3.h
│       └── minimp3_impl.c      # minimp3 实现单元
└── spiffs_image/               # 本地 MP3 文件放这里
```

---

## 音频流水线

```
[HTTP/SPIFFS 源] ──► [输入环形缓冲区 64KB]
                              │
                              ▼
                    [MP3 解码任务 (minimp3)]
                              │
                              ▼
                    [PCM 环形缓冲区 32KB]
                         │         │
                         ▼         ▼
                   [I2S 输出]   [FFT 分析]
                                    │
                                    ▼
                            [WebSocket 广播]
```

---

## 常见问题

**Q: 编译时找不到 minimp3.h？**  
A: 确保 CMake 配置阶段有网络访问，或手动下载（见步骤 3）。

**Q: 没有声音？**  
A: 检查 I2S 接线和 GPIO 配置，确认 DAC 模块供电正常。

**Q: 在线流媒体卡顿？**  
A: 增大 `INPUT_RINGBUF_SIZE`（在 `audio_player.c` 中），或使用码率更低的流。

**Q: Web 界面无法访问？**  
A: 确认 ESP32-S3 和电脑在同一 WiFi 网络，检查防火墙设置。

**Q: PSRAM 相关错误？**  
A: 如果开发板没有 PSRAM，在 `sdkconfig.defaults` 中删除 PSRAM 相关配置。

---

## 许可证

- 本项目代码：MIT License  
- minimp3：MIT License（© lieff）