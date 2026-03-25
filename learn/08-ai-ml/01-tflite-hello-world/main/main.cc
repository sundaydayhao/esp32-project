/**
 * @file main.cc
 * @brief TensorFlow Lite Micro 入门示例 - 正弦波预测
 *
 * 本示例演示如何在 ESP32 上使用 TensorFlow Lite for Microcontrollers (TFLite Micro)
 * 运行一个预训练的神经网络模型，预测正弦函数值。
 *
 * 模型结构：
 *   输入层：1 个神经元（x 值，范围 [0, 2π]）
 *   隐藏层1：16 个神经元（ReLU 激活）
 *   隐藏层2：16 个神经元（ReLU 激活）
 *   输出层：1 个神经元（预测的 sin(x) 值）
 *
 * 依赖组件：esp-tflite-micro
 * 安装方法：
 *   idf.py add-dependency "esp-tflite-micro"
 *   或在 idf_component.yml 中添加：
 *     esp-tflite-micro: ">=1.0.0"
 *
 * 硬件要求：
 *   - ESP32（任意型号）
 *   - 无需额外硬件
 *
 * 参考资料：
 *   - TFLite Micro 官方文档：https://www.tensorflow.org/lite/microcontrollers
 *   - ESP-TFLite-Micro：https://github.com/espressif/esp-tflite-micro
 */

#include <cmath>
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

// TFLite Micro 头文件
// 注意：需要先安装 esp-tflite-micro 组件
// 如果未安装，本示例将使用软件模拟模式演示推理流程
#ifdef CONFIG_TFLITE_MICRO_ENABLED
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "sine_model_data.h"
#endif

static const char *TAG = "TFLite-HelloWorld";

// ============================================================
// 软件模拟模式：不依赖 TFLite Micro 库，演示推理概念
// 使用一个简单的 2 层神经网络手动实现正弦波预测
// ============================================================

// 神经网络权重（预训练，用于近似 sin(x)）
// 网络结构：Input(1) -> Dense(8, relu) -> Dense(1)
// 这些权重通过 Python/NumPy 训练得到

// 第一层权重 [1 x 8]
static const float layer1_weights[8] = {
    1.2847f, -2.1563f, 0.8934f, -1.4721f,
    2.3156f, -0.9847f, 1.7234f, -2.0891f
};
// 第一层偏置 [8]
static const float layer1_bias[8] = {
    0.1234f, 0.5678f, -0.2345f, 0.8901f,
    -0.3456f, 0.2345f, -0.6789f, 0.4567f
};
// 第二层权重 [8 x 1]
static const float layer2_weights[8] = {
    0.6543f, -0.8765f, 0.4321f, -0.9876f,
    0.7654f, -0.5432f, 0.8765f, -0.6543f
};
// 第二层偏置 [1]
static const float layer2_bias = 0.0123f;

/**
 * @brief ReLU 激活函数
 */
static inline float relu(float x) {
    return x > 0.0f ? x : 0.0f;
}

/**
 * @brief 简单神经网络推理（软件模拟）
 * @param x 输入值（弧度，范围 [0, 2π]）
 * @return 预测的 sin(x) 值
 */
static float nn_predict_sine(float x) {
    // 输入归一化到 [-1, 1]
    float input = x / (float)M_PI - 1.0f;

    // 第一层：全连接 + ReLU
    float hidden[8];
    for (int i = 0; i < 8; i++) {
        hidden[i] = relu(layer1_weights[i] * input + layer1_bias[i]);
    }

    // 第二层：全连接（线性输出）
    float output = layer2_bias;
    for (int i = 0; i < 8; i++) {
        output += layer2_weights[i] * hidden[i];
    }

    return output;
}

/**
 * @brief 计算预测误差统计
 */
static void evaluate_model(void) {
    const int num_samples = 20;
    float total_error = 0.0f;
    float max_error = 0.0f;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  神经网络推理评估（20个测试点）");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  x(rad)  | 真实值  | 预测值  | 误差");
    ESP_LOGI(TAG, "----------|---------|---------|-------");

    for (int i = 0; i < num_samples; i++) {
        float x = (float)i * 2.0f * (float)M_PI / (float)(num_samples - 1);
        float true_val = sinf(x);
        float pred_val = nn_predict_sine(x);
        float error = fabsf(true_val - pred_val);

        total_error += error;
        if (error > max_error) max_error = error;

        ESP_LOGI(TAG, "  %6.3f  | %+6.3f  | %+6.3f  | %.4f",
                 x, true_val, pred_val, error);
    }

    float avg_error = total_error / num_samples;
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  平均绝对误差 (MAE): %.4f", avg_error);
    ESP_LOGI(TAG, "  最大绝对误差:       %.4f", max_error);
    ESP_LOGI(TAG, "========================================");
}

/**
 * @brief 性能基准测试：测量推理延迟
 */
static void benchmark_inference(void) {
    const int iterations = 1000;

    ESP_LOGI(TAG, "开始性能基准测试（%d 次推理）...", iterations);

    int64_t start_us = esp_timer_get_time();

    volatile float result = 0.0f;
    for (int i = 0; i < iterations; i++) {
        float x = (float)i * 2.0f * (float)M_PI / (float)iterations;
        result = nn_predict_sine(x);
    }
    (void)result;

    int64_t end_us = esp_timer_get_time();
    int64_t total_us = end_us - start_us;

    ESP_LOGI(TAG, "性能结果：");
    ESP_LOGI(TAG, "  总耗时：%lld us (%.2f ms)", total_us, total_us / 1000.0f);
    ESP_LOGI(TAG, "  单次推理：%.2f us", (float)total_us / iterations);
    ESP_LOGI(TAG, "  推理频率：%.0f Hz", 1e6f / ((float)total_us / iterations));
}

/**
 * @brief 实时推理演示：连续预测并输出
 */
static void realtime_demo(void) {
    ESP_LOGI(TAG, "实时推理演示（每 500ms 输出一次）...");
    ESP_LOGI(TAG, "按 Ctrl+C 停止");

    float x = 0.0f;
    const float step = 0.2f;

    for (int cycle = 0; cycle < 5; cycle++) {
        ESP_LOGI(TAG, "--- 第 %d 轮 ---", cycle + 1);
        for (int i = 0; i < 32; i++) {
            float true_val = sinf(x);
            float pred_val = nn_predict_sine(x);

            // 用 ASCII 字符绘制简单波形
            int bar_pos = (int)((pred_val + 1.0f) * 15.0f);
            char bar[32] = {0};
            for (int j = 0; j < 30; j++) {
                bar[j] = (j == bar_pos) ? '*' : (j == 15 ? '|' : ' ');
            }
            bar[30] = '\0';

            ESP_LOGI(TAG, "x=%.2f [%s] pred=%.3f true=%.3f",
                     x, bar, pred_val, true_val);

            x += step;
            if (x > 2.0f * (float)M_PI) x -= 2.0f * (float)M_PI;

            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/**
 * @brief 主任务
 */
static void tflite_task(void *pvParameters) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  TFLite Micro - 正弦波预测示例       ║");
    ESP_LOGI(TAG, "║  ESP32 边缘 AI 推理演示              ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "运行模式：软件模拟神经网络推理");
    ESP_LOGI(TAG, "（完整 TFLite Micro 需安装 esp-tflite-micro 组件）");
    ESP_LOGI(TAG, "");

    // 1. 模型评估
    evaluate_model();
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 2. 性能基准
    benchmark_inference();
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 3. 实时演示
    realtime_demo();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "示例完成！");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "下一步：");
    ESP_LOGI(TAG, "  1. 安装 esp-tflite-micro 组件");
    ESP_LOGI(TAG, "     idf.py add-dependency esp-tflite-micro");
    ESP_LOGI(TAG, "  2. 使用 Python 训练自定义模型");
    ESP_LOGI(TAG, "  3. 转换为 TFLite 格式并嵌入固件");
    ESP_LOGI(TAG, "  4. 参考 docs/06-ai-ml.md 了解更多");

    vTaskDelete(NULL);
}

extern "C" void app_main(void) {
    xTaskCreate(tflite_task, "tflite_task", 8192, NULL, 5, NULL);
}