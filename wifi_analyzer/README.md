# WiFi 信号分析仪

基于 ESP32-S3-WROOM-1 + SSD1306 OLED，使用 ESP-IDF 原生框架开发。

## 功能

- 扫描周边所有 WiFi 网络（含隐藏网络）
- OLED 实时显示信号强度（信号条 + RSSI 值），按板载 BOOT 键翻页
- 开启 AP 热点，手机连接后通过浏览器查看可视化 Web 界面
- 每 10 秒自动重新扫描
- 串口输出详细扫描日志

## 硬件要求

| 元件 | 型号 | 数量 | 价格 |
|------|------|------|------|
| 主控模组 | ESP32-S3-WROOM-1 | 1 | ~35元 |
| OLED | SSD1306，0.96寸，I2C，128×64 | 1 | ~8元 |
| 杜邦线 | 母对母 | 4根 | ~1元 |

## 接线

```
ESP32          SSD1306 OLED
─────          ────────────
3.3V  ──────►  VCC
GND   ──────►  GND
GPIO8  ─────►  SDA
GPIO9  ─────►  SCL
```

> ⚠️ OLED 接 **3.3V**，不要接 5V
> ⚠️ 翻页默认使用板载 `BOOT` 键（`GPIO0`，低电平有效），无需额外接线
> ⚠️ 上电或复位时若持续按住 `BOOT`，芯片可能进入下载模式；正常使用时请在系统启动后再按

## 开发环境

- ESP-IDF **v5.1** 或更高版本
- 安装方式：https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/

## 编译与烧录

```bash
# 进入项目目录
cd wifi_analyzer

# 设置目标芯片
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录（替换 PORT 为实际串口，如 COM3 或 /dev/ttyUSB0）
idf.py -p PORT flash

# 烧录并打开串口监视器
idf.py -p PORT flash monitor
```

## 使用方法

### OLED 界面

```
┌──────────────────────────────┐
│ WiFi Analyzer          N:12  │  ← 发现网络总数
├──────────────────────────────┤
│ MyHomeWiFi        ████  -45  │  ← 4格满信号
│ ChinaNet-xxxx     ███░  -68  │  ← 3格信号
│ TP-LINK_5G        ██░░  -75  │  ← 2格信号
│ <hidden>          █░░░  -88  │  ← 隐藏网络
├──────────────────────────────┤
│ P1/3  Best:CH6 -45dBm        │  ← 翻页信息
└──────────────────────────────┘
```

按板载 **BOOT 键**（GPIO0，低电平有效）翻页查看更多网络。

### Web 界面

1. 手机/电脑连接 WiFi 热点：`WiFi-Analyzer`，密码：`12345678`
2. 浏览器打开：`http://192.168.4.1`
3. 查看信号列表和 2.4G 信道占用图，每 10 秒自动刷新

## 项目结构

```
wifi_analyzer/
├── README.md               本文档
├── CMakeLists.txt          顶层 CMake
├── sdkconfig.defaults      默认 SDK 配置
└── main/
    ├── CMakeLists.txt      main 组件 CMake
    ├── main.c              程序入口
    ├── wifi_scanner.h      WiFi 扫描模块接口
    ├── wifi_scanner.c      WiFi 扫描模块实现
    ├── oled_display.h      OLED 显示模块接口
    ├── oled_display.c      SSD1306 驱动 + 显示逻辑
    ├── web_server.h        Web 服务器模块接口
    └── web_server.c        HTTP 服务器 + JSON API + HTML
```

## 串口输出示例

```
I (352) MAIN: WiFi 分析仪启动
I (1205) WIFI: AP 热点已启动: SSID=WiFi-Analyzer IP=192.168.4.1
I (1210) WEB: HTTP 服务器已启动
I (2350) WIFI: [SCAN] 开始扫描...
I (4120) WIFI: [ 1] SSID: MyHomeWiFi          RSSI: -45 dBm  CH:  6
I (4121) WIFI: [ 2] SSID: ChinaNet-xxxx       RSSI: -68 dBm  CH:  1
I (4122) WIFI: [ 3] SSID: <hidden>            RSSI: -88 dBm  CH: 11
I (4123) WIFI: [SCAN] 完成，共 12 个网络
