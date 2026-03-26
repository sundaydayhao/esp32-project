# AI 智能垃圾分类系统
## AI Garbage Recognition & Sorting System

基于 **ESP32-S3** + **ESP-IDF** 框架实现的智能垃圾分类系统，通过摄像头识别彩色卡片，自动控制舵机将垃圾投入对应垃圾桶，并播放语音提示。

---

## 功能特性 / Features

- 📷 **摄像头识别**: OV2640 摄像头实时采集图像
- 🎨 **颜色卡片识别**: 基于 HSV 颜色空间分析，识别 4 种颜色卡片
- 🤖 **智能分拣**: 5 个舵机控制旋转平台和挡板，精准投放
- 🔊 **语音播报**: DFPlayer Mini 播放对应垃圾类型语音
- 💡 **LED 指示**: 5 个 LED 实时显示系统状态和垃圾类型
- 🧠 **可选 TFLite**: 支持 MobileNetV2 模型进行实物识别

## 垃圾分类 / Waste Categories

| 卡片颜色 | 垃圾类型 | 语音提示 |
|---------|---------|---------|
| 🔴 红色 | 有害垃圾 | "有害垃圾，请注意安全处理" |
| 🔵 蓝色 | 可回收物 | "可回收物，感谢您的环保行动" |
| 🟢 绿色 | 厨余垃圾 | "厨余垃圾，请妥善处理" |
| ⚫ 灰色 | 其他垃圾 | "其他垃圾" |

---

## 项目结构 / Project Structure

```
ai-garbage-recognition/
├── main/                          # ESP-IDF 主程序
│   ├── CMakeLists.txt             # 组件构建配置
│   ├── app_config.h               # 全局配置 (引脚/参数)
│   ├── main.cpp                   # 主入口 app_main()
│   ├── camera_handler.h/.cpp      # 摄像头模块 (OV2640)
│   ├── garbage_classifier.h/.cpp  # 垃圾分类模块 (HSV颜色)
│   ├── motor_control.h/.cpp       # 舵机控制模块 (LEDC PWM)
│   ├── audio_player.h/.cpp        # 音频播放模块 (DFPlayer Mini)
│   └── led_indicator.h/.cpp       # LED 指示灯模块
├── model/
│   └── train_model.py             # TFLite 模型训练脚本
├── docs/
│   └── hardware_wiring.md         # 硬件接线说明
├── CMakeLists.txt                 # 顶层 CMake 配置
├── sdkconfig.defaults             # ESP-IDF SDK 默认配置
├── partitions.csv                 # Flash 分区表
└── README.md                      # 本文件
```

---

## 硬件需求 / Hardware Requirements

| 元件 | 型号 | 数量 |
|------|------|------|
| 主控板 | ESP32-S3-DevKitC-1 (8MB Flash + PSRAM) | 1 |
| 摄像头 | OV2640 模块 | 1 |
| 舵机 | SG90 (5V) | 5 |
| MP3 模块 | DFPlayer Mini | 1 |
| 扬声器 | 3W 8Ω | 1 |
| Micro SD 卡 | ≥1GB | 1 |
| LED | 红/蓝/绿/黄/白 | 5 |
| 限流电阻 | 220Ω | 5 |
| 电源 | 5V/3A | 1 |

详细接线请参考: [docs/hardware_wiring.md](docs/hardware_wiring.md)

---

## 快速开始 / Quick Start

### 1. 环境准备

安装 ESP-IDF v5.x:
```bash
# Linux/macOS
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
source export.sh

# Windows (PowerShell)
# 下载 ESP-IDF Windows 安装器:
# https://dl.espressif.com/dl/esp-idf/
```

安装 esp32-camera 组件:
```bash
# 在项目根目录执行
idf.py add-dependency "espressif/esp32-camera^2.0.0"
```

### 2. 配置项目

编辑 `main/app_config.h` 根据您的硬件修改引脚配置:

```c
// 选择开发板类型
#define BOARD_ESP32S3_DEVKIT   // ESP32-S3-DevKitC-1 + 外接 OV2640
// #define BOARD_ESP32S3_EYE   // ESP32-S3-EYE 内置摄像头

// 舵机引脚 (根据实际接线修改)
#define SERVO_HAZARDOUS_GPIO    1
#define SERVO_RECYCLABLE_GPIO   2
// ...

// DFPlayer Mini UART 引脚
#define DFPLAYER_TX_GPIO        6
#define DFPLAYER_RX_GPIO        7
```

### 3. 准备 SD 卡音频文件

在 SD 卡根目录创建 `01` 文件夹，放入以下 MP3 文件:

```
SD卡/
└── 01/
    ├── 001.mp3   ← "系统就绪，请放置垃圾卡片"
    ├── 002.mp3   ← "有害垃圾，请注意安全处理"
    ├── 003.mp3   ← "可回收物，感谢您的环保行动"
    ├── 004.mp3   ← "厨余垃圾，请妥善处理"
    ├── 005.mp3   ← "其他垃圾"
    ├── 006.mp3   ← "无法识别，请重新放置卡片"
    └── 007.mp3   ← "系统错误，请检查设备"
```

> 可使用文字转语音工具生成 MP3 文件，推荐: [讯飞TTS](https://www.xfyun.cn/) 或 [百度TTS](https://ai.baidu.com/tech/speech/tts)

### 4. 编译与烧录

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 配置 (可选，使用 sdkconfig.defaults 默认配置)
idf.py menuconfig

# 编译
idf.py build

# 烧录 (替换 /dev/ttyUSB0 为实际串口)
idf.py -p /dev/ttyUSB0 flash

# 监视串口输出
idf.py -p /dev/ttyUSB0 monitor

# 一键编译+烧录+监视
idf.py -p /dev/ttyUSB0 flash monitor
```

### 5. 使用方法

1. 上电后系统自动初始化，LED 自检闪烁
2. 听到"系统就绪"语音提示后，系统进入待机状态
3. 将彩色卡片放置在摄像头前方 (距离约 15-30cm)
4. 系统自动检测并识别卡片颜色
5. 舵机旋转将垃圾导向对应垃圾桶
6. 播放对应垃圾类型语音
7. 系统复位，等待下一次识别

---

## 系统工作流程 / System Workflow

```
上电初始化
    │
    ▼
[空闲等待] ←──────────────────────────────┐
    │ 检测到运动                           │
    ▼                                      │
[捕获图像]                                 │
    │                                      │
    ▼                                      │
[颜色分析] ──置信度不足──► 播放"无法识别" ─┤
    │ 置信度 ≥ 70%                         │
    ▼                                      │
[舵机分拣] ──► 旋转平台 ──► 打开挡板       │
    │                                      │
    ▼                                      │
[语音播报] ──► 播放垃圾类型语音            │
    │                                      │
    ▼                                      │
[系统复位] ──► 关闭挡板 ──► 平台归位 ─────┘
```

---

## 颜色识别原理 / Color Recognition

系统使用 **HSV 颜色空间**分析图像中心 ROI 区域的颜色分布:

```
RGB → HSV 转换
    H (色相):    0-360°  区分颜色种类
    S (饱和度):  0-1     区分彩色/灰色
    V (明度):    0-1     过滤过暗像素

颜色判断规则:
    红色: H ∈ [0°,15°] 或 [345°,360°], S > 0.4  → 有害垃圾
    蓝色: H ∈ [195°,265°], S > 0.3               → 可回收物
    绿色: H ∈ [75°,165°], S > 0.3                → 厨余垃圾
    灰色: S < 0.2                                 → 其他垃圾
```

---

## TFLite 模型 (可选) / TFLite Model (Optional)

如需识别实物垃圾 (而非颜色卡片)，可训练 TFLite 模型:

### 准备数据集

```
dataset/
├── train/
│   ├── hazardous/    (有害垃圾图片, ≥200张)
│   ├── recyclable/   (可回收物图片, ≥200张)
│   ├── kitchen/      (厨余垃圾图片, ≥200张)
│   └── other/        (其他垃圾图片, ≥200张)
└── val/
    ├── hazardous/    (验证集, ≥50张)
    ├── recyclable/
    ├── kitchen/
    └── other/
```

### 训练模型

```bash
# 安装依赖
pip install tensorflow pillow numpy matplotlib scikit-learn seaborn

# 训练
python model/train_model.py --data_dir ./dataset --epochs 30

# 输出文件:
#   output/garbage_classifier.tflite  (TFLite 模型)
#   output/model_data.h               (C 头文件)
#   output/training_history.png       (训练曲线)
#   output/confusion_matrix.png       (混淆矩阵)
```

### 启用 TFLite 模型

```cpp
// 在 main/garbage_classifier.cpp 中修改:
#define USE_TFLITE_MODEL  1  // 改为 1

// 将 output/model_data.h 复制到 main/ 目录
// 重新编译烧录
```

---

## 串口调试输出 / Serial Debug Output

系统运行时会通过串口 (115200 baud) 输出详细日志:

```
╔══════════════════════════════════════╗
║   AI 智能垃圾分类系统  v1.0.0       ║
║   AI Garbage Sorting System          ║
║   ESP32-S3 + ESP-IDF                 ║
╠══════════════════════════════════════╣
║  卡片颜色对应:                       ║
║  红色 -> 有害垃圾                    ║
║  蓝色 -> 可回收物                    ║
║  绿色 -> 厨余垃圾                    ║
║  灰色 -> 其他垃圾                    ║
╚══════════════════════════════════════╝

[1/5] 初始化 LED 指示灯...
  LED 初始化成功
[2/5] 初始化音频播放器 (DFPlayer Mini)...
  DFPlayer Mini 初始化完成，音量: 20
[3/5] 初始化舵机电机...
  舵机初始化完成 (5个)
[4/5] 初始化摄像头 (OV2640)...
  摄像头初始化成功 (320x240)
[5/5] 初始化垃圾分类器...
  分类器初始化 (颜色卡片识别模式)

系统就绪！请将垃圾卡片放置在摄像头前

>>> 检测到物体，开始识别...
ROI 平均颜色: R=210 G=30 B=25
HSV: H=2.1 S=0.88 V=0.82
颜色统计: 红=3240 蓝=12 绿=45 灰=203 总=3500

┌─────────────────────────────────────┐
│  识别结果: 有害垃圾                  │
│  置信度:   92%                      │
│  颜色:     R=210 G=30  B=25         │
│  HSV:      H=2.1  S=0.88 V=0.82    │
└─────────────────────────────────────┘

分拣: 投入 有害垃圾桶 (平台角度: 0°)
打开 有害垃圾桶 挡板
关闭 有害垃圾桶 挡板
复位完成，等待下一次检测
```

---

## 常见问题 / FAQ

**Q: 摄像头初始化失败怎么办?**
A: 检查摄像头接线，确认 XCLK/SIOD/SIOC 引脚正确。确保 3.3V 供电稳定。

**Q: 颜色识别不准确?**
A: 确保光线充足均匀，避免强烈背光。调整 `main/garbage_classifier.cpp` 中的 HSV 范围参数。

**Q: 舵机不动作?**
A: 检查舵机 5V 供电是否独立，确认 GPIO 引脚配置正确。

**Q: DFPlayer Mini 无声音?**
A: 检查 SD 卡文件目录结构 (`/01/001.mp3`)，确认 UART 引脚和波特率 (9600)。

**Q: 如何调整置信度阈值?**
A: 修改 `main/app_config.h` 中的 `CONFIDENCE_THRESHOLD` (默认 70%)。

---

## 许可证 / License

MIT License - 自由使用、修改和分发

---

## 参考资料 / References

- [ESP-IDF 编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/)
- [esp32-camera 组件](https://github.com/espressif/esp32-camera)
- [DFPlayer Mini 数据手册](https://wiki.dfrobot.com/DFPlayer_Mini_SKU_DFR0299)
- [OV2640 数据手册](https://www.ov.com/ov2640.html)