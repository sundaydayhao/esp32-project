# ESP32-S3 Signal Generator

基于 ESP-IDF 的 ESP32-S3 信号发生器，通过 LEDC PWM 输出正弦波、方波、三角波和锯齿波。

## 功能特性

| 特性 | 说明 |
|------|------|
| 波形类型 | 正弦波、方波、三角波、锯齿波 |
| 频率范围 | 1 Hz – 20 kHz |
| 幅度控制 | 0% – 100% |
| DC 偏置 | 0.0 – 1.0（0.5 = 中点，无偏置）|
| 输出引脚 | GPIO 1（可在 `signal_generator.h` 中修改）|
| PWM 载波 | 80 kHz，10-bit 分辨率（1024 级）|
| 采样率 | 40 kHz |
| 控制方式 | UART 串口控制台（115200 baud）|

## 硬件连接

```
ESP32-S3 GPIO 1
      │
      ├──[1kΩ]──┬── 输出（模拟信号）
                │
               [100nF]
                │
               GND
```

> **RC 低通滤波器**将 PWM 信号转换为模拟电压。
> - R = 1 kΩ，C = 100 nF → 截止频率 ≈ 1.6 kHz（适合低频信号）
> - R = 1 kΩ，C = 10 nF  → 截止频率 ≈ 16 kHz（适合高频信号）

## 项目结构

```
esp32-signal-generator/
├── CMakeLists.txt          # 根 CMake 构建文件
├── sdkconfig.defaults      # SDK 默认配置
├── README.md               # 本文档
└── main/
    ├── CMakeLists.txt      # 组件 CMake 文件
    ├── main.c              # 入口点 & 串口控制台
    ├── signal_generator.h  # 信号发生器头文件
    └── signal_generator.c  # 信号发生器实现
```

## 编译与烧录

### 前提条件

- ESP-IDF v5.x（推荐 v5.1 或更高版本）
- 目标芯片：ESP32-S3

### 步骤

```bash
# 1. 设置 ESP-IDF 环境
. $IDF_PATH/export.sh          # Linux/macOS
# 或
%IDF_PATH%\export.bat          # Windows CMD
# 或
. $IDF_PATH/export.ps1         # Windows PowerShell

# 2. 设置目标芯片
idf.py set-target esp32s3

# 3. 编译
idf.py build

# 4. 烧录（替换 PORT 为实际串口）
idf.py -p PORT flash

# 5. 监视串口输出
idf.py -p PORT monitor

# 或一步完成烧录和监视
idf.py -p PORT flash monitor
```

## 串口控制台命令

连接串口终端（115200 baud），使用以下命令控制信号发生器：

| 命令 | 参数 | 说明 | 示例 |
|------|------|------|------|
| `start` | — | 启动波形输出 | `start` |
| `stop` | — | 停止波形输出 | `stop` |
| `wave` | `<类型>` | 设置波形类型 | `wave sine` |
| `freq` | `<Hz>` | 设置频率 | `freq 1000` |
| `amp` | `<0.0-1.0>` | 设置幅度 | `amp 0.8` |
| `offset` | `<0.0-1.0>` | 设置 DC 偏置 | `offset 0.5` |
| `status` | — | 显示当前配置 | `status` |
| `help` | — | 显示帮助信息 | `help` |

### 波形类型参数

| 参数 | 波形 |
|------|------|
| `sine` 或 `sin` | 正弦波 |
| `square` 或 `sq` | 方波 |
| `triangle` 或 `tri` | 三角波 |
| `sawtooth` 或 `saw` | 锯齿波 |

### 使用示例

```
siggen> wave sine
Waveform set to: Sine

siggen> freq 440
Frequency set to: 440.00 Hz

siggen> amp 0.9
Amplitude set to: 0.90 (90%)

siggen> start
Output started.

siggen> status

┌─────────────────────────────────────┐
│       Signal Generator Status        │
├─────────────────────────────────────┤
│  State     : RUNNING                 │
│  Waveform  : Sine                    │
│  Frequency : 440.00 Hz               │
│  Amplitude : 0.90                    │
│  DC Offset : 0.50                    │
│  Output    : GPIO 1                  │
│  PWM Freq  : 80000 Hz                │
│  Sample Rt : 40000 Hz                │
└─────────────────────────────────────┘

siggen> wave square
siggen> freq 1000
siggen> stop
```

## 技术原理

### 相位累加器

信号发生器使用 32-bit 相位累加器生成波形：

```
phase_acc += phase_inc   (每个采样点)
phase_inc = (freq / sample_rate) × 2³²
```

相位累加器自然溢出（无符号整数），实现精确的频率控制。

### 波形生成

| 波形 | 算法 |
|------|------|
| 正弦波 | 1024 点查找表（预计算 sin 值）|
| 方波 | phase_acc < 2³¹ → +1，否则 → -1 |
| 三角波 | 前半周期线性上升，后半周期线性下降 |
| 锯齿波 | 全周期线性上升 |

### PWM 输出

```
duty = (sample × amplitude × 0.5 + offset) × 1023
```

LEDC 以 80 kHz 载波频率输出 PWM，经 RC 低通滤波器还原为模拟信号。

## 修改输出引脚

在 `main/signal_generator.h` 中修改：

```c
#define SIGGEN_OUTPUT_GPIO  1   // 改为所需的 GPIO 编号
```

## 许可证

MIT License