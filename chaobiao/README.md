# ESP32-CAM Gas Meter Camera Push

基于 `ESP-IDF v5.5.3` 的 `ESP32-CAM` 定时拍照推送项目。

设备启动后会：

- 连接配置好的 Wi-Fi 热点
- 初始化摄像头
- 按设定时间间隔拍照
- 通过企业微信 Webhook 或 ServerChan 推送通知

## 支持范围

- 目标芯片：`esp32`
- 默认板型：`AI-Thinker ESP32-CAM`
- 默认摄像头引脚：AI-Thinker ESP32-CAM 引脚定义

## 环境要求

- ESP-IDF `v5.5.3`
- 一块带摄像头和 PSRAM 的 `ESP32-CAM`
- 可用的 2.4 GHz Wi-Fi 热点
- 企业微信机器人 Webhook 或 ServerChan SendKey

## 配置方法

先进入项目目录，然后打开配置界面：

```bash
idf.py menuconfig
```

配置入口在 `Gas Meter Camera Push Configuration` 菜单下。

重点配置项：

- `WiFi SSID`
- `WiFi password`
- `WiFi max retry count`
- `WeChat push method`
- `WeCom webhook key`
- `ServerChan SendKey`
- `Capture interval (minutes)`
- `Camera frame size`
- `JPEG quality`
- `Camera board profile`
- `Camera pin configuration`

其中热点名称和密码就在这里设置：

- `WiFi SSID`
- `WiFi password`

这些配置项定义在 [Kconfig.projbuild](/D:/workspace/esp32-chaobiao/main/Kconfig.projbuild)。

## 构建

如果还没有设置 target：

```bash
idf.py set-target esp32
```

编译：

```bash
idf.py build
```

## 烧录

```bash
idf.py -p COM3 flash monitor
```

把 `COM3` 改成你自己的串口号。

## 推送方式

项目支持两种推送方式：

- `WeCom webhook`
  适合把图片直接发到企业微信群机器人
- `ServerChan`
  适合发送文本通知到个人微信

默认使用 `WeCom webhook`。

## 摄像头默认引脚

默认板型为 `AI-Thinker ESP32-CAM`，默认引脚如下：

- `PWDN`: `32`
- `RESET`: `-1`
- `XCLK`: `0`
- `SIOD`: `26`
- `SIOC`: `27`
- `D7`: `35`
- `D6`: `34`
- `D5`: `39`
- `D4`: `36`
- `D3`: `21`
- `D2`: `19`
- `D1`: `18`
- `D0`: `5`
- `VSYNC`: `25`
- `HREF`: `23`
- `PCLK`: `22`

如果你的板子不是 AI-Thinker 版本，可以在 `Camera pin configuration` 中手动修改。

## 运行逻辑

- 启动后先初始化 NVS
- 然后连接 Wi-Fi
- 再初始化摄像头
- 成功后创建周期任务
- 每次拍照前会检查 Wi-Fi 是否仍然在线

主流程在 [main.c](/D:/workspace/esp32-chaobiao/main/main.c)，Wi-Fi 连接逻辑在 [wifi_connect.c](/D:/workspace/esp32-chaobiao/main/wifi_connect.c)。

## 常见问题

### 1. VS Code 里头文件有波浪线

项目已经配置为使用 `build/compile_commands.json`。如果波浪线仍然存在，执行：

- `Developer: Reload Window`
- `C/C++: Reset IntelliSense Database`

### 2. 拍照失败

优先检查：

- 摄像头排线是否接好
- 板型是否真的是 `ESP32-CAM`
- 引脚配置是否匹配你的模块
- 供电是否稳定

### 3. 图片太大发送失败

可以降低：

- `Camera frame size`
- `JPEG quality`

### 4. Wi-Fi 连不上

检查：

- `WiFi SSID`
- `WiFi password`
- 热点是否为 2.4 GHz
- 信号强度是否足够
