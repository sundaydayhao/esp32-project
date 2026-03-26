# ESP32-S3 模拟示波器

基于 ESP32-S3 + ESP-IDF 框架实现的双通道模拟示波器，通过 WiFi 在网页浏览器中实时显示波形。

## 功能特性

- **双通道 ADC 采样**：CH1 (GPIO1) 和 CH2 (GPIO2)，12位精度
- **可调采样率**：1 kSPS ~ 200 kSPS
- **WiFi AP 模式**：ESP32-S3 创建热点，无需路由器
- **WebSocket 实时传输**：二进制帧，低延迟
- **网页示波器界面**：
  - 实时波形显示（线条/点/填充模式）
  - 可调时基（100 µs/格 ~ 100 ms/格）
  - 可调电压灵敏度（100 mV/格 ~ 3.3 V/格）
  - 触发功能（上升沿/下降沿，可调电平）
  - 余辉效果
  - 自动测量（Vpp、Vmax、Vmin、Vrms、频率）
  - 鼠标光标读数
  - 单次触发模式
  - 演示模式（未连接时显示模拟波形）

## 硬件连接

| 信号 | ESP32-S3 引脚 | 说明 |
|------|--------------|------|
| CH1  | GPIO1 (ADC1_CH0) | 通道1输入，0~3.3V |
| CH2  | GPIO2 (ADC1_CH1) | 通道2输入，0~3.3V |
| GND  | GND | 公共地 |

> ⚠️ **注意**：ADC 输入电压范围 0~3.3V，请勿超过此范围，否则会损坏芯片！

## 软件架构

```
esp32-oscilloscope/
├── CMakeLists.txt          # 顶层 CMake
├── sdkconfig.defaults      # 默认配置
├── partitions.csv          # 分区表
├── main/
│   ├── CMakeLists.txt      # 组件 CMake（嵌入 web 文件）
│   ├── main.c              # 入口：初始化各模块
│   ├── adc_sampler.c/h     # ADC 采样（双缓冲，核心1）
│   ├── wifi_ap.c/h         # WiFi AP 模式
│   └── http_server.c/h     # HTTP + WebSocket 服务器（核心0）
└── web/
    ├── index.html          # 示波器界面
    ├── style.css           # 深色主题样式
    └── app.js              # 波形渲染引擎
```

### 数据流

```
ADC (Core 1) → 双缓冲 → WebSocket广播任务 (Core 0) → 浏览器
                                                        ↓
                                              Canvas 波形渲染
```

### WebSocket 二进制帧格式

```
偏移  大小   内容
0     2B    魔数 0xAD 0xC1
2     2B    采样点数 (uint16 LE)
4     4B    实际采样率 Hz (uint32 LE)
8     4B    时间戳 ms (uint32 LE)
12    N*4B  交错数据: ch1[0],ch2[0],ch1[1],ch2[1],... (uint16 LE)
```

## 编译与烧录

### 环境要求

- ESP-IDF v5.x（推荐 v5.2+）
- Python 3.8+
- CMake 3.16+

### 编译步骤

```bash
# 1. 设置 IDF 环境
. $IDF_PATH/export.sh   # Linux/macOS
# 或
%IDF_PATH%\export.bat   # Windows

# 2. 设置目标芯片
idf.py set-target esp32s3

# 3. 编译
idf.py build

# 4. 烧录
idf.py -p /dev/ttyUSB0 flash monitor
# Windows: idf.py -p COM3 flash monitor
```

### 可选配置

```bash
# 打开配置菜单
idf.py menuconfig
```

主要配置项：
- `Component config → ESP-IDF → WiFi` - WiFi 参数
- `Component config → HTTP Server` - WebSocket 支持

## 使用方法

1. 烧录固件后，ESP32-S3 会创建 WiFi 热点：
   - **SSID**: `Oscilloscope`
   - **密码**: `12345678`

2. 手机或电脑连接该 WiFi

3. 打开浏览器，访问 `http://192.168.4.1`

4. 将待测信号接到 GPIO1（CH1）或 GPIO2（CH2），GND 接地

5. 调整时基、电压灵敏度和触发设置

## 键盘快捷键

| 按键 | 功能 |
|------|------|
| `空格` | 运行/停止切换 |
| `S` | 单次触发 |

## API 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET  | `/` | 示波器网页 |
| GET  | `/api/info` | 设备信息 JSON |
| POST | `/api/config` | 设置采样率 `{"sample_rate": 50000}` |
| WS   | `/ws` | WebSocket 数据流 |

## 性能参数

| 参数 | 值 |
|------|-----|
| ADC 分辨率 | 12 bit (0~4095) |
| 最大采样率 | ~200 kSPS (单通道) |
| 缓冲区大小 | 1024 点/帧 |
| WebSocket 帧大小 | ~4 KB |
| 最大并发客户端 | 4 |

## 注意事项

1. ESP32-S3 的 ADC 在高频时精度会下降，建议测量频率 < 50 kHz
2. ADC 存在非线性，已启用校准补偿
3. 两个通道共用 ADC1，采样时序为交替采样
4. 如需更高精度，可外接 SPI ADC（如 MCP3204）

## License

MIT License