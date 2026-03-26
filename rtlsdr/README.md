# ESP32-S3 RTL-SDR 多功能无线电接收平台

基于 **ESP32-S3** + **RTL2832U/R820T2** USB 软件定义无线电（SDR）的多功能无线电接收平台。通过 Wi-Fi 热点提供 Web 界面，支持五大功能模块。

---

## 功能模块

| 模块 | 路径 | 频率 | 说明 |
|------|------|------|------|
| ✈ ADS-B 飞机追踪 | `/adsb/` | 1090 MHz | 实时接收飞机广播，显示位置/高度/速度 |
| 📻 FM 收音机 | `/fm/` | 87.5–108 MHz | 调频广播解调，浏览器 Web Audio 播放 |
| 📊 频谱仪 | `/spectrum/` | 500 kHz–1750 MHz | 实时频谱分析 + 瀑布图 |
| ⚓ AIS 船舶监控 | `/ais/` | 161.975/162.025 MHz | 港口河道船舶自动识别，Canvas 地图追踪 |
| 🚁 无人机监测告警 | `/drone/` | 433/868/915/1090/1200 MHz | 多频段扫描，SNR 分级告警 |

---

## 硬件要求

| 组件 | 规格 |
|------|------|
| 主控 | ESP32-S3（推荐 ESP32-S3-DevKitC-1，16 MB Flash） |
| SDR 接收器 | RTL2832U + R820T2（RTL-SDR Blog V3 或兼容型号） |
| USB 连接 | ESP32-S3 USB OTG（GPIO19 D−, GPIO20 D+）→ RTL-SDR |
| 天线 | 根据频段选择：1090 MHz 1/4波长天线（ADS-B/无人机）、VHF 天线（AIS）、宽带天线（频谱/FM） |

---

## 软件架构

```
esp32-rtlsdr/
├── main/
│   ├── main.c                  # 入口，初始化所有模块
│   ├── config.h                # 全局配置（Wi-Fi、GPIO 等）
│   ├── rtlsdr_usb.c/h          # RTL-SDR USB Host 驱动
│   ├── wifi_manager.c/h        # Wi-Fi AP/STA 管理
│   ├── web_server.c/h          # HTTP + WebSocket 服务器
│   ├── adsb/
│   │   ├── adsb_decoder.c/h    # ADS-B Mode S 解码器
│   │   └── adsb_rmt.c/h        # RMT 外设信号捕获
│   ├── fm/
│   │   └── fm_receiver.c/h     # FM 广播解调（WBFM）
│   ├── spectrum/
│   │   └── spectrum_analyzer.c/h  # FFT 频谱分析
│   ├── ais/
│   │   └── ais_decoder.c/h     # AIS GMSK/HDLC 解码器
│   └── drone/
│       └── drone_detector.c/h  # 无人机多频段扫描告警
├── spiffs_image/               # Web 前端（烧录到 SPIFFS）
│   ├── index.html              # 导航首页
│   ├── adsb/                   # ADS-B 前端
│   ├── fm/                     # FM 前端
│   ├── spectrum/               # 频谱仪前端
│   ├── ais/                    # AIS 船舶监控前端
│   └── drone/                  # 无人机监测前端
├── CMakeLists.txt
├── partitions.csv
└── sdkconfig.defaults
```

---

## 模块详情

### ✈ ADS-B 飞机追踪

- **频率**：1090 MHz
- **协议**：Mode S / ADS-B（DF17）
- **解码**：PPM 脉冲解调 → CRC-24 校验 → 位置/速度/高度解析
- **Web**：Canvas 地图，实时飞机图标 + 详情面板
- **API**：`GET /api/aircraft`，`WS /ws`

### 📻 FM 收音机

- **频率**：87.5–108.0 MHz
- **解调**：宽带 FM（WBFM），采样率 1 MSPS，抽取 22 倍 → 44.1 kHz PCM
- **播放**：HTTP 音频流 → 浏览器 Web Audio API 实时播放
- **API**：`GET /fm/audio`（PCM 流），`POST /fm/freq`，`POST /fm/start`

### 📊 频谱仪

- **频率**：500 kHz–1750 MHz（RTL-SDR 覆盖范围）
- **分辨率**：可调 FFT 点数，支持瀑布图历史显示
- **API**：`WS /spectrum/ws`，`POST /spectrum/start`

### ⚓ AIS 船舶监控

- **频率**：161.975 MHz（CH 87B）和 162.025 MHz（CH 88B），每 5 秒自动切换
- **协议**：GMSK 9600 baud → NRZI → HDLC → CRC-16/CRC-CCITT → AIS 载荷
- **支持消息类型**：
  - Type 1/2/3：Class A 位置报告（位置、航速、航向、导航状态）
  - Type 5：Class A 静态数据（船名、呼号、IMO、船型、目的港）
  - Type 18：Class B 位置报告
  - Type 24：Class B 静态数据
- **Web**：Canvas 地图，船只图标按导航状态/船型着色，点击显示详情
- **API**：`WS /ais/ws`，`GET /ais/vessels`，`POST /ais/start`

### 🚁 无人机监测告警

- **检测频段**：

  | 频段 | 频率 | 用途 |
  |------|------|------|
  | 433 MHz | 433.920 MHz | RC 遥控器 / ISM 频段 |
  | 868 MHz | 868.000 MHz | 欧洲 Remote ID / LoRa |
  | 915 MHz | 915.000 MHz | 北美数传链路 |
  | 1080 MHz | 1080.000 MHz | FPV 视频下行 |
  | 1090 MHz | 1090.000 MHz | ADS-B 装备无人机 |
  | 1200 MHz | 1200.000 MHz | 1.2 GHz FPV 视频 |

- **算法**：自适应噪底估计（IIR 滤波）+ SNR 三级告警
  - 低级（≥10 dB SNR）：可能是无人机
  - 中级（≥20 dB SNR）：疑似无人机
  - 高级（≥30 dB SNR）：很可能是无人机，页面红色闪烁横幅
- **配置**：告警阈值和驻留时间可在线调整
- **API**：`WS /drone/ws`，`GET /drone/status`，`POST /drone/start`，`GET/POST /drone/config`

---

## 编译与烧录

### 环境要求

- ESP-IDF v5.x
- Python 3.8+

### 编译

```bash
idf.py set-target esp32s3
idf.py build
```

### 烧录

```bash
idf.py -p COM3 flash monitor
```

> 首次烧录会同时烧录 SPIFFS 分区（Web 前端文件）。

### 仅更新 Web 前端

```bash
idf.py -p COM3 spiffs-flash
```

---

## Wi-Fi 配置

默认以 **AP 模式**启动：

| 参数 | 默认值 |
|------|--------|
| SSID | `ESP32-RTL-SDR` |
| 密码 | `12345678` |
| IP | `192.168.4.1` |

连接后访问 `http://192.168.4.1/` 进入导航首页。

如需连接现有 Wi-Fi（STA 模式），修改 `main/config.h`：

```c
#define WIFI_STA_ENABLE   1
#define WIFI_STA_SSID     "your_ssid"
#define WIFI_STA_PASSWORD "your_password"
```

---

## HTTP API 汇总

| 路径 | 方法 | 说明 |
|------|------|------|
| `/` | GET | 导航首页 |
| `/ws` | WS | ADS-B 实时数据推送 |
| `/api/aircraft` | GET | 当前飞机列表 JSON |
| `/api/stats` | GET | ADS-B 统计信息 |
| `/fm/start` | POST | 启动 FM 接收 |
| `/fm/stop` | POST | 停止 FM 接收 |
| `/fm/freq` | POST | 设置 FM 频率 |
| `/fm/audio` | GET | PCM 音频流 |
| `/fm/status` | GET | FM 状态 |
| `/spectrum/ws` | WS | 频谱实时数据 |
| `/spectrum/start` | POST | 启动频谱仪 |
| `/spectrum/stop` | POST | 停止频谱仪 |
| `/ais/ws` | WS | AIS 实时船只数据 |
| `/ais/start` | POST | 启动 AIS 接收 |
| `/ais/stop` | POST | 停止 AIS 接收 |
| `/ais/vessels` | GET | 当前船只列表 JSON |
| `/ais/status` | GET | AIS 统计信息 |
| `/drone/ws` | WS | 无人机监测实时数据 |
| `/drone/start` | POST | 启动无人机扫描 |
| `/drone/stop` | POST | 停止无人机扫描 |
| `/drone/status` | GET | 频段状态 + 告警事件 |
| `/drone/clear` | POST | 清除告警日志 |
| `/drone/config` | GET/POST | 读取/设置告警阈值 |

---

## 注意事项

1. **RTL-SDR 同一时刻只能工作在一个频率**，各模块互斥使用，启动一个模块前请停止其他模块。
2. **AIS 接收**需要 VHF 天线（162 MHz），普通 1090 MHz 天线效果较差。
3. **无人机检测**基于 RF 能量检测，存在误报可能（其他 ISM 设备也使用 433/868/915 MHz）。建议在空旷环境中先建立噪底基线再使用。
4. **FM 音频**通过 HTTP 流传输，需要浏览器支持 Web Audio API（现代浏览器均支持）。
5. ESP32-S3 的 USB OTG 与 USB 串口下载共用引脚，烧录时需断开 RTL-SDR。

---

## License

MIT License