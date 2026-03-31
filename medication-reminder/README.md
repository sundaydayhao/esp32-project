# 老年人用药提醒系统

基于 ESP32-S3 的智能用药提醒设备，专为老年人设计，具备大字体显示、声光提醒、WiFi 配置和用药记录功能。

## 硬件配置

| 模块 | 型号 | 接口 |
|------|------|------|
| 主控 | ESP32-S3 | — |
| 显示屏 | ILI9341 TFT 2.4" 240×320 | SPI |
| 实时时钟 | DS3231 | I2C |
| 蜂鸣器 | 有源蜂鸣器 | GPIO 6 |
| 指示灯 | 红色 LED | GPIO 3 |
| 确认按钮 | 轻触开关 | GPIO 1 |
| 暂停按钮 | 轻触开关 | GPIO 2 |

### 引脚分配

```
TFT LCD:
  MOSI → GPIO 11    CLK  → GPIO 12
  CS   → GPIO 10    DC   → GPIO 9
  RST  → GPIO 8     BL   → GPIO 7 (PWM)

DS3231 RTC:
  SDA  → GPIO 4     SCL  → GPIO 5

其他:
  蜂鸣器 → GPIO 6   红色LED → GPIO 3
  确认键 → GPIO 1   暂停键  → GPIO 2
```

## 功能特性

- **最多 6 个每日提醒**：可配置时间、药品名称、剂量
- **提醒升级机制**：
  - 触发 → 5 分钟未确认 → 升级（持续蜂鸣）
  - 升级 → 5 分钟未确认 → 标记为已错过
- **按键操作**：
  - 确认键：标记已服药，停止提醒
  - 暂停键：临时静音蜂鸣器（提醒继续计时）
- **DS3231 RTC**：断电后保持时间，上电自动读取
- **NTP 时间同步**：WiFi 连接后自动同步（阿里云 NTP）
- **用药记录**：保存至 SPIFFS，记录服药/暂停/错过状态
- **Web 配置界面**：通过浏览器配置提醒、查看记录、设置 WiFi

## 显示界面

| 状态 | 显示内容 |
|------|----------|
| 待机 | 大字时间、日期、星期、IP 地址 |
| 提醒触发 | 药品名称、剂量、闪烁边框 |
| 提醒升级 | 红色背景警告、持续蜂鸣 |
| 已确认 | 绿色"已服药"确认界面 |
| 启动 | 欢迎画面 |

## Web 配置界面

设备支持两种 WiFi 模式：

### AP 模式（默认/无法连接时）
1. 手机/电脑连接 WiFi：`MedReminder`（密码：`12345678`）
2. 浏览器访问：`http://192.168.4.1`

### STA 模式（已配置 WiFi）
- 浏览器访问：`http://<设备IP>`

### API 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/reminders` | 获取所有提醒配置 |
| POST | `/api/reminders` | 保存提醒配置（JSON 数组）|
| GET | `/api/log` | 获取用药记录（JSONL 格式）|
| DELETE | `/api/log` | 清空用药记录 |
| POST | `/api/wifi` | 配置 WiFi（`{"ssid":"...","password":"..."}`)  |
| GET | `/api/status` | 获取设备状态（时间、IP、WiFi）|

## 编译与烧录

### 环境要求
- ESP-IDF v5.x
- Python 3.8+

### 编译

```bash
cd medication-reminder
idf.py set-target esp32s3
idf.py menuconfig   # 可选：修改 WiFi/引脚配置
idf.py build
```

### 烧录固件

```bash
idf.py -p COM<端口> flash monitor
```

### 烧录 Web 文件（SPIFFS）

```bash
idf.py -p COM<端口> spiffs_flash
```

或手动：

```bash
python $IDF_PATH/components/spiffs/spiffsgen.py \
  0x100000 spiffs_image spiffs.bin
esptool.py -p COM<端口> write_flash 0x300000 spiffs.bin
```

## 项目结构

```
medication-reminder/
├── CMakeLists.txt
├── partitions.csv          # 分区表（NVS + SPIFFS）
├── sdkconfig.defaults      # 默认配置
├── components/
│   └── ds3231/             # DS3231 RTC 驱动
│       ├── CMakeLists.txt
│       ├── ds3231.h
│       └── ds3231.c
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild   # menuconfig 配置项
│   ├── main.c              # 入口 + FreeRTOS 任务
│   ├── time_manager.c/h    # 时间管理（RTC + NTP）
│   ├── reminder.c/h        # 提醒调度逻辑
│   ├── display.c/h         # ILI9341 显示驱动
│   ├── buzzer.c/h          # 蜂鸣器 + LED 控制
│   ├── storage.c/h         # NVS + SPIFFS 存储
│   ├── wifi_manager.c/h    # WiFi STA/AP 管理
│   └── web_server.c/h      # HTTP REST 服务器
└── spiffs_image/
    └── www/
        ├── index.html      # Web 配置页面
        ├── style.css       # 样式
        └── app.js          # 前端逻辑
```

## 默认提醒配置

| 编号 | 时间 | 药品 | 剂量 | 状态 |
|------|------|------|------|------|
| 1 | 08:00 | 降压药 | 1片 | 启用 |
| 2 | 08:30 | 降糖药 | 2片 | 启用 |
| 3 | 12:00 | 降压药 | 1片 | 启用 |
| 4 | 18:00 | 降压药 | 1片 | 启用 |
| 5 | 09:00 | — | — | 禁用 |
| 6 | 21:00 | — | — | 禁用 |

所有配置保存在 NVS，断电不丢失。

## 注意事项

- DS3231 首次使用需通过 Web 界面连接 WiFi 进行 NTP 同步，之后断网也能保持准确时间
- SPIFFS 用药记录最大约 1MB，建议定期通过 Web 界面清空
- VS Code IntelliSense 会显示 ESP-IDF 头文件找不到的警告，这是正常现象，不影响编译