# ESP32 LCD1602 实时时钟、天气、室内温湿度与语音报时

基于 ESP32 + LCD1602（HD44780 兼容控制器）的综合显示项目，支持天气、DHT11 室内温湿度、MAX98357A 语音报时。

## 功能特性

- **实时时钟**：通过 NTP 自动同步北京时间（CST+8），每秒刷新
- **天气显示**：调用和风天气 API，显示天气状况、温度、湿度、风速
- **室内温湿度**：通过 DHT11 采集室内温湿度
- **双屏轮播显示**：时间/日期屏与室内温湿度屏轮播
- **开机语音播报**：启动后播报当前北京时刻
- **整点语音报时**：在 8:00-20:00 之间执行整点语音报时
- **LCD1602 字符屏**：16列 x 2行，适合标准字符型液晶模块

## 显示布局

当前显示策略：

- **屏幕 1**
  - 第一行：`HH:MM:SS WEATHER`
  - 第二行：`YYYY-MM-DD WEEK`
- **屏幕 2**
  - 第一行：`HH:MM:SS WEATHER`
  - 第二行：`IN:xxC xx%` 或 `IN:xxC xx% OUT:xxC`

示例：

```text
12:34:56 SUNNY
2026-03-26 THU
```

```text
12:34:56 RAIN
IN:25C 60%
```

## 硬件连接

### 所需硬件

| 器件 | 说明 |
|------|------|
| ESP32 开发板 | 任意 GPIO 足够的 ESP32 开发板 |
| LCD1602 | HD44780 兼容控制器，**8位并行接口** |
| DHT11 | 室内温湿度传感器 |
| MAX98357A | I2S 数字功放 |
| 喇叭 | 与 MAX98357A 连接 |
| 杜邦线 | 若干 |
| 10k 电位器 | 用于 LCD 对比度调节 |

### 引脚连接（默认）

#### LCD1602

| ESP32 GPIO | LCD1602 引脚 | 说明 |
|-----------|--------------|------|
| GPIO 12   | D0           | 数据总线 bit0 |
| GPIO 13   | D1           | 数据总线 bit1 |
| GPIO 14   | D2           | 数据总线 bit2 |
| GPIO 15   | D3           | 数据总线 bit3 |
| GPIO 16   | D4           | 数据总线 bit4 |
| GPIO 17   | D5           | 数据总线 bit5 |
| GPIO 18   | D6           | 数据总线 bit6 |
| GPIO 19   | D7           | 数据总线 bit7 |
| GPIO 21   | RS           | 寄存器选择（0=命令，1=数据） |
| GPIO 22   | E            | 使能（下降沿锁存） |
| **GND**   | **RW**       | **必须接 GND**（始终写模式） |
| **GND**   | **VSS**      | 地 |
| **5V**    | **VDD**      | 电源 |
| 电位器中间端 | **V0**    | 对比度输入 |

#### DHT11

| ESP32 GPIO | DHT11 引脚 | 说明 |
|-----------|------------|------|
| GPIO 23   | DATA       | 单总线数据 |
| 3.3V/5V   | VCC        | 供电 |
| GND       | GND        | 地 |

> DHT11 数据线需要上拉电阻。

#### MAX98357A

| ESP32 GPIO | MAX98357A 引脚 | 说明 |
|-----------|----------------|------|
| GPIO 25   | BCLK           | I2S 时钟 |
| GPIO 26   | LRC / WS       | I2S 帧同步 |
| GPIO 27   | DIN            | I2S 数据 |
| 5V        | VIN            | 供电 |
| GND       | GND            | 地 |

### 接线注意事项

- LCD RW 必须接地
- LCD VDD 接 5V，V0 接对比度电位器
- DHT11 数据线需要上拉电阻
- MAX98357A 使用 I2S 连接到 ESP32
- 如果屏幕无显示，优先检查对比度、电源和 RW 接线

## 软件配置

### 1. 配置 WiFi、天气、DHT11 和音频

```bash
idf.py menuconfig
```

进入 `LCD1602 Clock Configuration`：

| 配置项 | 说明 |
|--------|------|
| WiFi SSID | 你的 WiFi 名称 |
| WiFi Password | 你的 WiFi 密码 |
| QWeather API Key | 和风天气 API Key |
| Location ID | 城市代码（默认 101010100 北京） |
| Weather update interval | 天气更新间隔（分钟，默认30） |
| NTP re-sync interval | NTP 重同步间隔（分钟，默认60） |
| Audio I2S BCLK GPIO | MAX98357A BCLK |
| Audio I2S WS/LRCK GPIO | MAX98357A WS/LRCK |
| Audio I2S DOUT GPIO | MAX98357A DIN |
| Hourly announce start hour | 整点报时开始时间，默认 8 |
| Hourly announce end hour | 整点报时结束时间，默认 20 |
| DHT11 data GPIO | DHT11 数据脚 |
| DHT11 update interval | DHT11 采样间隔（秒） |
| Screen switch interval | 屏幕轮播间隔（秒） |

## 编译与烧录

```bash
# 进入项目目录
cd computer_motherboard_display/lcd1602

# 设置目标芯片
idf.py set-target esp32

# 配置参数
idf.py menuconfig

# 编译
idf.py build

# 烧录并监视串口
idf.py -p COM3 flash monitor
```

## 项目结构

```
lcd1602/
├── CMakeLists.txt          # 顶层构建文件
├── sdkconfig.defaults      # SDK 默认配置
├── README.md               # 本文档
└── main/
    ├── CMakeLists.txt      # 组件构建文件
    ├── Kconfig.projbuild   # menuconfig 配置项
    ├── main.c              # 主程序入口，任务创建与显示逻辑
    ├── lcd1602.h/.c        # LCD1602 8位并行驱动
    ├── dht11.h/.c          # 室内温湿度采集
    ├── audio_output.h/.c   # I2S 音频输出（MAX98357A）
    ├── hourly_chime.h/.c   # 整点报时任务
    ├── wifi_manager.h/.c   # WiFi STA 连接管理
    ├── ntp_client.h/.c     # NTP 时间同步（SNTP）
    └── weather_client.h/.c # 和风天气 HTTP API 客户端
```

## 任务架构

```
app_main
  ├── 初始化 NVS / LCD / WiFi / NTP / 天气 / DHT11 / 音频
  └── 创建多个 FreeRTOS 任务：
       ├── ntp_task       (Core 0, Prio 3)
       ├── weather_task   (Core 0, Prio 2)
       ├── dht11_task     (Core 0, Prio 2)
       ├── hourly_chime_task (Core 0, Prio 2)
       └── display_task   (Core 1, Prio 4)
```

## 常见问题

**Q: 屏幕无显示？**
- 检查 LCD RW 是否接 GND
- 检查 VDD 是否为 5V
- 检查 V0 是否正确接入对比度电位器
- 检查 D0~D7、RS、E 引脚连接是否正确
- 查看串口日志确认 LCD1602 已初始化

**Q: DHT11 无数据？**
- 检查 DHT11 数据线是否有上拉电阻
- 检查 GPIO 配置是否正确
- 采样间隔不要过短

**Q: 没有声音？**
- 检查 MAX98357A 的 BCLK/WS/DIN 接线
- 检查喇叭与功放供电
- 确认 WAV 资源已正确编译进固件

**Q: 没有整点报时？**
- 检查 NTP 是否同步成功
- 确认当前时间在 8:00-20:00 区间
- 查看串口日志中的 hourly_chime 输出

## 依赖

- ESP-IDF v5.x
- 组件：`esp_wifi`, `esp_http_client`, `esp_sntp`, `nvs_flash`, `json`, `esp_driver_i2s`

## License

MIT