# ESP32 LCD1602 实时时钟与天气显示

基于 ESP32 + LCD1602（HD44780 兼容控制器）的实时时钟与天气信息显示项目。

## 功能特性

- **实时时钟**：通过 NTP 自动同步北京时间（CST+8），每秒刷新
- **日期显示**：年月日 + 星期
- **实时天气**：调用和风天气 API，显示天气状况、温度、湿度、风速
- **自动重连**：WiFi 断线自动重连，天气/NTP 定时更新
- **LCD1602 字符屏**：16列 x 2行，适合标准字符型液晶模块

## 显示布局

```
┌────────────────┐
│ 12:34:56 SUNNY │
│ 03-26 THU      │
└────────────────┘
```

## 硬件连接

### 所需硬件

| 器件 | 说明 |
|------|------|
| ESP32 开发板 | 任意 GPIO 足够的 ESP32 开发板 |
| LCD1602 | HD44780 兼容控制器，**8位并行接口** |
| 杜邦线 | 若干 |
| 10k 电位器 | 用于对比度调节 |

### 引脚连接（默认）

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

## 软件配置

### 1. 配置 WiFi 和天气 API

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
- 检查 RW 引脚是否接 GND
- 检查 VDD 是否为 5V
- 检查 D0~D7、RS、E 引脚连接是否正确
- 查看串口日志确认 LCD1602 已初始化

**Q: 时间不准确？**
- 确认 WiFi 已连接
- 确认 NTP 服务器可访问（`ntp.aliyun.com`）
- 查看日志中 `NTP time synchronized` 是否出现

**Q: 天气显示异常？**
- 确认 API Key 正确
- 确认 Location ID 正确
- 检查网络连接
- 查看日志中的 HTTP 状态码

## 依赖

- ESP-IDF v5.x
- 组件：`esp_wifi`, `esp_http_client`, `esp_sntp`, `nvs_flash`, `json`

## License

MIT