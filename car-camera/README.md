# ESP32-S3 智能摄像头小车

这是一个基于 ESP-IDF 的 ESP32-S3 摄像头小车工程。设备启动后会创建 Wi-Fi 热点，手机或电脑连接后打开 `http://192.168.4.1`，即可看到摄像头画面并控制小车前进、后退、转向、原地旋转和停止。

## 功能

- ESP32-S3 SoftAP 热点：默认 `ESP32S3-Car` / `12345678`
- OV2640 摄像头 MJPEG 实时视频流
- 双直流电机 PWM 调速，默认适配 TB6612FNG，也可接 L298N
- 网页遥控，支持触摸、鼠标和键盘
- 可选 HC-SR04 超声波避障，前方过近时自动停止前进
- 电机失控保护：超过一段时间未收到控制命令会自动停车
- 所有主要 GPIO 可在 `idf.py menuconfig` 中修改

## 快速烧录

```bash
cd car
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py menuconfig
idf.py build flash monitor
```

启动后连接热点 `ESP32S3-Car`，密码 `12345678`，浏览器打开：

```text
http://192.168.4.1
```

## 硬件清单

| 模块 | 建议型号 | 说明 |
| --- | --- | --- |
| 主控 | ESP32-S3 DevKit / ESP32-S3-WROOM，带 PSRAM | 摄像头建议使用带 PSRAM 的模块 |
| 摄像头 | OV2640 DVP/FPC 模块 | 3.3V 供电 |
| 电机驱动 | TB6612FNG | 推荐，逻辑 3.3V 友好 |
| 电机 | TT 减速电机 x2 | 左右轮差速 |
| 电源 | 2S 锂电或 6-12V 电池 + 5V 降压模块 | 电机和 ESP32 不要共用薄弱 USB 供电 |
| 避障 | HC-SR04，可选 | Echo 必须降压到 3.3V |

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
 +----+-----+     +----+----------------+
      |                |
  Left/Right       +---v----+
  DC Motors        | OV2640 |
                   +--------+

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

## 重要注意事项

- 电机不要从 ESP32-S3 的 3.3V 引脚取电。
- 电机电源、降压模块和 ESP32-S3 必须共地。
- 建议在电机驱动电源端并联 470uF 以上电解电容，减少电机启动导致的掉电复位。
- HC-SR04 的 Echo 是 5V 电平，直接接 ESP32-S3 可能损坏芯片。
- 如果你的 ESP32-S3 摄像头板针脚不同，进入 `idf.py menuconfig`，修改 `Smart Camera Car Configuration` 下的摄像头和电机 GPIO。

## 工程结构

```text
car/
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
    ├── web_server.c/h
    └── web/index.html
```
