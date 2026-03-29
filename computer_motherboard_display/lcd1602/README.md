# ESP32 / ESP32-S3 + LCD12864 实时时钟与天气显示

基于 ESP32 / ESP32-S3 + LCD12864（ST7920 控制器）的实时时钟与天气信息显示项目。

已验证源码层面兼容以下模组：

- `ESP-WROOM-32D`
- `ESP32-S3-WROOM-1`

说明：这是“同一套源码兼容双目标”，不是“一份固件同时刷两种芯片”。编译前需要先执行对应的 `idf.py set-target`。

## 功能特性

- **实时时钟**：通过 NTP 自动同步北京时间（CST+8），每秒刷新
- **日期显示**：年月日 + 星期
- **实时天气**：调用和风天气 API，显示天气状况、温度、湿度、风速
- **自动重连**：WiFi 断线自动重连，天气/NTP 定时更新
- **8位并行驱动**：直接操作 GPIO 数据总线，兼容 5V LCD12864 模块

## 显示布局

```
┌────────────────────────────────┐
│  行0:    12:34:56              │  实时时间（NTP同步）
│  行1:  2026-03-26 Thu          │  日期 + 星期
│  行2:  晴        25°C          │  天气状况 + 温度
│  行3:  Hum: 60%  W: 10km       │  湿度 + 风速
└────────────────────────────────┘
```

> ST7920 内置 GB2312 字库，天气状况中文（如"晴"、"多云"）可直接显示。

## 硬件连接

### 所需硬件

| 器件 | 说明 |
|------|------|
| ESP32 开发板 | `ESP-WROOM-32D`、`ESP32-S3-WROOM-1`，或其它可用 GPIO 足够的 ESP32 / ESP32-S3 开发板 |
| LCD12864 | ST7920 控制器，**5V 8位并行接口** |
| 杜邦线 | 若干 |

### 引脚连接（默认）

| ESP32 GPIO | LCD12864 引脚 | 说明 |
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
| GPIO 4    | RST          | 复位（低电平有效） |
| **GND**   | **RW**       | **必须接GND**（始终写模式） |
| **5V**    | **PSB**      | **必须接5V/VCC**（选择并行模式） |
| 5V        | VCC          | 电源 |
| GND       | GND          | 地 |

> ⚠️ **重要注意事项：**
> 1. **RW 引脚必须接 GND**：始终保持写模式，防止 LCD12864 输出 5V 信号反向损坏 ESP32
> 2. **PSB 引脚必须接 VCC（5V）**：选择 8 位并行模式（PSB=0 为串行模式）
> 3. **电平兼容**：ESP32 输出 3.3V，LCD12864 的 5V TTL 输入阈值约 2.0V，可直接连接无需电平转换

引脚可通过 `idf.py menuconfig` → `LCD12864 Clock Configuration` → `LCD12864 Pin Configuration (8-bit Parallel)` 修改。

## 软件配置

### 1. 配置 WiFi 和天气 API

```bash
idf.py menuconfig
```

进入 `LCD12864 Clock Configuration`：

| 配置项 | 说明 |
|--------|------|
| WiFi SSID | 你的 WiFi 名称 |
| WiFi Password | 你的 WiFi 密码 |
| QWeather API Key | 和风天气 API Key（见下方说明） |
| Location ID | 城市代码（默认 101010100 北京） |
| Weather update interval | 天气更新间隔（分钟，默认30） |
| NTP re-sync interval | NTP 重同步间隔（分钟，默认60） |

### 2. 获取和风天气 API Key

1. 访问 [https://dev.qweather.com](https://dev.qweather.com) 注册账号
2. 创建应用，选择**免费订阅**（每天 1000 次请求）
3. 复制 API Key 填入 menuconfig

### 3. 常用城市 Location ID

| 城市 | Location ID |
|------|------------|
| 北京 | 101010100 |
| 上海 | 101020100 |
| 广州 | 101280101 |
| 深圳 | 101280601 |
| 杭州 | 101210101 |
| 成都 | 101270101 |
| 武汉 | 101200101 |
| 西安 | 101110101 |

更多城市 ID 查询：[https://github.com/qwd/LocationList](https://github.com/qwd/LocationList)

## 编译与烧录

```bash
# 进入项目目录
cd computer_motherboard_display/lcd12864

# 设置目标芯片（二选一）
idf.py set-target esp32
# 或
idf.py set-target esp32s3

# 配置参数
idf.py menuconfig

# 编译
idf.py build

# 烧录并监视串口
idf.py -p COM3 flash monitor
```

> Windows 下将 `COM3` 替换为实际串口号；Linux/macOS 使用 `/dev/ttyUSB0` 等。

## 项目结构

```
lcd12864/
├── CMakeLists.txt          # 顶层构建文件
├── sdkconfig.defaults      # SDK 默认配置
├── README.md               # 本文档
└── main/
    ├── CMakeLists.txt      # 组件构建文件
    ├── Kconfig.projbuild   # menuconfig 配置项
    ├── main.c              # 主程序入口，任务创建与显示逻辑
    ├── lcd12864.h/.c       # ST7920 8位并行驱动
    ├── wifi_manager.h/.c   # WiFi STA 连接管理
    ├── ntp_client.h/.c     # NTP 时间同步（SNTP）
    └── weather_client.h/.c # 和风天气 HTTP API 客户端
```

## 任务架构

```
app_main
  ├── 初始化 NVS / LCD / WiFi / NTP / 天气客户端
  └── 创建三个 FreeRTOS 任务：
       ├── ntp_task     (Core 0, Prio 3) — 每60分钟重新同步时间
       ├── weather_task (Core 0, Prio 2) — 每30分钟更新天气
       └── display_task (Core 1, Prio 4) — 每秒刷新屏幕
```

## 常见问题

**Q: 屏幕无显示？**
- 检查 **PSB 引脚是否接 VCC（5V）**（并行模式）
- 检查 **RW 引脚是否接 GND**
- 检查 VCC 是否为 5V（LCD12864 通常需要 5V）
- 检查 D0~D7、RS、E 引脚连接是否正确
- 查看串口日志确认 `LCD12864 initialized OK`

**Q: 时间不准确？**
- 确认 WiFi 已连接
- 确认 NTP 服务器可访问（`ntp.aliyun.com`）
- 查看日志中 `NTP time synchronized` 是否出现

**Q: 天气显示 "No weather"？**
- 确认 API Key 正确
- 确认 Location ID 正确
- 检查网络连接
- 查看日志中的 HTTP 状态码

**Q: 中文天气状况乱码？**
- 和风天气 API 返回 UTF-8 编码，而 ST7920 使用 GB2312
- 当前版本直接写入 API 返回的字节，如出现乱码需在 `weather_client.c` 中添加 UTF-8 → GB2312 转换

## 依赖

- ESP-IDF v5.x
- 组件：`esp_wifi`, `esp_http_client`, `esp_sntp`, `nvs_flash`, `json`（cJSON，ESP-IDF 内置）

## 后续功能
蜂鸣器报时

## License

MIT
