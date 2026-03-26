# ESP32 电路板放大镜 (PCB Magnifier)

使用 ESP32-CAM 通过 WiFi 将摄像头画面实时传输到网页，并提供放大镜功能，方便查看电路板上的芯片丝印和细小元件。

## 功能特性

- **实时 MJPEG 视频流** — 低延迟摄像头画面传输
- **圆形放大镜** — 鼠标悬停区域实时放大，倍数 1.5x ~ 12x 可调
- **镜头大小可调** — 120px ~ 360px
- **画面冻结** — 按 `F` 键或点击按钮冻结当前帧，方便仔细查看
- **截图保存** — 将当前帧保存为 PNG 文件（`Ctrl+S`）
- **镜像/翻转** — 水平镜像、垂直翻转
- **分辨率切换** — QVGA / CIF / VGA / SVGA / XGA / UXGA
- **画质调节** — 实时调整 JPEG 压缩质量
- **触摸支持** — 手机/平板触摸操作
- **键盘快捷键** — `F` 冻结，`+/-` 调整倍数，`Ctrl+S` 保存

## 硬件要求

| 组件 | 说明 |
|------|------|
| ESP32-CAM (AI-Thinker) | 推荐，内置 OV2640 摄像头 + PSRAM |
| OV2640 摄像头模块 | 支持 JPEG 输出 |
| 5V 电源 | 建议 500mA 以上 |
| USB-TTL 串口模块 | 用于烧录固件 |

> 也支持 ESP-WROVER-KIT、ESP-EYE、M5Stack Camera 等，通过 menuconfig 选择。

## 快速开始

### 1. 安装 ESP-IDF

确保已安装 ESP-IDF v5.0 或更高版本：
```bash
# 参考官方文档: https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/get-started/
```

### 2. 配置项目

```bash
idf.py menuconfig
```

在 **ESP32 Magnifier Configuration** 菜单中设置：
- `WiFi SSID` — 你的 WiFi 名称
- `WiFi Password` — 你的 WiFi 密码
- `Select Camera Board` — 选择你的摄像头板型（默认 AI-Thinker）
- `Camera Frame Size` — 默认 VGA (640×480)，值 `8`
- `JPEG Quality` — 默认 `10`（值越小画质越好）

### 3. 编译并烧录

```bash
idf.py build
idf.py -p COM3 flash monitor   # Windows，COM3 替换为实际端口
idf.py -p /dev/ttyUSB0 flash monitor  # Linux/macOS
```

### 4. 查看 IP 地址

烧录成功后，串口监视器会输出：
```
I (xxxx) magnifier-main: =========================================
I (xxxx) magnifier-main:   WiFi connected!
I (xxxx) magnifier-main:   IP Address : 192.168.1.xxx
I (xxxx) magnifier-main:   Open browser: http://192.168.1.xxx
I (xxxx) magnifier-main: =========================================
```

### 5. 打开浏览器

在同一 WiFi 网络下，打开浏览器访问 `http://192.168.1.xxx`

## Web API 接口

| 路径 | 说明 |
|------|------|
| `GET /` | 放大镜 Web 界面 |
| `GET /stream` | MJPEG 视频流 |
| `GET /capture` | 单帧 JPEG 截图 |
| `GET /status` | 摄像头状态 JSON |
| `GET /control?var=xxx&val=yyy` | 摄像头参数控制 |

### 控制参数 (`/control`)

| var | 说明 | 范围 |
|-----|------|------|
| `framesize` | 分辨率 | 0-13 |
| `quality` | JPEG 质量 | 4-63 |
| `brightness` | 亮度 | -2~2 |
| `contrast` | 对比度 | -2~2 |
| `sharpness` | 锐度 | -2~2 |
| `hmirror` | 水平镜像 | 0/1 |
| `vflip` | 垂直翻转 | 0/1 |

## AI-Thinker ESP32-CAM 引脚定义

```
PWDN  → GPIO 32    XCLK  → GPIO 0
SIOD  → GPIO 26    SIOC  → GPIO 27
D7    → GPIO 35    D6    → GPIO 34
D5    → GPIO 39    D4    → GPIO 36
D3    → GPIO 21    D2    → GPIO 19
D1    → GPIO 18    D0    → GPIO 5
VSYNC → GPIO 25    HREF  → GPIO 23
PCLK  → GPIO 22
```

> **注意**：AI-Thinker ESP32-CAM 的 GPIO 0 在烧录时需要接 GND，烧录完成后断开。

## 使用技巧

1. **查看芯片丝印**：将摄像头对准芯片，调整放大倍数到 4x~8x，移动鼠标到芯片位置
2. **冻结画面**：按 `F` 键冻结，然后仔细查看放大的细节
3. **保存截图**：按 `Ctrl+S` 保存当前帧为 PNG
4. **提高清晰度**：在分辨率选择器中选择 SVGA 或 XGA，降低 JPEG 质量值（如 6）
5. **调整光线**：确保被拍摄的电路板有足够的光照

## 项目结构

```
esp32-magnifier/
├── CMakeLists.txt          # 根 CMake 配置
├── sdkconfig.defaults      # 默认 SDK 配置
├── README.md
└── main/
    ├── CMakeLists.txt      # 组件 CMake（含 HTML 嵌入）
    ├── idf_component.yml   # 组件依赖（esp32_camera）
    ├── Kconfig.projbuild   # menuconfig 配置项
    ├── main.c              # 入口：WiFi 初始化
    ├── camera_server.h     # 摄像头服务器接口
    ├── camera_server.c     # 摄像头初始化 + HTTP 服务器
    └── index.html          # Web 放大镜界面（编译时嵌入固件）
```

## 常见问题

**Q: 摄像头初始化失败**
- 检查摄像头排线是否插好
- 确认 menuconfig 中选择了正确的摄像头板型
- AI-Thinker 板需要 PSRAM，确保 `CONFIG_ESP32_SPIRAM_SUPPORT=y`

**Q: WiFi 连接失败**
- 检查 SSID 和密码是否正确
- 确保路由器使用 2.4GHz 频段（ESP32 不支持 5GHz）

**Q: 画面卡顿**
- 降低分辨率（使用 VGA 或 CIF）
- 增大 JPEG 质量值（降低画质但减小数据量）
- 确保 ESP32 与路由器距离不太远

**Q: 网页无法打开**
- 确认手机/电脑与 ESP32 在同一 WiFi 网络
- 检查串口输出的 IP 地址是否正确