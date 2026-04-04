# ESP32-S3 Desk Clock

## 自动配网逻辑

当前项目已经支持和 `xiaozhi-spi-lcd` 类似的“自动进入配网 / 长按重新配网”流程，逻辑如下：

1. 设备启动后优先读取 `NVS` 命名空间 `wifi_cfg` 中保存的 `ssid` / `pass`。
2. 如果 `NVS` 中没有保存过 Wi‑Fi，则再尝试使用 `main/config.h` 里的 `CONFIG_WIFI_SSID_DEFAULT` / `CONFIG_WIFI_PASS_DEFAULT`。
3. 只要存在可用凭据，设备就会自动以 `STA` 模式联网。
4. 如果完全没有凭据，或者联网超时 / 重试失败，设备会自动切换到热点配网模式。
5. 热点配网模式下，设备会开启一个临时热点，SSID 形如 `DeskClock-XXXX`，配网页地址固定为 `http://192.168.4.1/`。
6. 在配网页提交新的 2.4G Wi‑Fi 名称和密码后，设备会把凭据写入 `NVS`，退出热点模式，并自动重新连接路由器。
7. 联网成功后，首页 `/` 会恢复为环境看板页面，手机和电脑接入同一局域网后，直接访问设备的局域网 IP 即可。

## 按键逻辑

- `GPIO21` 短按：播报当前北京时间，并继续播报天气信息。
- `GPIO21` 长按 `5` 秒：清空已保存的 Wi‑Fi 凭据，并立即重新进入热点配网模式。

## 配网说明

- 默认情况下，`main/config.h` 中的 `CONFIG_WIFI_SSID_DEFAULT` 和 `CONFIG_WIFI_PASS_DEFAULT` 已调整为空字符串，首次上电会直接进入自动配网流程。
- 如果你希望固件出厂后优先连接固定路由器，也可以重新在 `main/config.h` 中填入默认 Wi‑Fi，设备会在 `NVS` 没有保存值时先尝试连接这些默认凭据。
- 设备热点仅用于临时配网；配网成功后会自动关闭热点并回到正常联网模式。
- 重新配网不需要擦除整片 `NVS`，长按按键 5 秒即可。

本项目面向基于 `ESP32-S3 + NT35510` 的通用并口屏时钟板，当前使用 `I80` 总线驱动 `800x480` LCD，并在屏幕上显示时间、联网状态和天气信息。

## 当前支持的功能

- `LVGL` 桌面时钟界面，包含模拟时钟、数字时钟、日期和星期显示
- `Wi-Fi STA + SoftAP` 自动配网，开机先尝试已保存凭据，失败后自动切换到热点配网页
- 长按 `GPIO21` `5` 秒清空 Wi‑Fi 凭据并重新配网
- `NTP` 对时，时区固定为 `CST-8`
- 和风天气 `QWeather v7` 接入
- 实时天气信息显示
- 当日高低温显示
- 湿度、体感温度、风速、能见度显示
- 未来 `3` 天预报显示
- 整点语音报时，默认在 `08:00` 到 `20:00` 之间触发
- 独立按键触发“北京时间 xx 点 xx 分”语音播报
- 开机音频测试音
- 运行日志镜像到 `SD` 卡文件，便于离线排障
- `NT35510` 的 `8-bit` / `16-bit I80` 总线支持
- `LCD diagnostic` 诊断模式
- `UI only` 模式和 `full app` 模式

## 当前限制

- 电池图标目前是界面占位，显示固定为 `100%`，没有真实电量采集
- 亮度虽然支持从 `NVS` 读取，但当前没有界面内调节入口
- 天气当前显示的是天气状态、当日温区间和详情卡片，`temp_now` 已获取但没有单独显示在界面上
- 目前没有板载设置界面，`Wi-Fi`、天气城市、API Key 等仍以代码配置为主
- 按键功能目前只实现了“单击播报当前时间”，还没有天气播报等扩展操作

## 硬件说明

### 显示屏

显示屏参数和引脚在 `menuconfig` 的 `Desk Clock Hardware` 菜单中配置，支持：

- 分辨率
- `I80` 总线宽度
- 像素时钟
- 颜色顺序
- `Swap X/Y`
- `Mirror X/Y`
- `RGB565` 字节交换
- `WR / CS / DC / RST / BK / POWER / D0..D15` 引脚

仓库内的 `sdkconfig.defaults` 已带一套示例 `NT35510` 引脚映射，正式上板前请按你的硬件修改。

### 音频

默认音频输出面向 `MAX98357A`：

- `BCLK`: `GPIO47`
- `WS/LRCK`: `GPIO48`
- `DIN`: `GPIO46`

### SD 日志

运行日志支持通过 `SPI` 模式写入 `SD` 卡，相关配置位于 `menuconfig -> Desk Clock Storage`：

- `DESK_CLOCK_SD_LOG_PIN_MISO`
- `DESK_CLOCK_SD_LOG_PIN_MOSI`
- `DESK_CLOCK_SD_LOG_PIN_SCLK`
- `DESK_CLOCK_SD_LOG_PIN_CS`
- `DESK_CLOCK_SD_LOG_FREQ_MHZ`

日志文件默认路径：

- `/sdcard/logs/runtime.log`

### 按键

时间播报按键默认配置：

- `GPIO21`
- 低电平触发

## 代码配置位置

以下运行参数当前主要在 [main/config.h](D:/workspace/esp32-project/computer_motherboard_display/rgb-lcd-nt35510/main/config.h) 中修改：

- 默认 Wi-Fi：`CONFIG_WIFI_SSID_DEFAULT`、`CONFIG_WIFI_PASS_DEFAULT`
- 天气配置：`WEATHER_API_KEY`、`WEATHER_LOCATION_ID`、`WEATHER_API_HOST`
- 整点报时时段：`AUDIO_ANNOUNCE_START_HOUR`、`AUDIO_ANNOUNCE_END_HOUR`
- 音频引脚：`AUDIO_I2S_BCLK_PIN`、`AUDIO_I2S_WS_PIN`、`AUDIO_I2S_DOUT_PIN`
- 按键引脚：`BUTTON_TIME_ANNOUNCE_PIN`

说明：

- `Wi-Fi SSID/密码` 优先从 `NVS` 命名空间 `wifi_cfg` 中读取
- 若 `NVS` 中没有保存值，则回退到 `main/config.h` 里的默认值
- 天气 `API Key` 和城市 `Location ID` 会优先尝试从 `desk_clock` 命名空间读取；如果没有外部写入，则使用编译时默认值

## 构建与烧录

设置目标芯片：

```powershell
idf.py set-target esp32s3
```

按需配置屏幕参数：

```powershell
idf.py menuconfig
```

编译：

```powershell
idf.py build
```

烧录并查看日志：

```powershell
idf.py -p COMx flash monitor
```

## 操作说明

### 1. 首次上电

- 上电后会先初始化 LCD
- 音频初始化成功时，会播放一小段测试音
- 默认启动模式为 `full app`

### 2. 联网与对时

- 设备启动后会自动连接已配置的 `Wi-Fi`
- `Wi-Fi` 连上后启动 `NTP` 对时
- 天气请求依赖正确系统时间，未完成对时前不会发起首个 `HTTPS` 天气请求
- 首次天气更新可能会比开机晚几十秒，这是正常现象

### 3. 屏幕显示

左侧区域显示：

- 模拟时钟
- 当前数字时间
- 日期和星期
- `NTP` 是否已同步

右侧区域显示：

- 当前天气状态
- 当日高低温
- 湿度
- 体感温度
- 风速
- 能见度
- 未来 `3` 天预报

顶部状态栏显示：

- `Wi-Fi` 信号条
- 顶部时间标题
- 电池图标占位

### 4. 按键操作

- 按下 `GPIO21` 对应按键，会播报当前北京时间
- 如果 `NTP` 还没同步成功，按键播报会被忽略

### 5. 整点报时

- 默认在每天 `08:00` 到 `20:00` 的整点触发
- 例如 `09:00` 会播报“北京时间九点整”
- 非整点不会自动播报

### 6. 天气刷新

- 启动后会尝试拉取一次天气
- 之后按 `WEATHER_UPDATE_INTERVAL_S` 的配置定时刷新，当前默认 `3600s`

## 启动模式说明

启动模式由 [main/config.h](D:/workspace/esp32-project/computer_motherboard_display/rgb-lcd-nt35510/main/config.h) 中的 `SYSTEM_BOOT_MODE` 控制：

- `BOOT_MODE_FULL_APP`
  - 默认模式
  - 启动 `Wi-Fi`、`NTP`、天气、整点报时、按键播报和完整 UI
- `BOOT_MODE_UI_ONLY`
  - 只初始化 UI，不启动联网任务
- `BOOT_MODE_LCD_DIAGNOSTIC`
  - 循环切换测试图案，用于排查 LCD 接线和颜色异常

## 建议排查顺序

如果上电后界面或功能异常，建议按下面顺序检查：

1. 先用 `BOOT_MODE_LCD_DIAGNOSTIC` 确认屏幕引脚、颜色顺序和镜像配置正确
2. 再切回 `BOOT_MODE_FULL_APP` 检查 `Wi-Fi` 是否连上
3. 确认 `NTP` 已同步后，再看天气是否更新
4. 最后检查按键和 `MAX98357A` 音频接线
