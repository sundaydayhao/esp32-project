# ESP32 AI/ML 开发指南

> 在资源受限的微控制器上运行机器学习模型

## 目录

- [ESP32 AI 能力概述](#esp32-ai-能力概述)
- [开发框架选择](#开发框架选择)
- [示例说明](#示例说明)
- [TFLite Micro 快速入门](#tflite-micro-快速入门)
- [模型训练与部署流程](#模型训练与部署流程)
- [性能优化](#性能优化)
- [常见应用场景](#常见应用场景)

---

## ESP32 AI 能力概述

### 硬件规格对比

| 芯片 | CPU | SRAM | Flash | AI 特性 | 适合模型大小 |
|------|-----|------|-------|---------|------------|
| ESP32 | 240MHz 双核 Xtensa LX6 | 520KB | 4MB+ | 无专用加速 | <50KB |
| ESP32-S3 | 240MHz 双核 Xtensa LX7 | 512KB | 8MB+ | 向量指令（AI加速） | <500KB |
| ESP32-P4 | 400MHz 双核 RISC-V | 768KB | 16MB+ | 高性能 AI | <2MB |

### ESP32 推理性能参考

```
模型规模    参数量    推理时间    适用场景
─────────────────────────────────────────────
超小型      <1K      <10μs      实时控制、简单分类
小型        1K~10K   10~100μs   传感器分类、关键词检测
中型        10K~100K 0.1~10ms   图像分类（低分辨率）
大型        >100K    >10ms      不推荐（内存不足）
```

---

## 开发框架选择

### 1. TensorFlow Lite for Microcontrollers (TFLite Micro)

**最推荐**，官方支持，生态完善。

```bash
# 安装组件
idf.py add-dependency "esp-tflite-micro>=1.0.0"
```

**特点：**
- 支持量化（INT8）推理，减少内存占用
- 支持 ESP32/ESP32-S3/ESP32-C3
- 丰富的算子支持（Conv2D、LSTM、Dense 等）
- 官方示例：hello_world、person_detection、magic_wand

### 2. ESP-DL（乐鑫深度学习库）

**ESP32-S3 专用**，针对向量指令优化。

```bash
idf.py add-dependency "esp-dl>=1.0.0"
```

**特点：**
- 专为 ESP32-S3 优化，性能最佳
- 支持人脸检测、人脸识别、手势识别
- 与 ESP-WHO 摄像头框架集成

### 3. 纯 C 实现（本项目 03-neural-network）

**无依赖**，适合简单模型。

**特点：**
- 零依赖，代码完全可控
- 适合小型 MLP（<10K 参数）
- 权重存储在 Flash，推理缓冲区在 SRAM

### 4. MicroML / EloquentML

**Arduino 生态**，适合快速原型。

---

## 示例说明

### 01-tflite-hello-world

**目标：** 了解 TFLite Micro 工作流程

**内容：**
- 神经网络推理基本概念
- 软件模拟 2 层 MLP 推理正弦波
- 推理性能基准测试
- ASCII 波形可视化

**关键概念：**
```
输入 x → [归一化] → [Dense(8,ReLU)] → [Dense(1,Linear)] → 预测 sin(x)
```

### 02-anomaly-detection

**目标：** 传感器数据实时异常检测

**内容：**
- Z-Score 滑动窗口异常检测
- 统计特征提取（均值、方差、IQR）
- 移动平均滤波
- 多种异常类型测试（尖峰、漂移、噪声）

**算法：**
```
Z-Score = |x - μ| / σ
若 Z-Score > 阈值(3.0) → 异常
```

**应用场景：**
- 工业设备预测性维护
- 传感器故障检测
- 环境监测告警

### 03-neural-network

**目标：** 理解神经网络推理原理

**内容：**
- 纯 C 实现的 MLP 推理引擎
- XOR 分类器演示
- 鸢尾花多分类演示
- 不同规模网络性能基准

**支持的激活函数：**

| 函数 | 公式 | 用途 |
|------|------|------|
| ReLU | max(0, x) | 隐藏层（最常用） |
| Sigmoid | 1/(1+e⁻ˣ) | 二分类输出层 |
| Tanh | (eˣ-e⁻ˣ)/(eˣ+e⁻ˣ) | 隐藏层（RNN） |
| Softmax | eˣⁱ/Σeˣʲ | 多分类输出层 |

### 04-kalman-filter

**目标：** 传感器数据融合与噪声滤除

**内容：**
- 一维卡尔曼滤波器（温度去噪）
- 互补滤波器（IMU 姿态融合）
- 与移动平均的性能对比
- 参数调优指南

**卡尔曼滤波器公式：**
```
预测：x̂⁻ = x̂，P⁻ = P + Q
增益：K = P⁻ / (P⁻ + R)
更新：x̂ = x̂⁻ + K·(z - x̂⁻)
      P = (1 - K)·P⁻
```

---

## TFLite Micro 快速入门

### 步骤1：Python 训练模型

```python
# train_model.py
import numpy as np
import tensorflow as tf

# 1. 准备数据
x_train = np.linspace(0, 2*np.pi, 1000, dtype=np.float32)
y_train = np.sin(x_train)

# 2. 构建模型
model = tf.keras.Sequential([
    tf.keras.layers.Dense(16, activation='relu', input_shape=(1,)),
    tf.keras.layers.Dense(16, activation='relu'),
    tf.keras.layers.Dense(1)
])
model.compile(optimizer='adam', loss='mse')
model.fit(x_train, y_train, epochs=500, batch_size=32, verbose=0)

# 3. 转换为 TFLite（带量化）
converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
tflite_model = converter.convert()

# 4. 保存
with open('model.tflite', 'wb') as f:
    f.write(tflite_model)
print(f"模型大小: {len(tflite_model)/1024:.1f} KB")
```

### 步骤2：转换为 C 数组

```bash
# Linux/macOS
xxd -i model.tflite > model_data.cc

# Windows（PowerShell）
$bytes = [System.IO.File]::ReadAllBytes("model.tflite")
$hex = $bytes | ForEach-Object { "0x{0:x2}" -f $_ }
"const unsigned char model_data[] = {$($hex -join ', ')};" | Out-File model_data.cc
"const int model_data_len = $($bytes.Length);" | Out-File model_data.cc -Append
```

### 步骤3：ESP32 推理代码

```cpp
// main.cc
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model_data.h"

// 分配 TFLite 工作内存（根据模型大小调整）
constexpr int kTensorArenaSize = 8 * 1024;
uint8_t tensor_arena[kTensorArenaSize];

extern "C" void app_main() {
    // 1. 加载模型
    const tflite::Model* model = tflite::GetModel(g_model_data);

    // 2. 注册算子
    tflite::MicroMutableOpResolver<4> resolver;
    resolver.AddFullyConnected();
    resolver.AddRelu();

    // 3. 创建解释器
    tflite::MicroInterpreter interpreter(
        model, resolver, tensor_arena, kTensorArenaSize);
    interpreter.AllocateTensors();

    // 4. 设置输入
    TfLiteTensor* input = interpreter.input(0);
    input->data.f[0] = 1.0f;  // x = 1.0 弧度

    // 5. 运行推理
    interpreter.Invoke();

    // 6. 读取输出
    TfLiteTensor* output = interpreter.output(0);
    float result = output->data.f[0];
    printf("sin(1.0) ≈ %.4f (真实值: %.4f)\n", result, sinf(1.0f));
}
```

---

## 模型训练与部署流程

```
┌─────────────────────────────────────────────────────────┐
│                    PC 端（训练）                         │
│                                                         │
│  数据收集 → 特征工程 → 模型训练 → 验证评估              │
│     ↓           ↓          ↓          ↓                 │
│  传感器CSV   归一化/FFT  TF/PyTorch  准确率/MAE          │
│                                                         │
│                    ↓ 模型转换                           │
│                                                         │
│  .h5/.pt → TFLite转换 → 量化(INT8) → C数组(.cc)        │
└─────────────────────────────────────────────────────────┘
                         ↓ 烧录
┌─────────────────────────────────────────────────────────┐
│                   ESP32 端（推理）                       │
│                                                         │
│  传感器采集 → 预处理 → TFLite推理 → 后处理 → 执行动作   │
│     ↓           ↓          ↓          ↓         ↓      │
│  ADC/I2C    卡尔曼/FFT  模型前向   阈值判断   GPIO/MQTT │
└─────────────────────────────────────────────────────────┘
```

### 量化说明

| 量化类型 | 精度损失 | 内存节省 | 速度提升 |
|---------|---------|---------|---------|
| FP32（无量化） | 无 | 基准 | 基准 |
| FP16 | 极小 | 50% | 1.5x |
| INT8（推荐） | 小（<1%） | 75% | 2~4x |
| INT4 | 中等 | 87.5% | 3~6x |

---

## 性能优化

### 1. 模型压缩

```python
# 训练后量化（Post-Training Quantization）
converter.optimizations = [tf.lite.Optimize.DEFAULT]

# 量化感知训练（Quantization-Aware Training，精度更高）
import tensorflow_model_optimization as tfmot
model = tfmot.quantization.keras.quantize_model(model)
```

### 2. 内存优化

```c
// 将模型数据放在 Flash（RODATA），不占用 SRAM
const uint8_t model_data[] __attribute__((section(".rodata"))) = {...};

// 使用 PSRAM（ESP32 外部 PSRAM）
uint8_t *tensor_arena = heap_caps_malloc(arena_size, MALLOC_CAP_SPIRAM);
```

### 3. 计算优化

```c
// 启用 ESP32 浮点加速单元
// ESP-IDF 默认已启用，确保 sdkconfig 中：
// CONFIG_ESP32_DEFAULT_CPU_FREQ_240=y
// CONFIG_MBEDTLS_HARDWARE_AES=y

// 使用 SIMD 指令（ESP32-S3）
// TFLite Micro 自动利用 ESP32-S3 的向量指令
```

### 4. 推理频率建议

| 应用场景 | 推荐频率 | 原因 |
|---------|---------|------|
| 关键词检测 | 10~30 Hz | 音频帧率 |
| 姿态估计 | 50~100 Hz | IMU 采样率 |
| 温度异常检测 | 1~10 Hz | 温度变化慢 |
| 图像分类 | 1~5 Hz | 计算量大 |

---

## 常见应用场景

### 场景1：智能温控系统

```
DHT11/BMP280 → 卡尔曼滤波 → 异常检测 → MQTT告警
                    ↓
              神经网络预测 → PWM控制风扇/加热器
```

### 场景2：设备预测性维护

```
振动传感器(ADC) → FFT特征提取 → MLP分类器 → 故障预警
                                    ↓
                              正常/磨损/故障
```

### 场景3：手势识别（ESP32-S3 + MPU6050）

```
MPU6050(I2C) → 卡尔曼滤波 → 特征提取 → TFLite推理 → 手势类别
  加速度计                  滑动窗口     CNN/LSTM
  陀螺仪
```

### 场景4：语音关键词检测

```
麦克风(I2S) → MFCC特征 → TFLite推理 → 关键词识别
                          小型CNN
                          (~20KB)
```

---

## 相关资源

### 官方文档
- [TFLite Micro 官方文档](https://www.tensorflow.org/lite/microcontrollers)
- [ESP-TFLite-Micro GitHub](https://github.com/espressif/esp-tflite-micro)
- [ESP-DL 深度学习库](https://github.com/espressif/esp-dl)
- [ESP-WHO 人脸检测](https://github.com/espressif/esp-who)

### 学习资源
- [TinyML 课程（哈佛大学）](https://www.edx.org/professional-certificate/harvardx-tiny-machine-learning)
- [Edge Impulse（在线训练平台）](https://edgeimpulse.com/)
- [Wio Terminal TinyML 教程](https://wiki.seeedstudio.com/Wio-Terminal-TinyML/)

### 数据集
- [UCI ML Repository](https://archive.ics.uci.edu/ml/)
- [Kaggle 传感器数据集](https://www.kaggle.com/datasets?tags=13208-Sensors)
- [Google Speech Commands](https://www.tensorflow.org/datasets/catalog/speech_commands)