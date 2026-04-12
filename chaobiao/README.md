# ESP32-CAM Gas Meter Email Notifier

基于 `ESP-IDF v5.5.3` 的 `ESP32-CAM` 定时拍照邮件通知项目。

设备启动后会：

- 连接配置好的 Wi-Fi 热点
- 初始化摄像头
- 按设定时间间隔拍照
- 通过 SMTP 邮件把图片作为附件发送到指定邮箱

## 支持范围

- 目标芯片：`esp32`
- 默认板型：`AI-Thinker ESP32-CAM`
- 默认摄像头引脚：AI-Thinker ESP32-CAM 引脚定义

## 环境要求

- ESP-IDF `v5.5.3`
- 一块带摄像头和 PSRAM 的 `ESP32-CAM`
- 可用的 2.4 GHz Wi-Fi 热点
- 可用的 SMTP 发件邮箱
- 发件邮箱的 SMTP 授权码或应用专用密码

## 配置方法

先进入项目目录，然后打开配置界面：

```bash
idf.py menuconfig
```

配置入口在 `Gas Meter Camera Push Configuration` 菜单下。

重点配置项：

- `WiFi SSID`
- `WiFi password`
- `SMTP server`
- `SMTP port`
- `Use STARTTLS`
- `SMTP sender email`
- `SMTP sender auth code / app password`
- `SMTP recipient email`
- `Capture interval (minutes)`
- `Camera frame size`
- `JPEG quality`
- `Camera board profile`
- `Camera pin configuration`

默认收件人已经改成 `zhanghao163mail@163.com`。如果你准备用同一个 `163` 邮箱发给自己，通常只需要再填写发件邮箱的 SMTP 授权码。

当前默认 SMTP 参数假设使用 `163` 邮箱：

- `SMTP server`: `smtp.163.com`
- `SMTP port`: `465`
- `Use STARTTLS`: `No`

如果你用的是其他邮箱服务商，请按它们的 SMTP 参数改掉这三项。

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

## 邮件通知行为

- 正常拍照时，会发送一封带 JPEG 附件的邮件
- 启动完成后，会先发送一封纯文本启动通知
- 摄像头初始化失败、拍照失败、邮件发送失败时，也会发送纯文本告警

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
- 每次拍照成功后会尝试发送带附件的邮件

主流程在 `main/main.c`，Wi-Fi 连接逻辑在 `main/wifi_connect.c`，邮件发送逻辑在 `main/email_sender.c`。

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

### 3. 邮件发送失败

优先检查：

- `SMTP server`
- `SMTP port`
- `Use STARTTLS`
- `SMTP sender email`
- `SMTP sender auth code / app password`
- Wi-Fi 是否可正常联网

### 4. Wi-Fi 连不上

检查：

- `WiFi SSID`
- `WiFi password`
- 热点是否为 2.4 GHz
- 信号强度是否足够
