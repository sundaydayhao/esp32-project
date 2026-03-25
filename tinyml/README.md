# ESP32-S3 TinyML Examples

这个仓库按 **“每一个实例使用一个单独的文件夹”** 的方式组织，便于在 VS Code 中分别打开、构建和扩展不同的 TensorFlow Lite Micro 示例。

## 目录结构

```text
.
├── hello_world/        # 独立的 ESP-IDF hello_world 示例
├── micro_speech/       # 独立的 ESP-IDF micro_speech 示例骨架
├── person_detection/   # 独立的 ESP-IDF person_detection 示例骨架
├── magic_wand/         # 独立的 ESP-IDF IMU 手势识别通用骨架
└── third_party/
    └── tflite-micro/   # 官方 TensorFlow Lite Micro 源码参考
```

## 1. hello_world

位置：

```text
hello_world/
```

说明：
- 已适配为 **ESP32-S3 + ESP-IDF** 工程
- 基于官方 `tensorflow/lite/micro/examples/hello_world`
- 已内置模型，可直接作为最小推理示例使用
- 串口会打印 `sin(x)` 近似推理结果

进入该目录后构建：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

## 2. micro_speech

位置：

```text
micro_speech/
```

说明：
- 已拆分为单独工程目录
- 已加入：
  - `audio_preprocessor_int8.tflite`
  - `micro_speech_quantized.tflite`
  - 对应的 C 数组模型文件
- 当前版本是 **ESP32-S3 上的 micro_speech 工程骨架**
- 已完成两个模型的装载入口
- 后续需要继续接入：
  - I2S 麦克风采集
  - 音频帧缓存
  - signal ops 注册
  - 推理结果后处理（yes / no / silence / unknown）

进入该目录后构建：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

## 3. person_detection

位置：

```text
person_detection/
```

说明：
- 已拆分为单独工程目录
- 已加入官方 `person_detect.tflite`
- 已生成嵌入式 C 数组模型文件
- 当前版本是 **ESP32-S3 上的 person_detection 工程骨架**
- 已完成模型装载入口和基础算子注册
- 后续需要继续接入：
  - ESP32-S3 摄像头采集
  - 图像缩放/灰度化预处理
  - 推理结果解析（person / no person）
  - 与摄像头驱动的整合

进入该目录后构建：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

## 4. magic_wand

位置：

```text
magic_wand/
```

说明：
- 当前 `tflite-micro` 官方仓库的 `examples` 目录中未提供 `magic_wand`
- 因此这里创建的是一个 **ESP32-S3 IMU 手势识别通用骨架**
- 当前已具备：
  - 独立 ESP-IDF 工程目录
  - `app_main()` 入口
  - 后续接入 IMU + TFLM 手势模型的扩展起点
- 推荐后续接入：
  - I2C/SPI IMU 驱动
  - 三轴加速度/陀螺仪采样
  - 滑动窗口缓存
  - TFLite Micro 手势分类模型
  - 手势标签映射与串口输出

进入该目录后构建：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

## 5. 官方参考源码

位置：

```text
third_party/tflite-micro/
```

当前保留该目录是为了方便继续参考以下官方示例：

- `hello_world`
- `micro_speech`
- `person_detection`

## 当前状态

### 已完成
- `hello_world` 独立目录化
- `micro_speech` 独立目录化
- `person_detection` 独立目录化
- `magic_wand` 独立目录化
- `micro_speech` 模型文件准备完成
- `person_detection` 模型文件准备完成
- 根目录按多实例方式重新组织说明

### 待继续完善
- 为 `micro_speech` 接入真实麦克风采样链路
- 注册完整的音频预处理所需算子
- 为 `person_detection` 接入摄像头采集链路
- 为 `magic_wand` 接入 IMU 与手势模型

## 说明

- 如果 VS Code 中出现 include 报红，通常是因为 ESP-IDF 工具链和 includePath 尚未由插件正确加载。
- 实际是否可编译，请以 `idf.py build` 结果为准。
- 建议分别在 `hello_world/`、`micro_speech/`、`person_detection/`、`magic_wand/` 目录中单独打开终端并执行构建。