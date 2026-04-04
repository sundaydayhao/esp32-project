# ESP32 / ESP32-S3 + LCD12864 时钟、天气、室内传感器与整点报时

基于 `ESP32` / `ESP32-S3`、`LCD12864(ST7920 8 位并口)`、`DHT11` 和 `MAX98357A` 的信息显示项目。设备联网后会同步北京时间，拉取和风天气今日预报，在 LCD12864 上轮播显示室外与室内信息，并可在 `08:00` 到 `20:00` 执行整点语音报时。

当前源码已验证可用于以下模组：

- `ESP-WROOM-32D`
- `ESP32-S3-WROOM-1`

说明：本项目是同一套源码兼容两个目标芯片，编译前仍需先执行对应的 `idf.py set-target`。

## 功能清单

- NTP 自动同步北京时间，屏幕按秒刷新时间与日期
- Wi-Fi STA 自动连接，断线后自动重连
- 通过和风天气 API 获取今日天气预报
- 显示今日天气文本、最低/最高温、室外湿度
- 通过 `DHT11` 采集室内温度和湿度
- 轮播两组页面：
  - 页面 1：时间、日期、室外天气、室内温湿度
  - 页面 2：室内外温湿度对比结果
- 通过 `MAX98357A` + 喇叭播放整点语音报时
- 默认在 `08:00` 到 `20:00` 执行整点语音报时
- 所有 GPIO、Wi-Fi、天气、NTP、音频、DHT11、页面轮播周期都可在 `menuconfig` 中修改

## 屏幕显示说明

LCD12864 每隔 `Display screen switch interval` 秒切换一次页面，默认 5 秒。

### 页面 1

```text
12:34:56 SUNNY
2026-03-30 MON
OUT 24-30C 68%
IN  26C 61%
```

- 第 1 行：当前时间 + 天气文本
- 第 2 行：日期 + 星期
- 第 3 行：室外今日最低/最高温 + 湿度
- 第 4 行：室内温度 + 湿度

### 页面 2

```text
12:34:56 SUNNY
2026-03-30 MON
TEMP IN RANGE
HUM +8% WET
```

- 第 3 行：室内温度与室外今日温度范围比较
- 第 4 行：室内湿度与室外湿度比较

天气中文会被映射成 LCD 上更稳定的英文简写，例如 `SUNNY`、`RAIN`、`CLOUDY`。

## 硬件连接

### 必要硬件

| 器件 | 说明 |
|------|------|
| ESP32 / ESP32-S3 开发板 | 需有足够 GPIO |
| LCD12864 | `ST7920` 控制器，8 位并口模式 |
| DHT11 | 室内温湿度传感器 |
| MAX98357A | I2S 功放模块 |
| 4Ω/8Ω 喇叭 | 用于语音播报 |
| 连接线 | 若干 |

### LCD12864 默认引脚

| ESP32 GPIO | LCD12864 引脚 | 说明 |
|-----------|---------------|------|
| GPIO 32 | D0 | 数据 bit0 |
| GPIO 13 | D1 | 数据 bit1 |
| GPIO 14 | D2 | 数据 bit2 |
| GPIO 15 | D3 | 数据 bit3 |
| GPIO 16 | D4 | 数据 bit4 |
| GPIO 17 | D5 | 数据 bit5 |
| GPIO 18 | D6 | 数据 bit6 |
| GPIO 19 | D7 | 数据 bit7 |
| GPIO 21 | RS | 指令/数据选择 |
| GPIO 22 | E | 使能 |
| GPIO 4  | RST | 复位 |
| GND | RW | 必须接地，固定为写模式 |
| 5V  | PSB | 必须拉高，选择并口模式 |
| 5V  | VCC | 供电 |
| GND | GND | 地 |

### DHT11 默认引脚

| ESP32 GPIO | DHT11 引脚 | 说明 |
|-----------|------------|------|
| GPIO 23 | DATA | 单总线数据 |
| 3.3V / 5V | VCC | 供电 |
| GND | GND | 地 |

说明：`DATA` 建议外接上拉电阻。

### MAX98357A 默认引脚

| ESP32 GPIO | MAX98357A 引脚 | 说明 |
|-----------|----------------|------|
| GPIO 25 | BCLK | I2S Bit Clock |
| GPIO 26 | LRC / WS | I2S Word Select |
| GPIO 27 | DIN | I2S 数据输出 |
| 5V / VIN | VIN | 供电 |
| GND | GND | 地 |

喇叭接到 `MAX98357A` 的扬声器输出端。

### 接线注意事项

1. `LCD12864` 的 `RW` 必须接 `GND`，避免 LCD 反向输出 5V 到 ESP32。
2. `LCD12864` 的 `PSB` 必须接高电平，项目只支持 8 位并口模式。
3. `LCD12864` 模块通常使用 5V 供电，接线前先确认你的模块电源规格。
4. 如果更改了引脚，请同步在 `menuconfig` 中修改。

## 配置说明

运行：

```bash
idf.py menuconfig
```

进入 `LCD12864 Clock Configuration` 后，至少检查以下几组参数。

### Wi-Fi

- `WiFi SSID`
- `WiFi Password`

### Weather API

- `QWeather API Key`
- `Location ID`
- `Weather update interval (minutes)`

项目当前使用和风天气 `3d` 接口中的第 1 天数据作为“今日天气”显示。

### NTP

- `NTP re-sync interval (minutes)`

默认使用以下 NTP 服务器：

- `ntp.aliyun.com`
- `cn.pool.ntp.org`
- `pool.ntp.org`

### Audio Announcement

- `Audio I2S BCLK GPIO`
- `Audio I2S WS/LRCK GPIO`
- `Audio I2S DOUT GPIO`
- `Hourly announce start hour`
- `Hourly announce end hour`

默认在 `08:00` 到 `20:00` 执行整点报时，避免夜间播报。

### Indoor Sensor

- `DHT11 data GPIO`
- `DHT11 update interval (seconds)`

### Display

- `Screen switch interval (seconds)`

## 获取和风天气配置

1. 访问 [https://dev.qweather.com](https://dev.qweather.com) 注册账号。
2. 创建应用并获取 `API Key`。
3. 在 `menuconfig` 中填入 `QWeather API Key`。
4. 根据城市设置 `Location ID`。

常用城市示例：

| 城市 | Location ID |
|------|-------------|
| 北京 | 101010100 |
| 上海 | 101020100 |
| 广州 | 101280101 |
| 深圳 | 101280601 |
| 杭州 | 101210101 |
| 成都 | 101270101 |

更多城市可参考 [qwd/LocationList](https://github.com/qwd/LocationList)。

## 编译与烧录

```bash
cd lcd12864

# 二选一
idf.py set-target esp32
# 或
idf.py set-target esp32s3

idf.py menuconfig
idf.py build
idf.py -p COM3 flash monitor
```

Windows 下把 `COM3` 改成实际串口；Linux/macOS 可使用 `/dev/ttyUSB0`、`/dev/ttyACM0` 等。

## 操作说明

### 首次使用

1. 按上面的接线表连接 `LCD12864`、`DHT11`、`MAX98357A` 和喇叭。
2. 使用 `idf.py menuconfig` 填入 Wi-Fi、天气 API 和城市 ID。
3. 编译并烧录固件。
4. 打开串口监视器观察启动日志，确认 LCD、Wi-Fi、NTP、天气、DHT11、音频初始化是否成功。

### 上电后的工作流程

1. 屏幕先显示启动信息。
2. 设备连接 Wi-Fi。
3. NTP 同步北京时间。
4. 首次拉取天气数据。
5. DHT11 开始周期采样室内温湿度。
6. 显示任务开始按设定周期轮播两页内容。
7. 到整点且处于 `08:00` 到 `20:00` 时，设备播放“北京时间 X 点整”。

### 日常使用行为

- 时间每秒刷新一次。
- 天气按 `Weather update interval` 周期更新。
- DHT11 按 `DHT11 update interval` 周期更新。
- 到整点且处于 `08:00` 到 `20:00` 时，播放语音报时。
- Wi-Fi 断开后会自动尝试重连；重连成功后天气和 NTP 会继续更新。

### 日志中可关注的关键信息

- `WiFi connected`
- `NTP time synchronized`
- `Forecast: ...`
- `Indoor: ...`
- `I2S ready: ...`

## 项目结构

```text
lcd12864/
|-- CMakeLists.txt
|-- README.md
|-- sdkconfig.defaults
`-- main/
    |-- main.c
    |-- lcd12864.c
    |-- lcd12864_gfx.c
    |-- wifi_manager.c
    |-- ntp_client.c
    |-- weather_client.c
    |-- dht11.c
    |-- audio_output.c
    |-- hourly_chime.c
    |-- certs/isrg_root_x1.pem
    `-- audio/tts/*.wav
```

## 常见问题

### LCD 无显示

- 检查 `PSB` 是否接高电平
- 检查 `RW` 是否接地
- 检查模块供电是否正确
- 检查 `D0~D7`、`RS`、`E`、`RST` 接线
- 检查 `menuconfig` 中 GPIO 是否和实际接线一致

### 时间不更新

- 检查 Wi-Fi 是否成功联网
- 检查日志中是否出现 `NTP time synchronized`
- 检查网络环境是否允许访问 NTP 服务器

### 没有天气数据

- 检查 `API Key` 和 `Location ID`
- 检查设备是否联网
- 检查日志中的 HTTP 状态码和错误信息
- 注意首次天气请求会等待 NTP 完成后再执行 HTTPS 请求

### 没有语音

- 检查 `MAX98357A` 供电与 `BCLK/WS/DIN` 接线
- 检查喇叭是否接到功放输出端
- 检查整点是否落在 `Hourly announce start hour` 到 `Hourly announce end hour` 范围内
- 检查日志中是否出现 `I2S ready`

## 依赖

- `ESP-IDF v5.x`
- `esp_wifi`
- `esp_http_client`
- `esp_sntp`
- `nvs_flash`
- `cJSON`
- `esp_driver_i2s`

## License

MIT
