# ESP32-S3 智能摄像头小车

这是一个基于 ESP-IDF 的 ESP32-S3 摄像头小车工程。设备启动后会创建 Wi-Fi 热点，手机或电脑连接后打开 `http://192.168.4.1`，即可看到摄像头画面并控制小车前进、后退、转向、原地旋转和停止。

## 功能

- ESP32-S3 SoftAP 热点：默认 `ESP32S3-Car` / `12345678`
- OV2640 摄像头 MJPEG 实时视频流
- 双直流电机 PWM 调速，默认适配 TB6612FNG，也可接 L298N
- 网页遥控，支持触摸、鼠标和键盘
- 手机网页按住说话：由手机识别语音，再发送动作命令给小车
- 手机/小车双向对讲：小车麦克风上传声音，手机开关讲话并在小车扬声器播放
- 可选 HC-SR04 超声波避障，前方过近时自动停止前进
- 电机失控保护：超过一段时间未收到控制命令会自动停车
- 所有主要 GPIO 可在 `idf.py menuconfig` 中修改

## 快速烧录

```bash
cd car-camera
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py menuconfig
idf.py build flash monitor
```

启动后连接热点 `ESP32S3-Car`，密码 `12345678`，浏览器打开：

```text
http://192.168.4.1
```

## 手机语音控制

网页右侧有 `按住说话` 按钮。按住按钮说“前进、后退、左转、右转、停止、原地左转、原地右转”，手机浏览器会先做语音识别，再把文本命令发给 ESP32-S3。

同一个接口也可给手机 App 使用：

```text
GET http://192.168.4.1/voice?text=%E5%B7%A6%E8%BD%AC&speed=60
```

返回 JSON 中的 `recognized` 表示是否识别到动作，`cmd` 是最终动作。这个方案不需要 ESP32-S3 接麦克风，和下面的实时对讲功能相互独立。

注意：网页语音依赖手机浏览器的 Web Speech API。若浏览器不允许 `http://192.168.4.1` 页面使用麦克风，可以用原生 App 调用上面的 `/voice` 接口，或继续使用网页上的方向按钮。

## 双向对讲

网页右侧有 `对讲` 区：

- `连接收听`：手机开始接收小车麦克风上传的声音。小车端会先做简单噪声门判断，低于阈值的背景噪声不会上传。
- `开始讲话`：手机麦克风音频通过 WebSocket 发给小车，并从小车扬声器播放。再次点击后停止发送手机声音，但手机仍保持收听小车声音。

App 也可以直接复用同一个接口：

```text
WebSocket ws://192.168.4.1/audio
Binary frame: PCM16 little-endian, 16000 Hz, mono
car -> phone: 小车麦克风音频，经过小车端噪声门
phone -> car: 手机麦克风音频，小车扬声器播放
```

噪声门阈值可在 `idf.py menuconfig` 的 `Smart Camera Car Configuration -> Audio Intercom` 中调整 `Car-side noise gate threshold`。阈值过低会一直上传底噪，过高会吞掉轻声说话。

注意：手机浏览器的麦克风权限通常要求安全上下文。有些浏览器会拒绝 `http://192.168.4.1` 使用麦克风；这种情况下可以用原生 App 连接上面的 WebSocket，或改成 HTTPS/受信任环境。仅收听小车声音通常不需要手机麦克风权限。

## 硬件清单

| 模块 | 建议型号 | 说明 |
| --- | --- | --- |
| 主控 | ESP32-S3 DevKit / ESP32-S3-WROOM，带 PSRAM | 摄像头建议使用带 PSRAM 的模块 |
| 摄像头 | OV2640 DVP/FPC 模块 | 3.3V 供电 |
| 电机驱动 | TB6612FNG | 推荐，逻辑 3.3V 友好 |
| 电机 | TT 减速电机 x2 | 左右轮差速 |
| 电源 | 2S 锂电或 6-12V 电池 + 5V 降压模块 | 电机和 ESP32 不要共用薄弱 USB 供电 |
| 避障 | HC-SR04，可选 | Echo 必须降压到 3.3V |
| 麦克风 | INMP441 I2S 麦克风 | 3.3V 供电 |
| 扬声器功放 | MAX98357A I2S 功放 + 4-8Ω 小喇叭 | VIN 按模块要求接 5V 或 3.3V |

## 总体连线图

```text
         2S/3S Battery
              |
      +-------+--------+
      |                |
   Motor VM        DC-DC 5V
      |                |
 +----v-----+     +----v----------------+
 | TB6612   |     | ESP32-S3 Dev Board  |
 | Motor    |<--->| GPIO PWM/DIR        |
 | Driver   |     | GPIO DVP Camera     |
 +----+-----+     | GPIO I2S Mic/Amp    |
      |           +----+-----+----------+
  Left/Right           |     |
  DC Motors         +--v--+  +----v--------+
                    |OV2640| |INMP441/MAX |
                    +-----+  +-------------+

All GND must be connected together.
```

## 默认 GPIO 连线

### OV2640 摄像头

OV2640 常见排线会把数据脚标为 `Y2` 到 `Y9`。在 esp-camera 中它们对应 `D0` 到 `D7`。

| OV2640 引脚 | ESP32-S3 GPIO |
| --- | --- |
| 3V3 | 3V3 |
| GND | GND |
| XCLK | GPIO15 |
| SIOD / SDA | GPIO4 |
| SIOC / SCL | GPIO5 |
| Y2 / D0 | GPIO11 |
| Y3 / D1 | GPIO9 |
| Y4 / D2 | GPIO8 |
| Y5 / D3 | GPIO10 |
| Y6 / D4 | GPIO12 |
| Y7 / D5 | GPIO18 |
| Y8 / D6 | GPIO17 |
| Y9 / D7 | GPIO16 |
| VSYNC | GPIO6 |
| HREF | GPIO7 |
| PCLK | GPIO13 |
| PWDN | 不接，配置为 -1 |
| RESET | 不接，配置为 -1 |

### TB6612FNG 电机驱动

| TB6612FNG 引脚 | ESP32-S3 / 电源 |
| --- | --- |
| VCC | ESP32-S3 3V3 |
| GND | 公共 GND |
| VM | 电机电源正极 |
| STBY | GPIO41 |
| AIN1 | GPIO1 |
| AIN2 | GPIO2 |
| PWMA | GPIO14 |
| BIN1 | GPIO39 |
| BIN2 | GPIO40 |
| PWMB | GPIO21 |
| A01 / A02 | 左电机 |
| B01 / B02 | 右电机 |

如果使用 L298N：`ENA` 接左 PWM，`IN1/IN2` 接左方向，`ENB` 接右 PWM，`IN3/IN4` 接右方向。L298N 没有 `STBY`，可在 `menuconfig` 中把 `Motor standby GPIO` 设为 `-1`。

### HC-SR04 超声波模块（可选）

| HC-SR04 引脚 | ESP32-S3 / 电源 |
| --- | --- |
| VCC | 5V |
| GND | 公共 GND |
| TRIG | GPIO42 |
| ECHO | GPIO47，必须先经过 5V 到 3.3V 分压 |

Echo 分压示例：

```text
HC-SR04 ECHO -- 10k --+-- ESP32-S3 GPIO47
                      |
                     20k
                      |
                     GND
```

### I2S 麦克风 INMP441

| INMP441 引脚 | ESP32-S3 GPIO / 电源 |
| --- | --- |
| VDD | 3V3 |
| GND | 公共 GND |
| SCK / BCLK | GPIO35 |
| WS / LRCL | GPIO36 |
| SD / DOUT | GPIO37 |
| L/R | GND，使用左声道 |

如果把 `L/R` 接 3V3，请在 `menuconfig` 中打开 `Use right I2S microphone slot`。

### I2S 功放 MAX98357A

| MAX98357A 引脚 | ESP32-S3 GPIO / 电源 |
| --- | --- |
| VIN | 5V 或 3V3，按功放模块说明选择 |
| GND | 公共 GND |
| BCLK | GPIO38 |
| LRC / WS | GPIO45 |
| DIN | GPIO46 |
| SPK+ / SPK- | 4-8Ω 扬声器 |

默认音频引脚没有占用摄像头、电机、超声波和状态灯 GPIO。若你的开发板没有引出 GPIO35/36/37/38/45/46，可在 `menuconfig` 的 `Audio Intercom` 菜单改到可用引脚。

## 重要注意事项

- 电机不要从 ESP32-S3 的 3.3V 引脚取电。
- 电机电源、降压模块和 ESP32-S3 必须共地。
- 麦克风、功放、电机电源和 ESP32-S3 也必须共地。
- 建议在电机驱动电源端并联 470uF 以上电解电容，减少电机启动导致的掉电复位。
- HC-SR04 的 Echo 是 5V 电平，直接接 ESP32-S3 可能损坏芯片。
- 扬声器会被小车麦克风拾音，可能产生回声；把麦克风和扬声器尽量分开，并调高噪声门阈值会更稳。
- 如果你的 ESP32-S3 摄像头板针脚不同，进入 `idf.py menuconfig`，修改 `Smart Camera Car Configuration` 下的摄像头和电机 GPIO。

## 工程结构

```text
car-camera/
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
└── main/
    ├── CMakeLists.txt
    ├── Kconfig.projbuild
    ├── idf_component.yml
    ├── main.c
    ├── wifi_ap.c/h
    ├── camera_app.c/h
    ├── motor_control.c/h
    ├── ultrasonic.c/h
    ├── audio_intercom.c/h
    ├── web_server.c/h
    └── web/index.html
```
