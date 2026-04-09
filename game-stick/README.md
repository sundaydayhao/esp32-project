# ESP32 GameStick

使用 ESP32 作为主控，通过游戏摇杆（Joystick）控制网页上的游戏（俄罗斯方块 / 2048）。基于 **ESP-IDF** 框架开发。

---

## 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                      ESP32                              │
│                                                         │
│  ┌──────────┐   ADC    ┌──────────────┐                │
│  │ Joystick │ ──────►  │ Joystick     │                │
│  │ KY-023   │          │ Task (50ms)  │                │
│  └──────────┘          └──────┬───────┘                │
│                               │ JSON via WebSocket      │
│  ┌──────────────────────────────────────────────────┐  │
│  │  HTTP Server (port 80)                           │  │
│  │   GET /        → index.html  (游戏选择页)        │  │
│  │   GET /tetris  → tetris.html (俄罗斯方块)        │  │
│  │   GET /2048    → game2048.html (2048)            │  │
│  │   GET /ws      → WebSocket endpoint              │  │
│  └──────────────────────────────────────────────────┘  │
│                                                         │
│  WiFi SoftAP: SSID=GameStick  Password=12345678        │
│  IP: 192.168.4.1                                        │
└─────────────────────────────────────────────────────────┘
              │ WiFi
              ▼
        手机 / 电脑浏览器
        http://192.168.4.1
```

---

## 硬件接线

| 摇杆引脚 | ESP32 引脚 | 说明 |
|---------|-----------|------|
| VCC     | 3.3V      | 电源 |
| GND     | GND       | 地   |
| VRx     | GPIO34    | X 轴模拟量（ADC1_CH6） |
| VRy     | GPIO35    | Y 轴模拟量（ADC1_CH7） |
| SW      | GPIO0     | 按钮（低电平有效，内部上拉） |

> 如需修改引脚，编辑 `main/joystick.c` 中的宏定义。

---

## 项目结构

```
esp32-gamestick/
├── CMakeLists.txt          # 顶层 CMake
├── sdkconfig.defaults      # 默认配置（WebSocket、Flash 等）
├── main/
│   ├── CMakeLists.txt      # 组件注册 + 嵌入 HTML 文件
│   ├── main.c              # 入口：初始化 + 摇杆轮询任务
│   ├── wifi_ap.c/h         # WiFi SoftAP 初始化
│   ├── joystick.c/h        # ADC 读取 + 方向判断
│   ├── http_server.c/h     # HTTP + WebSocket 服务器
│   └── web/
│       ├── index.html      # 游戏选择页
│       ├── tetris.html     # 俄罗斯方块（Canvas）
│       └── game2048.html   # 2048 游戏
└── README.md
```

---

## 编译与烧录

### 环境要求

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/get-started/)
- CMake ≥ 3.16

### 步骤

```bash
# 1. 设置 IDF 环境
. $IDF_PATH/export.sh          # Linux/macOS
# 或
%IDF_PATH%\export.bat          # Windows

# 2. 配置目标芯片
idf.py set-target esp32

# 3. （可选）自定义配置
idf.py menuconfig

# 4. 编译
idf.py build

# 5. 烧录并监视串口
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## 使用方法

1. 烧录固件后，ESP32 会创建名为 **GameStick** 的 WiFi 热点（密码：`12345678`）。
2. 手机或电脑连接该 WiFi。
3. 打开浏览器，访问 **http://192.168.4.1**。
4. 选择游戏，用摇杆控制：

| 摇杆动作 | 俄罗斯方块 | 2048 |
|---------|-----------|------|
| 上推    | 旋转方块   | 向上滑动 |
| 下推    | 加速下落   | 向下滑动 |
| 左推    | 左移       | 向左滑动 |
| 右推    | 右移       | 向右滑动 |
| 按下按钮 | 硬降（立即落底） | — |

> 键盘方向键也可以控制游戏（调试用）。

---

## WebSocket 协议

ESP32 → 浏览器（JSON 文本帧）：

```json
{"dir":"UP"}
{"dir":"DOWN"}
{"dir":"LEFT"}
{"dir":"RIGHT"}
{"dir":"BUTTON"}
{"dir":"NONE"}
```

- 方向变化时立即发送
- 持续偏转时每 200ms 重复发送一次
- 回中时发送 `NONE`

---

## 许可证

MIT License