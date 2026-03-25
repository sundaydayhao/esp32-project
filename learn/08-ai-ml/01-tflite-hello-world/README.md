# TFLite Micro 入门示例 - 正弦波预测

## 概述

本示例演示如何在 ESP32 上运行神经网络推理，预测正弦函数值。这是 TensorFlow Lite for Microcontrollers 的经典入门示例。

## 学习目标

- 理解边缘 AI（Edge AI）的基本概念
- 了解 TFLite Micro 的工作流程
- 掌握神经网络推理的基本步骤
- 学习如何评估模型性能

## 神经网络结构

```
输入层 (1)  →  隐藏层1 (16, ReLU)  →  隐藏层2 (16, ReLU)  →  输出层 (1)
  x值              特征提取                  特征组合              sin(x)预测
```

## TFLite Micro 工作流程

```
Python 训练模型
      ↓
转换为 TFLite 格式 (.tflite)
      ↓
量化（可选，减小模型体积）
      ↓
转换为 C 数组 (xxd -i model.tflite > model_data.cc)
      ↓
嵌入 ESP32 固件
      ↓
运行推理
```

## 安装 esp-tflite-micro 组件

```bash
# 方法1：使用 idf.py
idf.py add-dependency "esp-tflite-micro>=1.0.0"

# 方法2：创建 idf_component.yml
cat > main/idf_component.yml << EOF
dependencies:
  esp-tflite-micro:
    version: ">=1.0.0"
EOF
```

## 使用 Python 训练模型

```python
# train_sine_model.py
import numpy as np
import tensorflow as tf

# 生成训练数据
x_train = np.linspace(0, 2 * np.pi, 1000).astype(np.float32)
y_train = np.sin(x_train).astype(np.float32)

# 构建模型
model = tf.keras.Sequential([
    tf.keras.layers.Dense(16, activation='relu', input_shape=(1,)),
    tf.keras.layers.Dense(16, activation='relu'),
    tf.keras.layers.Dense(1)
])

model.compile(optimizer='adam', loss='mse')
model.fit(x_train, y_train, epochs=500, verbose=0)

# 转换为 TFLite
converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]  # 量化
tflite_model = converter.convert()

# 保存
with open('sine_model.tflite', 'wb') as f:
    f.write(tflite_model)

print(f"模型大小: {len(tflite_model)} bytes")

# 转换为 C 数组（Linux/macOS）
# xxd -i sine_model.tflite > sine_model_data.cc
```

## 编译运行

```bash
cd 08-ai-ml/01-tflite-hello-world
idf.py set-target esp32
idf.py build
idf.py -p COM3 flash monitor
```

## 预期输出

```
I (xxx) TFLite-HelloWorld: ╔══════════════════════════════════════╗
I (xxx) TFLite-HelloWorld: ║  TFLite Micro - 正弦波预测示例       ║
I (xxx) TFLite-HelloWorld: ╚══════════════════════════════════════╝
I (xxx) TFLite-HelloWorld: ========================================
I (xxx) TFLite-HelloWorld:   神经网络推理评估（20个测试点）
I (xxx) TFLite-HelloWorld: ========================================
I (xxx) TFLite-HelloWorld:   x(rad)  | 真实值  | 预测值  | 误差
I (xxx) TFLite-HelloWorld:    0.000  | +0.000  | +0.012  | 0.0123
I (xxx) TFLite-HelloWorld:    0.330  | +0.324  | +0.318  | 0.0056
...
I (xxx) TFLite-HelloWorld:   平均绝对误差 (MAE): 0.0234
I (xxx) TFLite-HelloWorld:   单次推理：2.34 us
I (xxx) TFLite-HelloWorld:   推理频率：427350 Hz
```

## ESP32 边缘 AI 能力

| 指标 | ESP32 | ESP32-S3 |
|------|-------|----------|
| CPU | 240MHz 双核 | 240MHz 双核 |
| SRAM | 520KB | 512KB |
| Flash | 4MB+ | 8MB+ |
| 向量指令 | 无 | 有（AI加速） |
| 推理速度 | ~100K ops/s | ~500K ops/s |
| 适合模型 | <100KB | <500KB |

## 相关资源

- [TFLite Micro 官方文档](https://www.tensorflow.org/lite/microcontrollers)
- [ESP-TFLite-Micro GitHub](https://github.com/espressif/esp-tflite-micro)
- [ESP-DL（乐鑫深度学习库）](https://github.com/espressif/esp-dl)
- [ESP-WHO（人脸检测）](https://github.com/espressif/esp-who)