# IEM-3200 工业级 IoT 环境监测节点

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.x-blue)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![固件版本](https://img.shields.io/badge/固件-v2.1.0-green)]()
[![协议](https://img.shields.io/badge/协议-MQTT%20%7C%20Modbus%20RTU%20%7C%20HTTP-orange)]()

## 概述

IEM-3200 是基于 **ESP32-S3-WROOM-1** 的工业级多参数环境监测节点，支持温度、湿度、气压、CO₂、PM2.5/PM10 等参数的实时采集、本地存储、云端上报与工业总线集成。

---

## 硬件规格

| 组件 | 型号 | 接口 | 功能 |
|------|------|------|------|
| 主控 | ESP32-S3-WROOM-1 | — | 240MHz 双核，4MB Flash |
| 温湿度传感器 | SHT31-DIS | I2C (0x44) | ±0.3°C / ±2%RH |
| 气压传感器 | BME280 | I2C (0x76) | 300~1100 hPa，含海拔估算 |
| CO₂ 传感器 | MH-Z19B | UART1 | NDIR，0~5000 ppm |
| 颗粒物传感器 | PMS5003 | UART2 | PM1.0/2.5/10，激光散射 |
| 显示屏 | SSD1306 OLED | I2C (0x3C) | 128×64，多屏轮播 |
| 工业通信 | RS-485 | UART0 | Modbus RTU 从站 |

### 引脚分配

```
I2C:    SCL=GPIO15  SDA=GPIO21
UART1:  TX=GPIO17   RX=GPIO16   (MH-Z19B)
UART2:  TX=GPIO14   RX=GPIO26   (PMS5003)
UART0:  TX=GPIO1    RX=GPIO3    (Modbus RS-485)
RS485:  DE/RE=GPIO4
LED:    状态=GPIO2  报警=GPIO5
蜂鸣器: GPIO18
PMS SET: GPIO13
BOOT按键: GPIO0
```

---

## 软件架构

```
iot_env_monitor/
├── CMakeLists.txt
├── partitions.csv          # 自定义分区表（支持 OTA 双分区）
├── sdkconfig.defaults      # SDK 默认配置
└── main/
    ├── config.h            # 全局配置（引脚/阈值/周期）
    ├── main.c              # 主程序入口
    ├── sensors/
    │   ├── sht31.c/h       # SHT31 温湿度驱动（CRC-8 校验）
    │   ├── bme280.c/h      # BME280 气压驱动（完整补偿算法）
    │   ├── mhz19.c/h       # MH-Z19B CO₂ 驱动（ABC 控制）
    │   ├── pms5003.c/h     # PMS5003 颗粒物驱动（AQI 计算）
    │   └── sensor_manager.c/h  # 传感器聚合管理（滑动窗口滤波）
    ├── comm/
    │   ├── mqtt_handler.c/h    # MQTT 客户端（LWT/QoS/JSON）
    │   ├── modbus_rtu.c/h      # Modbus RTU 从站（FC03/04/06/10）
    │   └── web_server.c/h      # HTTP REST API + 内嵌仪表盘
    ├── storage/
    │   └── data_logger.c/h     # SPIFFS 环形缓冲区数据记录
    ├── alarm/
    │   └── alarm_manager.c/h   # 阈值报警（迟滞防抖/NVS持久化）
    ├── ota/
    │   └── ota_manager.c/h     # HTTP/HTTPS OTA 升级（自动回滚）
    └── display/
        └── display_manager.c/h # SSD1306 OLED 多屏显示
```

---

## 功能特性

### 传感器层
- **双传感器冗余**：SHT31 为主，BME280 温湿度为备份
- **滑动窗口均值滤波**（窗口=5）消除毛刺
- **传感器健康监控**：连续失败自动降级（OK→WARN→ERROR→OFFLINE）
- **自动重试**：失败后下次采样自动重试

### 通信层
| 协议 | 功能 |
|------|------|
| **MQTT** | 遥测数据上报（QoS 1）、报警事件（QoS 2）、LWT 遗嘱、远程命令订阅 |
| **Modbus RTU** | RS-485 从站，FC03/04/06/10，12 个寄存器映射全部测量值 |
| **HTTP REST** | 7 个 API 端点，内嵌深色主题仪表盘，5s 自动刷新 |

### MQTT 主题结构
```
iem/{device_id}/telemetry   ← 遥测数据（每 10s）
iem/{device_id}/alarm       ← 报警事件
iem/{device_id}/status      ← 在线状态（含 LWT）
iem/{device_id}/cmd         → 远程命令（reboot/ota/calibrate）
```

### Modbus 寄存器映射
| 地址 | 寄存器 | 说明 |
|------|--------|------|
| 40001 | 温度 | °C × 100（有符号 int16）|
| 40002 | 湿度 | %RH × 100 |
| 40003 | 气压整数 | hPa |
| 40004 | 气压小数 | × 100 |
| 40005 | CO₂ 高字 | ppm |
| 40006 | CO₂ 低字 | ppm |
| 40007 | PM1.0 | µg/m³ |
| 40008 | PM2.5 | µg/m³ |
| 40009 | PM10 | µg/m³ |
| 40010 | AQI 等级 | 0~5 |
| 40011 | 报警位图 | 每位对应一个报警 |
| 40012 | 健康位图 | 每 2 位对应一个传感器 |

### 报警系统
- **9 个报警通道**：温度高/低、湿度高/低、CO₂、PM2.5、PM10、气压高/低
- **迟滞防抖**：防止在阈值附近频繁触发
- **连续 3 次确认**：防止瞬时干扰误报
- **阈值持久化**：存储到 NVS，重启后保留
- **多路输出**：MQTT 发布 + 蜂鸣器 + LED + OLED 显示

### 数据存储
- **SPIFFS 环形缓冲区**：最多 1440 条记录（24h × 1min）
- **紧凑二进制格式**：每条记录 32 字节
- **掉电安全**：文件头含写入索引，重启后自动恢复

### OTA 升级
- 支持 HTTP/HTTPS 固件下载
- 双分区 OTA（ota_0 / ota_1）
- 升级失败自动回滚
- 通过 MQTT 命令或 Web API 触发

---

## 快速开始

### 1. 环境准备
```bash
# 安装 ESP-IDF v5.x
. $IDF_PATH/export.sh
```

### 2. 配置 WiFi 和 MQTT
```bash
idf.py menuconfig
# 导航到: Component config → IoT Env Monitor
# 设置 WiFi SSID/Password 和 MQTT Broker URL
```

或通过 NVS 写入（推荐生产环境）：
```bash
# 使用 nvs_partition_gen.py 生成 NVS 分区镜像
```

### 3. 编译与烧录
```bash
cd iot_env_monitor
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### 4. 访问 Web 仪表盘
设备连接 WiFi 后，串口日志会打印 IP 地址：
```
http://<device_ip>/
```

### 5. MQTT 数据订阅
```bash
mosquitto_sub -h broker.hivemq.com -t "iem/+/telemetry" -v
```

---

## Web API 参考

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/data` | 获取最新实时数据 |
| GET | `/api/history?n=60` | 获取最近 60 条历史记录 |
| GET | `/api/alarms` | 获取报警状态和阈值 |
| POST | `/api/thresholds` | 更新报警阈值 |
| POST | `/api/ota` | 触发 OTA 升级 `{"url":"http://..."}` |
| POST | `/api/reboot` | 重启设备 |

---

## 默认报警阈值

| 参数 | 低阈值 | 高阈值 |
|------|--------|--------|
| 温度 | -10°C | 40°C |
| 湿度 | 10%RH | 90%RH |
| CO₂ | — | 1000 ppm |
| PM2.5 | — | 75 µg/m³ |
| PM10 | — | 150 µg/m³ |
| 气压 | 870 hPa | 1060 hPa |

---

## 任务调度

| 任务 | 优先级 | 栈 | 核心 | 周期 |
|------|--------|-----|------|------|
| sensor | 6 | 4KB | Core1 | 5s |
| alarm | 8 | 3KB | Core1 | 1s |
| modbus | 7 | 4KB | Core1 | 实时 |
| mqtt | 5 | 6KB | Core0 | 10s |
| datalog | 4 | 4KB | Core0 | 60s |
| display | 3 | 3KB | Core0 | 2s |
| ota | 2 | 8KB | Core0 | 按需 |

---

## 许可证

MIT License
