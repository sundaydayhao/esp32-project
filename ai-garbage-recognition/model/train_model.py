#!/usr/bin/env python3
"""
train_model.py
垃圾分类 TFLite 模型训练脚本
Garbage Classification TFLite Model Training Script

使用 MobileNetV2 迁移学习训练4类垃圾分类模型
Uses MobileNetV2 transfer learning for 4-class garbage classification

垃圾类别 / Classes:
  0: 有害垃圾 (Hazardous Waste)
  1: 可回收物 (Recyclable Waste)
  2: 厨余垃圾 (Kitchen Waste)
  3: 其他垃圾 (Other Waste)

数据集目录结构 / Dataset Structure:
  dataset/
    train/
      hazardous/    (有害垃圾图片)
      recyclable/   (可回收物图片)
      kitchen/      (厨余垃圾图片)
      other/        (其他垃圾图片)
    val/
      hazardous/
      recyclable/
      kitchen/
      other/

依赖安装 / Install Dependencies:
  pip install tensorflow pillow numpy matplotlib scikit-learn

运行 / Run:
  python train_model.py --data_dir ./dataset --epochs 30 --output ./output
"""

import os
import sys
import argparse
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

# 检查 TensorFlow
try:
    import tensorflow as tf
    from tensorflow import keras
    from tensorflow.keras import layers
    from tensorflow.keras.applications import MobileNetV2
    from tensorflow.keras.preprocessing.image import ImageDataGenerator
    print(f"TensorFlow 版本: {tf.__version__}")
except ImportError:
    print("错误: 请安装 TensorFlow: pip install tensorflow")
    sys.exit(1)

# ============================================================
# 配置参数 / Configuration
# ============================================================
IMG_SIZE    = 96        # 模型输入尺寸 (96x96, 适合 ESP32-S3)
BATCH_SIZE  = 32
NUM_CLASSES = 4
EPOCHS_FINETUNE = 10   # 微调轮数

CLASS_NAMES = ['hazardous', 'recyclable', 'kitchen', 'other']
CLASS_NAMES_CN = ['有害垃圾', '可回收物', '厨余垃圾', '其他垃圾']

# ============================================================
# 数据增强 / Data Augmentation
# ============================================================
def create_data_generators(data_dir, img_size, batch_size):
    """创建训练和验证数据生成器"""

    train_datagen = ImageDataGenerator(
        rescale=1.0/255.0,
        rotation_range=20,
        width_shift_range=0.2,
        height_shift_range=0.2,
        shear_range=0.1,
        zoom_range=0.2,
        horizontal_flip=True,
        brightness_range=[0.7, 1.3],
        fill_mode='nearest'
    )

    val_datagen = ImageDataGenerator(rescale=1.0/255.0)

    train_dir = os.path.join(data_dir, 'train')
    val_dir   = os.path.join(data_dir, 'val')

    if not os.path.exists(train_dir):
        print(f"错误: 训练数据目录不存在: {train_dir}")
        print("请按以下结构准备数据集:")
        print("  dataset/train/hazardous/  (有害垃圾图片)")
        print("  dataset/train/recyclable/ (可回收物图片)")
        print("  dataset/train/kitchen/    (厨余垃圾图片)")
        print("  dataset/train/other/      (其他垃圾图片)")
        sys.exit(1)

    train_gen = train_datagen.flow_from_directory(
        train_dir,
        target_size=(img_size, img_size),
        batch_size=batch_size,
        class_mode='categorical',
        classes=CLASS_NAMES,
        shuffle=True
    )

    val_gen = val_datagen.flow_from_directory(
        val_dir,
        target_size=(img_size, img_size),
        batch_size=batch_size,
        class_mode='categorical',
        classes=CLASS_NAMES,
        shuffle=False
    )

    print(f"\n训练集: {train_gen.samples} 张图片")
    print(f"验证集: {val_gen.samples} 张图片")
    print(f"类别: {train_gen.class_indices}")

    return train_gen, val_gen


# ============================================================
# 模型构建 / Model Building
# ============================================================
def build_model(img_size, num_classes):
    """
    构建基于 MobileNetV2 的迁移学习模型
    输入: (96, 96, 3) RGB 图像
    输出: (4,) 各类别概率
    """
    # 加载预训练 MobileNetV2 (不含顶层)
    base_model = MobileNetV2(
        input_shape=(img_size, img_size, 3),
        include_top=False,
        weights='imagenet'
    )

    # 第一阶段: 冻结基础模型，只训练顶层
    base_model.trainable = False

    # 构建完整模型
    inputs = keras.Input(shape=(img_size, img_size, 3))
    x = base_model(inputs, training=False)
    x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dropout(0.3)(x)
    x = layers.Dense(128, activation='relu')(x)
    x = layers.Dropout(0.2)(x)
    outputs = layers.Dense(num_classes, activation='softmax')(x)

    model = keras.Model(inputs, outputs)

    model.compile(
        optimizer=keras.optimizers.Adam(learning_rate=1e-3),
        loss='categorical_crossentropy',
        metrics=['accuracy']
    )

    print("\n模型结构:")
    model.summary()
    print(f"总参数: {model.count_params():,}")

    return model, base_model


def finetune_model(model, base_model, train_gen, val_gen, epochs):
    """微调: 解冻部分层进行精细训练"""
    print("\n开始微调 (解冻最后 30 层)...")

    # 解冻最后 30 层
    base_model.trainable = True
    for layer in base_model.layers[:-30]:
        layer.trainable = False

    model.compile(
        optimizer=keras.optimizers.Adam(learning_rate=1e-5),
        loss='categorical_crossentropy',
        metrics=['accuracy']
    )

    callbacks = [
        keras.callbacks.EarlyStopping(
            monitor='val_accuracy',
            patience=5,
            restore_best_weights=True
        ),
        keras.callbacks.ReduceLROnPlateau(
            monitor='val_loss',
            factor=0.5,
            patience=3,
            min_lr=1e-7
        )
    ]

    history = model.fit(
        train_gen,
        epochs=epochs,
        validation_data=val_gen,
        callbacks=callbacks,
        verbose=1
    )

    return history


# ============================================================
# 模型转换 / Model Conversion
# ============================================================
def convert_to_tflite(model, output_dir, quantize=True):
    """将 Keras 模型转换为 TFLite 格式"""
    os.makedirs(output_dir, exist_ok=True)

    # 标准 TFLite 转换
    converter = tf.lite.TFLiteConverter.from_keras_model(model)

    if quantize:
        print("\n应用 INT8 量化...")
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.target_spec.supported_types = [tf.float16]

    tflite_model = converter.convert()

    # 保存 TFLite 模型
    tflite_path = os.path.join(output_dir, 'garbage_classifier.tflite')
    with open(tflite_path, 'wb') as f:
        f.write(tflite_model)

    size_kb = len(tflite_model) / 1024
    print(f"TFLite 模型已保存: {tflite_path} ({size_kb:.1f} KB)")

    # 转换为 C 头文件 (用于嵌入到固件)
    convert_to_c_header(tflite_model, output_dir)

    return tflite_path


def convert_to_c_header(tflite_model, output_dir):
    """将 TFLite 模型转换为 C 头文件"""
    header_path = os.path.join(output_dir, 'model_data.h')

    with open(header_path, 'w', encoding='utf-8') as f:
        f.write("/**\n")
        f.write(" * model_data.h\n")
        f.write(" * TFLite 垃圾分类模型数据 (自动生成)\n")
        f.write(" * 将此文件复制到 main/ 目录并设置 USE_TFLITE_MODEL=1\n")
        f.write(" */\n\n")
        f.write("#pragma once\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"// 模型大小: {len(tflite_model)} bytes\n")
        f.write(f"static const uint32_t g_model_data_len = {len(tflite_model)};\n\n")
        f.write("static const uint8_t g_model_data[] = {\n  ")

        for i, byte in enumerate(tflite_model):
            f.write(f"0x{byte:02x}")
            if i < len(tflite_model) - 1:
                f.write(", ")
                if (i + 1) % 16 == 0:
                    f.write("\n  ")

        f.write("\n};\n")

    print(f"C 头文件已生成: {header_path}")


# ============================================================
# 评估与可视化 / Evaluation & Visualization
# ============================================================
def evaluate_model(model, val_gen, output_dir):
    """评估模型性能"""
    print("\n评估模型...")

    loss, accuracy = model.evaluate(val_gen, verbose=1)
    print(f"\n验证集准确率: {accuracy*100:.2f}%")
    print(f"验证集损失:   {loss:.4f}")

    # 混淆矩阵
    try:
        from sklearn.metrics import confusion_matrix, classification_report
        import seaborn as sns

        val_gen.reset()
        y_pred = model.predict(val_gen, verbose=1)
        y_pred_classes = np.argmax(y_pred, axis=1)
        y_true = val_gen.classes

        print("\n分类报告:")
        print(classification_report(y_true, y_pred_classes,
                                    target_names=CLASS_NAMES_CN))

        # 绘制混淆矩阵
        cm = confusion_matrix(y_true, y_pred_classes)
        plt.figure(figsize=(8, 6))
        sns.heatmap(cm, annot=True, fmt='d', cmap='Blues',
                    xticklabels=CLASS_NAMES_CN,
                    yticklabels=CLASS_NAMES_CN)
        plt.title('混淆矩阵 / Confusion Matrix')
        plt.ylabel('真实标签')
        plt.xlabel('预测标签')
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'confusion_matrix.png'), dpi=150)
        print(f"混淆矩阵已保存")

    except ImportError:
        print("提示: 安装 scikit-learn 和 seaborn 可查看详细评估报告")

    return accuracy


def plot_training_history(history, output_dir):
    """绘制训练历史曲线"""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4))

    ax1.plot(history.history['accuracy'], label='训练准确率')
    ax1.plot(history.history['val_accuracy'], label='验证准确率')
    ax1.set_title('模型准确率')
    ax1.set_xlabel('Epoch')
    ax1.set_ylabel('准确率')
    ax1.legend()
    ax1.grid(True)

    ax2.plot(history.history['loss'], label='训练损失')
    ax2.plot(history.history['val_loss'], label='验证损失')
    ax2.set_title('模型损失')
    ax2.set_xlabel('Epoch')
    ax2.set_ylabel('损失')
    ax2.legend()
    ax2.grid(True)

    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, 'training_history.png'), dpi=150)
    print(f"训练曲线已保存")


# ============================================================
# 主函数 / Main Function
# ============================================================
def main():
    parser = argparse.ArgumentParser(
        description='垃圾分类 TFLite 模型训练',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python train_model.py --data_dir ./dataset --epochs 30
  python train_model.py --data_dir ./dataset --epochs 50 --no_quantize
        """
    )
    parser.add_argument('--data_dir',    default='./dataset',
                        help='数据集目录 (默认: ./dataset)')
    parser.add_argument('--output',      default='./output',
                        help='输出目录 (默认: ./output)')
    parser.add_argument('--epochs',      type=int, default=30,
                        help='训练轮数 (默认: 30)')
    parser.add_argument('--batch_size',  type=int, default=32,
                        help='批次大小 (默认: 32)')
    parser.add_argument('--img_size',    type=int, default=96,
                        help='图像尺寸 (默认: 96)')
    parser.add_argument('--no_quantize', action='store_true',
                        help='不进行量化')
    args = parser.parse_args()

    print("=" * 50)
    print("  AI 垃圾分类模型训练")
    print("=" * 50)
    print(f"数据目录: {args.data_dir}")
    print(f"输出目录: {args.output}")
    print(f"训练轮数: {args.epochs}")
    print(f"图像尺寸: {args.img_size}x{args.img_size}")
    print(f"量化:     {'否' if args.no_quantize else '是 (FP16)'}")
    print()

    os.makedirs(args.output, exist_ok=True)

    # 1. 准备数据
    train_gen, val_gen = create_data_generators(
        args.data_dir, args.img_size, args.batch_size)

    # 2. 构建模型
    model, base_model = build_model(args.img_size, NUM_CLASSES)

    # 3. 第一阶段训练 (冻结基础层)
    print(f"\n第一阶段: 训练顶层 ({args.epochs // 2} 轮)...")
    callbacks_stage1 = [
        keras.callbacks.EarlyStopping(
            monitor='val_accuracy', patience=8, restore_best_weights=True),
        keras.callbacks.ModelCheckpoint(
            os.path.join(args.output, 'best_model.h5'),
            monitor='val_accuracy', save_best_only=True, verbose=1)
    ]

    history1 = model.fit(
        train_gen,
        epochs=args.epochs // 2,
        validation_data=val_gen,
        callbacks=callbacks_stage1,
        verbose=1
    )

    # 4. 第二阶段: 微调
    print(f"\n第二阶段: 微调 ({EPOCHS_FINETUNE} 轮)...")
    history2 = finetune_model(model, base_model, train_gen, val_gen,
                               EPOCHS_FINETUNE)

    # 合并历史
    combined_history = type('History', (), {
        'history': {
            'accuracy':     history1.history['accuracy'] + history2.history['accuracy'],
            'val_accuracy': history1.history['val_accuracy'] + history2.history['val_accuracy'],
            'loss':         history1.history['loss'] + history2.history['loss'],
            'val_loss':     history1.history['val_loss'] + history2.history['val_loss'],
        }
    })()

    # 5. 评估
    accuracy = evaluate_model(model, val_gen, args.output)

    # 6. 绘制训练曲线
    plot_training_history(combined_history, args.output)

    # 7. 保存 Keras 模型
    keras_path = os.path.join(args.output, 'garbage_classifier.h5')
    model.save(keras_path)
    print(f"\nKeras 模型已保存: {keras_path}")

    # 8. 转换为 TFLite
    tflite_path = convert_to_tflite(model, args.output,
                                     quantize=not args.no_quantize)

    print("\n" + "=" * 50)
    print("  训练完成!")
    print("=" * 50)
    print(f"最终验证准确率: {accuracy*100:.2f}%")
    print(f"TFLite 模型: {tflite_path}")
    print(f"C 头文件: {os.path.join(args.output, 'model_data.h')}")
    print()
    print("下一步:")
    print("  1. 将 model_data.h 复制到 main/ 目录")
    print("  2. 在 main/garbage_classifier.cpp 中设置 USE_TFLITE_MODEL=1")
    print("  3. 重新编译并烧录固件")


if __name__ == '__main__':
    main()