# ESP32-S3 门卫系统 🏠

基于 ESP32-S3 的智能家门口监控系统，检测到有人活动或快递到达时，自动拍照并通过**微信**推送通知。

## 功能特性

| 功能 | 说明 |
|------|------|
| 🚨 运动检测 | PIR 传感器 + 摄像头帧差法双重检测 |
| 📷 自动拍照 | 检测到活动时立即拍摄 JPEG 图像 |
| 📦 快递检测 | 人员离开后自动判断是否有包裹留下 |
| 📱 微信推送 | 通过 Server酱 推送通知到微信（含图像链接） |
| 🌐 本地 Web | 内置 HTTP 服务器，局域网内直接查看图像 |
| 🔄 自动重连 | WiFi 断线自动重连，系统稳定运行 |

## 硬件需求

| 组件 | 说明 |
|------|------|
| ESP32-S3 开发板 | 需要带 PSRAM（如 ESP32-S3-DevKitC-1、ESP32-S3-EYE） |
| OV2640 摄像头 | 标准 24-pin FPC 接口 |
| PIR 传感器 | HC-SR501 或类似型号（可选，提高检测精度） |
| 状态 LED | 可选，使用 ESP32-S3 内置 LED（GPIO48） |

## 默认引脚配置（ESP32-S3-EYE）

| 信号 | GPIO | 信号 | GPIO |
|------|------|------|------|
| XCLK | 15 | D0 | 11 |
| SIOD | 4  | D1 | 9  |
| SIOC | 5  | D2 | 8  |
| VSYNC | 6 | D3 | 10 |
| HREF | 7  | D4 | 12 |
| PCLK | 13 | D5 | 18 |
| PIR  | 2  | D6 | 17 |
| LED  | 48 | D7 | 16 |

> 所有引脚均可在 `idf.py menuconfig` 中修改

## 快速开始

### 1. 安装 ESP-IDF

```bash
# 安装 ESP-IDF v5.x
# 参考: https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/
```

### 2. 获取 Server酱 SendKey（微信推送）

1. 访问 [https://sct.ftqq.com](https://sct.ftqq.com)
2. 用微信扫码登录
3. 复制你的 **SendKey**（格式：`SCT...`）

### 3. 配置项目

```bash
cd esp32-doorman
idf.py menuconfig
```

在菜单中配置：
- **门卫系统配置 → WiFi 设置** → 填入家庭 WiFi SSID 和密码
- **门卫系统配置 → 微信通知设置** → 填入 Server酱 SendKey
- **门卫系统配置 → 摄像头引脚配置** → 根据你的硬件调整引脚
- **门卫系统配置 → 运动检测设置** → 配置 PIR GPIO 和冷却时间

### 4. 编译和烧录

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录并监控串口
idf.py -p COM3 flash monitor
# Linux/Mac: idf.py -p /dev/ttyUSB0 flash monitor
```

### 5. 查看效果

设备启动后：
1. **微信**会收到"✅ 门卫系统已上线"通知，包含设备 IP
2. 在**同一局域网**内，浏览器访问 `http://设备IP/` 查看监控页面
3. 门口有人活动时，微信收到通知，点击链接查看抓拍图像

## 系统架构

```
┌─────────────────────────────────────────────────────┐
│                   ESP32-S3 门卫系统                   │
│                                                     │
│  ┌──────────┐    ┌──────────┐    ┌──────────────┐  │
│  │PIR 传感器 │    │ OV2640   │    │  帧差算法    │  │
│  │(硬件检测) │    │ 摄像头   │    │ (软件检测)   │  │
│  └────┬─────┘    └────┬─────┘    └──────┬───────┘  │
│       │               │                  │          │
│       └───────────────┴──────────────────┘          │
│                        │                            │
│               ┌────────▼────────┐                   │
│               │  运动检测器      │                   │
│               │ motion_detector │                   │
│               └────────┬────────┘                   │
│                        │ 检测到活动                  │
│               ┌────────▼────────┐                   │
│               │   拍摄照片       │                   │
│               │ camera_manager  │                   │
│               └────────┬────────┘                   │
│                        │                            │
│          ┌─────────────┴─────────────┐              │
│          │                           │              │
│  ┌───────▼────────┐       ┌──────────▼──────────┐  │
│  │  图像 HTTP 服务 │       │    微信通知          │  │
│  │  image_server  │       │  notification        │  │
│  │  :80/          │       │  Server酱 API        │  │
│  └────────────────┘       └─────────────────────┘  │
│                                                     │
└─────────────────────────────────────────────────────┘
```

## 微信通知示例

**门口有人时：**
```
🚨 门口有动静！

## 🚨 门口有人活动

**时间**: 3600 秒（设备运行时间）
**查看图像**: 点击查看实时画面
**最新图像**: 直接查看
**详情**: 图像大小: 45678 字节，帧差: 12.3%

> 来自 ESP32-S3 门卫系统
```

**快递到达时：**
```
📦 门口有快递！

## 📦 快递到达

**时间**: 7200 秒（设备运行时间）
**查看图像**: 点击查看实时画面
...
```

## Web 监控页面

访问 `http://设备IP/` 可以看到：
- 实时状态（IP、图像数量）
- 最新抓拍图像（带事件标签）
- 历史图像网格（最近 10 张）
- 页面每 10 秒自动刷新

其他接口：
- `GET /latest` - 直接获取最新 JPEG 图像
- `GET /image/{id}` - 获取指定序号的图像
- `GET /status` - JSON 格式系统状态

## 项目文件结构

```
esp32-doorman/
├── CMakeLists.txt              # 项目 CMake 配置
├── sdkconfig.defaults          # ESP-IDF 默认配置
├── partitions.csv              # Flash 分区表
├── README.md                   # 本文档
├── .vscode/
│   ├── c_cpp_properties.json   # VS Code C/C++ 路径配置
│   ├── settings.json           # VS Code 项目设置
│   └── extensions.json         # 推荐安装的扩展
└── main/
    ├── CMakeLists.txt          # 主组件 CMake
    ├── idf_component.yml       # 组件依赖（esp32_camera）
    ├── Kconfig.projbuild       # menuconfig 配置项
    ├── main.c                  # 主程序入口
    ├── wifi_manager.c/h        # WiFi 连接管理
    ├── camera_manager.c/h      # 摄像头控制
    ├── motion_detector.c/h     # 运动检测（PIR + 帧差）
    ├── image_server.c/h        # 本地 HTTP 图像服务器
    └── notification.c/h        # 微信通知（Server酱）
```

## 常见问题

**Q: 摄像头初始化失败？**
- 检查摄像头 FPC 排线是否插好
- 在 menuconfig 中确认引脚配置与实际硬件一致
- 确保使用的是带 PSRAM 的 ESP32-S3 模块

**Q: 微信收不到通知？**
- 确认 Server酱 SendKey 已正确配置（menuconfig）
- 确认设备已连接 WiFi（串口日志会显示 IP）
- 访问 `https://sct.ftqq.com` 检查 SendKey 是否有效

**Q: PIR 传感器不触发？**
- 检查 PIR 传感器 VCC/GND/OUT 接线
- 确认 PIR GPIO 配置正确（默认 GPIO2）
- 软件帧差检测仍会独立工作

**Q: VS Code 显示头文件找不到？**
- 安装 ESP-IDF VS Code 扩展（`espressif.esp-idf-extension`）
- 运行 `idf.py build` 生成 `build/config/sdkconfig.h`
- 重启 VS Code，IntelliSense 会自动更新

## 许可证

MIT License