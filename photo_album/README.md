# ESP32-S3 Photo Album

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.x-blue)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![MCU](https://img.shields.io/badge/MCU-ESP32--S3-green)]()
[![Display](https://img.shields.io/badge/Display-800x480-orange)]()

基于 ESP32-S3 的本地图片轮播项目。程序从 SD 卡挂载 FAT 文件系统，递归扫描 JPEG/BMP 图片，并在 800x480 LCD 上自动播放。

当前代码同时支持两条显示路径：

- `Waveshare ESP32-S3-Touch-LCD-7`，RGB 接口
- `Generic NT35510 I80 Board`，16-bit I80 接口

当前工作区里的 [`sdkconfig`](./sdkconfig) 已配置为 `Generic NT35510 I80 Board`，并已同步到参考工程 `esp32-s3-nt35510-idf-example` 的 SD 引脚。

## 当前已实现功能

- 开机初始化流程：NVS、LCD、SD 卡、图片扫描、轮播任务
- SD 卡 SPI 挂载，挂载点固定为 `/sdcard`
- 递归扫描图片目录，最大深度 3 层，最多记录 2048 张图片
- 支持图片格式：
  - JPEG：基于 `esp_jpeg`，支持基线 JPEG
  - BMP：支持 24-bit / 32-bit、未压缩 BMP
- 双帧缓冲显示
- 过渡动画：
  - 直接切换
  - 淡入淡出
  - 左滑
  - 右滑
- 播放顺序：
  - 顺序
  - 倒序
  - 随机
- 背光亮度和轮播间隔支持从 NVS 读取
- 启动画面和错误画面

## 当前限制

- 没有运行时图形菜单；轮播参数目前通过代码默认值或 API 配置
- 没有接入触摸交互逻辑，虽然 Waveshare 板级定义里保留了 GT911 相关引脚
- 轮播信息条目前只做底部暗化，没有实际文字绘制
- 不支持 PNG
- 不支持渐进式 JPEG
- 代码会读取上次播放索引，但当前播放过程中没有持续写回该索引

## 硬件要求

- ESP32-S3
- 推荐 16MB Flash
- 推荐 8MB PSRAM 或更高
- 800x480 LCD
- FAT32 格式 SD 卡

## 板型与引脚

### 1. Waveshare ESP32-S3-Touch-LCD-7

默认配置来源于 [`sdkconfig.defaults`](./sdkconfig.defaults) 和 [`main/config.h`](./main/config.h)。

主要引脚：

- RGB LCD
  - `PCLK=GPIO7`
  - `VSYNC=GPIO3`
  - `HSYNC=GPIO46`
  - `DE=GPIO5`
  - `D0..D15 = 14,38,18,17,10,39,0,45,48,47,21,1,2,42,41,40`
- SD SPI
  - `MOSI=GPIO11`
  - `MISO=GPIO13`
  - `CLK=GPIO12`
  - `CS` 由 CH422G 扩展 IO 控制

### 2. Generic NT35510 I80 Board

板型配置项定义在 [`main/Kconfig.projbuild`](./main/Kconfig.projbuild)。

当前仓库 `sdkconfig` 中的有效引脚如下：

- LCD 控制
  - `WR=GPIO18`
  - `CS=GPIO17`
  - `DC=GPIO16`
  - `RST=GPIO20`
  - `BK=GPIO38`
  - `PWR=GPIO19`
- LCD 数据
  - `D0..D15 = GPIO0..GPIO15`
- SD SPI
  - `MOSI=GPIO41`
  - `MISO=GPIO39`
  - `CLK=GPIO40`
  - `CS=GPIO42`

对应默认模板见 [`sdkconfig.nt35510.defaults`](./sdkconfig.nt35510.defaults)。

## 项目结构

```text
esp32_photo_album/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── sdkconfig.nt35510.defaults
├── README.md
└── main/
    ├── CMakeLists.txt
    ├── Kconfig.projbuild
    ├── config.h
    ├── main.c
    ├── board/
    │   └── ch422g_ctrl.c/h
    ├── lcd/
    │   └── lcd_driver.c/h
    ├── sd/
    │   ├── sd_card.c/h
    │   └── photo_scanner.c/h
    ├── decoder/
    │   ├── jpeg_decoder.c
    │   ├── photo_jpeg_decoder.h
    │   └── bmp_decoder.c/h
    └── slideshow/
        └── slideshow.c/h
```

## 构建环境

- ESP-IDF `v5.x`
- 组件依赖：
  - `esp_jpeg`

`esp_jpeg` 已在 [`main/idf_component.yml`](./main/idf_component.yml) 中声明，首次构建时会由组件管理器处理。

## 使用方法

### 1. 准备 SD 卡

将图片拷贝到 SD 卡中，支持放在子目录下：

```text
/sdcard
├── photo1.jpg
├── photo2.bmp
└── album/
    ├── a.jpg
    └── b.jpeg
```

支持扩展名：

- `.jpg`
- `.jpeg`
- `.bmp`

### 2. 选择板型配置

如果使用 Waveshare RGB 板，默认 `sdkconfig.defaults` 已包含对应路径。

如果使用 NT35510 I80 板，有两种方式：

1. 直接使用当前仓库里的 [`sdkconfig`](./sdkconfig)
2. 或者从 NT35510 默认模板重新生成配置：

```bash
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.nt35510.defaults" reconfigure
```

之后也可以通过 `menuconfig` 调整：

```bash
idf.py menuconfig
```

路径：

- `Photo Album Hardware`

### 3. 编译

```bash
idf.py set-target esp32s3
idf.py build
```

### 4. 烧录

```bash
idf.py -p PORT flash monitor
```

示例：

```bash
idf.py -p COM5 flash monitor
```

## 启动流程

程序启动顺序与 [`main/main.c`](./main/main.c) 一致：

1. 初始化 NVS
2. 初始化 LCD
3. 挂载 SD 卡
4. 扫描图片文件
5. 初始化轮播管理器
6. 创建轮播任务并开始播放

## 轮播行为

当前默认行为：

- 轮播间隔：`5000 ms`
- 播放顺序：顺序播放
- 过渡动画：淡入淡出
- 亮度：`100`
- 图片显示信息：开启，但当前只显示底部暗色信息栏，不绘制文字

相关定义见：

- [`main/config.h`](./main/config.h)
- [`main/slideshow/slideshow.c`](./main/slideshow/slideshow.c)

## 存储分区

分区表见 [`partitions.csv`](./partitions.csv)：

- `factory` 应用分区：`0x300000`
- `storage` FAT 数据分区：`0xCF0000`

当前图片播放实际使用的是 SD 卡 `/sdcard`，不是内部 `storage` 分区。

## 常见问题

### 1. LCD 初始化失败，日志里看到 `pin is not configured`

说明当前生效的 `sdkconfig` 里板型或引脚没有配好。优先检查：

- 是否选中了正确的 board
- `sdkconfig` 是否被旧配置覆盖
- NT35510 的 `WR/CS/DC/RST/D0-D15`
- SD 的 `MOSI/MISO/CLK/CS`

### 2. SD 卡挂载失败

检查：

- SD 卡是否插好
- 是否为 FAT32
- SPI 引脚是否正确
- NT35510 板上当前推荐接法是否为 `41/39/40/42`

### 3. JPEG 解码失败

当前不支持渐进式 JPEG，建议转换为普通基线 JPEG。

### 4. 没扫到图片

检查：

- 文件扩展名是否为 `.jpg/.jpeg/.bmp`
- 文件是否确实位于 SD 卡中
- 路径层级是否过深

## 参考文件

- 板级配置入口：[main/config.h](./main/config.h)
- 板型菜单配置：[main/Kconfig.projbuild](./main/Kconfig.projbuild)
- LCD 驱动：[main/lcd/lcd_driver.c](./main/lcd/lcd_driver.c)
- SD 驱动：[main/sd/sd_card.c](./main/sd/sd_card.c)
- 图片扫描：[main/sd/photo_scanner.c](./main/sd/photo_scanner.c)
- 轮播逻辑：[main/slideshow/slideshow.c](./main/slideshow/slideshow.c)

## License

MIT
