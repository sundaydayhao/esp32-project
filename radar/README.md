# ESP32-S3 超声波雷达 (Ultrasonic Radar)

基于 ESP-IDF 框架，使用 ESP32-S3 + HC-SR04 超声波传感器 + SG90 舵机实现 180° 扫描雷达，并通过 WebSocket 将数据实时推送到浏览器雷达图页面。

---

## 硬件连接

| 模块 | 引脚 | ESP32-S3 GPIO |
|------|------|---------------|
| SG90 舵机 | 信号线 (橙/黄) | GPIO **14** |
| SG90 舵机 | VCC (红) | 5V |
| SG90 舵机 | GND (棕/黑) | GND |
| HC-SR04 | TRIG | GPIO **16** |
| HC-SR04 | ECHO | GPIO **17** |
| HC-SR04 | VCC | 5V |
| HC-SR04 | GND | GND |

> ⚠️ HC-SR04 ECHO 输出为 5V 电平，ESP32-S3 GPIO 为 3.3V 容忍。建议通过分压电阻（10kΩ + 20kΩ）或电平转换模块降压后再接 ECHO 引脚。

### ECHO 分压接线图

推荐使用 `10kΩ + 20kΩ` 电阻分压，将 HC-SR04 的 `5V ECHO` 信号降到约 `3.3V` 后再接入 ESP32-S3。

```text
HC-SR04 ECHO (5V) ----[10kΩ]----+----> GPIO17 (ESP32-S3)
                                |
                              [20kΩ]
                                |
                               GND
```

输出电压约为：

```text
Vout = 5V x 20k / (10k + 20k) = 3.33V
```

注意：

- `10kΩ` 接在 `ECHO` 和 `GPIO17` 之间
- `20kΩ` 接在 `GPIO17` 和 `GND` 之间
- `ESP32-S3`、`HC-SR04`、`SG90` 必须共地
- 如果使用电平转换模块，也可以把 `ECHO` 先接到转换模块，再输出到 `GPIO17`

### 舵机供电说明

`SG90` 舵机建议使用独立 `5V` 电源供电，不要直接从 ESP32-S3 开发板的 `5V` 引脚取电。舵机启动和转动时电流冲击较大，容易导致开发板 `5V` 电压下跌、舵机不转、ESP32-S3 重启，或者 WebSocket 连接不稳定。

推荐接法：

- `SG90 VCC` 接独立 `5V` 电源正极
- `SG90 GND` 接独立 `5V` 电源负极
- `SG90 SIGNAL` 接 `GPIO14`
- `ESP32-S3 GND` 与独立 `5V` 电源负极共地

示意图：

```text
独立 5V 电源 +  ------------------> SG90 VCC
独立 5V 电源 -  ----+-------------> SG90 GND
                    |
ESP32-S3 GND  ------+
ESP32-S3 GPIO14 ------------------> SG90 SIGNAL
```

---

## 软件架构

```
esp32-radar/
├── CMakeLists.txt          # 根 CMake 文件
├── sdkconfig.defaults      # 默认配置（启用 WebSocket、SoftAP）
└── main/
    ├── CMakeLists.txt      # 组件 CMake（嵌入 HTML 页面）
    ├── main.c              # 应用入口
    ├── wifi_ap.c/h         # WiFi SoftAP（SSID: ESP32-Radar）
    ├── web_server.c/h      # HTTP 服务器 + WebSocket 广播
    ├── servo.c/h           # LEDC PWM 舵机控制
    ├── ultrasonic.c/h      # HC-SR04 驱动
    ├── radar.c/h           # 扫描任务（FreeRTOS Task）
    └── radar_page.html     # 雷达可视化网页（编译时嵌入固件）
```

### 数据流

```
[HC-SR04] ──距离──▶ [radar_task]
[SG90]    ◀──角度── [radar_task]
                         │
                    WebSocket JSON
                    {"a":45,"d":123,"dir":1}
                         │
                    [浏览器 Canvas 雷达图]
```

---

## 编译与烧录

### 环境要求

- ESP-IDF v5.x（推荐 v5.1 或更高）
- ESP32-S3 开发板

### 步骤

```bash
# 1. 设置 IDF 环境
. $IDF_PATH/export.sh          # Linux/macOS
# 或
%IDF_PATH%\export.bat          # Windows

# 2. 设置目标芯片
idf.py set-target esp32s3

# 3. （可选）修改配置
idf.py menuconfig

# 4. 编译
idf.py build

# 5. 烧录并监视串口
idf.py -p COM3 flash monitor    # Windows，替换 COM3 为实际端口
# 或
idf.py -p /dev/ttyUSB0 flash monitor  # Linux
```

---

## 使用方法

1. 烧录完成后，ESP32-S3 会创建 WiFi 热点：
   - **SSID**: `ESP32-Radar`
   - **密码**: `12345678`

2. 手机或电脑连接该 WiFi。

3. 打开浏览器，访问 **http://192.168.4.1**

4. 雷达图页面自动通过 WebSocket 连接并实时显示扫描数据。

---

## 雷达页面说明

| 元素 | 说明 |
|------|------|
| 绿色同心半圆 | 距离刻度（25 / 50 / 75 / 100 cm）|
| 绿色辐射线 | 角度刻度（每 30°）|
| 亮绿色扫描线 | 当前舵机角度 |
| 绿色光点 | 检测到的障碍物（8 秒后淡出）|
| 底部信息栏 | 实时角度、距离、扫描方向 |

---

## 参数调整

| 文件 | 宏 | 说明 |
|------|----|------|
| `servo.c` | `SERVO_GPIO` | 舵机 PWM 引脚（默认 14）|
| `servo.c` | `SERVO_DUTY_MIN/MAX` | 舵机脉宽范围（适配不同型号）|
| `ultrasonic.c` | `TRIG_GPIO` / `ECHO_GPIO` | 超声波引脚（默认 16 / 17）|
| `radar.c` | `ANGLE_STEP` | 扫描步进角度（默认 2°）|
| `radar.c` | `SETTLE_MS` | 舵机稳定等待时间（默认 40 ms）|
| `radar.c` | `MAX_DISTANCE` | 最大显示距离（默认 100 cm）|
| `radar_page.html` | `FADE_MS` | 光点淡出时间（默认 8000 ms）|

---

## WebSocket 数据格式

ESP32 每步发送一条 JSON 消息：

```json
{"a": 45, "d": 123, "dir": 1}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `a` | int | 当前角度 0–180° |
| `d` | int | 距离（cm），等于 100 表示超出范围 |
| `dir` | int | 扫描方向：+1 向右，-1 向左 |
