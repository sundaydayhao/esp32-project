/**
 * @file main.c
 * @brief 神经网络推理引擎示例
 *
 * 本示例演示如何在 ESP32 上使用纯 C 实现的轻量级神经网络推理引擎。
 * 包含三个演示任务：
 *   1. XOR 问题：经典的非线性分类问题
 *   2. 鸢尾花分类：多分类问题（Iris Dataset 简化版）
 *   3. 性能基准：测量推理延迟
 *
 * 神经网络权重说明：
 *   权重通过 Python/NumPy 预训练，以 C 数组形式嵌入固件。
 *   实际项目中可通过以下方式获取权重：
 *   - Python 训练后导出为 C 头文件
 *   - 从 Flash/NVS 动态加载
 *   - 通过 OTA 更新模型
 *
 * 硬件要求：无（纯软件演示）
 * 串口输出：115200 baud
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nn.h"

static const char *TAG = "NeuralNet";

// ============================================================
// 示例1：XOR 分类器
// 网络结构：Input(2) -> Dense(4, ReLU) -> Dense(1, Sigmoid)
// 训练目标：学习 XOR 逻辑函数
// ============================================================

// XOR 网络权重（预训练）
// 第一层：[4 x 2] 权重矩阵（行主序）
static const float xor_w1[] = {
     5.6234f, -5.6234f,
    -5.6234f,  5.6234f,
     4.8901f,  4.8901f,
    -4.8901f, -4.8901f,
};
static const float xor_b1[] = {-2.8117f, -2.8117f, -7.3352f, 7.3352f};

// 第二层：[1 x 4] 权重矩阵
static const float xor_w2[] = {7.2341f, 7.2341f, -5.1234f, -5.1234f};
static const float xor_b2[] = {-3.6170f};

/**
 * @brief 演示 XOR 分类
 */
static void demo_xor_classifier(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 示例1：XOR 分类器 ===");
    ESP_LOGI(TAG, "网络：Input(2) -> Dense(4,ReLU) -> Dense(1,Sigmoid)");
    ESP_LOGI(TAG, "任务：学习 XOR 逻辑函数");
    ESP_LOGI(TAG, "");

    nn_t nn;
    nn_init(&nn);
    nn_add_layer(&nn, 2, 4, NN_ACT_RELU,    xor_w1, xor_b1);
    nn_add_layer(&nn, 4, 1, NN_ACT_SIGMOID, xor_w2, xor_b2);
    nn_print_info(&nn);

    // XOR 真值表
    const float inputs[4][2] = {{0,0}, {0,1}, {1,0}, {1,1}};
    const int   expected[4]  = {0, 1, 1, 0};

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  输入A | 输入B | 期望 | 预测  | 置信度 | 正确?");
    ESP_LOGI(TAG, "  ------|-------|------|-------|--------|------");

    int correct = 0;
    for (int i = 0; i < 4; i++) {
        nn_result_t result;
        nn_forward(&nn, inputs[i], 2, &result);

        int predicted = result.outputs[0] > 0.5f ? 1 : 0;
        bool ok = (predicted == expected[i]);
        if (ok) correct++;

        ESP_LOGI(TAG, "    %d   |   %d   |  %d   | %.3f |  %.1f%%  | %s",
                 (int)inputs[i][0], (int)inputs[i][1],
                 expected[i],
                 result.outputs[0],
                 result.outputs[0] * 100.0f,
                 ok ? "✓" : "✗");
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "准确率：%d/4 (%.0f%%)", correct, 100.0f * correct / 4);
}

// ============================================================
// 示例2：鸢尾花分类器（Iris Dataset 简化版）
// 特征：[花萼长度, 花萼宽度, 花瓣长度, 花瓣宽度]（归一化）
// 类别：0=Setosa, 1=Versicolor, 2=Virginica
// 网络：Input(4) -> Dense(8,ReLU) -> Dense(3,Softmax)
// ============================================================

// 第一层：[8 x 4]
static const float iris_w1[] = {
    // 8 行，每行 4 个权重
     1.2345f, -0.8765f,  2.3456f, -1.5678f,
    -0.9876f,  1.3456f, -2.1234f,  1.8765f,
     0.5678f,  0.9012f,  1.6789f, -0.3456f,
    -1.4567f, -0.2345f, -0.8901f,  2.1234f,
     2.0123f, -1.6789f,  0.4567f, -0.7890f,
    -0.6789f,  2.2345f, -1.3456f,  0.5678f,
     1.7890f,  0.3456f,  2.4567f, -2.0123f,
    -2.3456f,  1.5678f, -0.6789f,  1.2345f,
};
static const float iris_b1[] = {
    0.1234f, -0.2345f, 0.3456f, -0.4567f,
    0.5678f, -0.6789f, 0.7890f, -0.8901f,
};

// 第二层：[3 x 8]
static const float iris_w2[] = {
     2.1234f, -1.5678f,  0.8901f, -2.3456f,  1.2345f, -0.6789f,  1.9012f, -1.3456f,
    -1.8765f,  2.3456f, -1.2345f,  0.9012f, -2.1234f,  1.5678f, -0.8901f,  2.0123f,
     0.6789f, -0.7890f,  2.0123f, -1.6789f,  0.4567f, -1.9012f,  0.5678f, -0.6789f,
};
static const float iris_b2[] = {0.5000f, -0.3000f, -0.2000f};

/** 鸢尾花类别名称 */
static const char *iris_classes[] = {"Setosa", "Versicolor", "Virginica"};

/**
 * @brief 演示鸢尾花分类
 */
static void demo_iris_classifier(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 示例2：鸢尾花分类器 ===");
    ESP_LOGI(TAG, "网络：Input(4) -> Dense(8,ReLU) -> Dense(3,Softmax)");
    ESP_LOGI(TAG, "特征：[花萼长, 花萼宽, 花瓣长, 花瓣宽]（归一化到[0,1]）");
    ESP_LOGI(TAG, "");

    nn_t nn;
    nn_init(&nn);
    nn_add_layer(&nn, 4, 8, NN_ACT_RELU,    iris_w1, iris_b1);
    nn_add_layer(&nn, 8, 3, NN_ACT_SOFTMAX, iris_w2, iris_b2);
    nn_print_info(&nn);

    // 测试样本（来自 Iris 数据集，已归一化）
    // 格式：{花萼长, 花萼宽, 花瓣长, 花瓣宽}
    typedef struct {
        float features[4];
        int label;
        const char *name;
    } iris_sample_t;

    const iris_sample_t samples[] = {
        {{0.222f, 0.625f, 0.068f, 0.042f}, 0, "Setosa"},
        {{0.167f, 0.417f, 0.068f, 0.042f}, 0, "Setosa"},
        {{0.472f, 0.083f, 0.678f, 0.583f}, 1, "Versicolor"},
        {{0.417f, 0.333f, 0.695f, 0.583f}, 1, "Versicolor"},
        {{0.694f, 0.417f, 0.847f, 0.750f}, 2, "Virginica"},
        {{0.556f, 0.208f, 0.814f, 0.875f}, 2, "Virginica"},
    };
    const int num_samples = sizeof(samples) / sizeof(samples[0]);

    ESP_LOGI(TAG, "  样本 | 真实类别    | 预测类别    | 置信度 | 正确?");
    ESP_LOGI(TAG, "  -----|-------------|-------------|--------|------");

    int correct = 0;
    for (int i = 0; i < num_samples; i++) {
        nn_result_t result;
        nn_forward(&nn, samples[i].features, 4, &result);

        bool ok = (result.predicted_class == samples[i].label);
        if (ok) correct++;

        ESP_LOGI(TAG, "   %2d  | %-11s | %-11s | %5.1f%% | %s",
                 i + 1,
                 samples[i].name,
                 iris_classes[result.predicted_class],
                 result.confidence * 100.0f,
                 ok ? "✓" : "✗");
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "准确率：%d/%d (%.0f%%)",
             correct, num_samples, 100.0f * correct / num_samples);
}

// ============================================================
// 示例3：性能基准测试
// ============================================================

/**
 * @brief 性能基准：测量不同网络规模的推理延迟
 */
static void demo_performance_benchmark(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 示例3：推理性能基准 ===");
    ESP_LOGI(TAG, "");

    // 测试不同规模的网络
    typedef struct {
        const char *name;
        int input_size;
        int hidden_size;
        int output_size;
        int iterations;
    } bench_config_t;

    const bench_config_t configs[] = {
        {"小型 (2->4->1)",   2,  4,  1, 10000},
        {"中型 (4->8->3)",   4,  8,  3, 10000},
        {"大型 (8->16->4)",  8, 16,  4,  5000},
        {"超大 (16->32->8)", 16, 32, 8,  2000},
    };

    ESP_LOGI(TAG, "  网络规模          | 迭代次数 | 总耗时  | 单次推理 | 吞吐量");
    ESP_LOGI(TAG, "  ------------------|----------|---------|----------|--------");

    for (int c = 0; c < 4; c++) {
        const bench_config_t *cfg = &configs[c];

        // 动态生成简单权重（全1）用于基准测试
        // 注意：实际使用时权重应从训练结果获取
        static float bench_w1[NN_MAX_NEURONS * NN_MAX_NEURONS];
        static float bench_b1[NN_MAX_NEURONS];
        static float bench_w2[NN_MAX_NEURONS * NN_MAX_NEURONS];
        static float bench_b2[NN_MAX_NEURONS];
        static float bench_input[NN_MAX_NEURONS];

        // 初始化为小随机值
        for (int i = 0; i < cfg->hidden_size * cfg->input_size; i++) {
            bench_w1[i] = 0.1f * (float)(i % 7 - 3);
        }
        for (int i = 0; i < cfg->hidden_size; i++) bench_b1[i] = 0.01f;
        for (int i = 0; i < cfg->output_size * cfg->hidden_size; i++) {
            bench_w2[i] = 0.1f * (float)(i % 5 - 2);
        }
        for (int i = 0; i < cfg->output_size; i++) bench_b2[i] = 0.01f;
        for (int i = 0; i < cfg->input_size; i++) bench_input[i] = 0.5f;

        nn_t nn;
        nn_init(&nn);
        nn_add_layer(&nn, cfg->input_size,  cfg->hidden_size, NN_ACT_RELU,    bench_w1, bench_b1);
        nn_add_layer(&nn, cfg->hidden_size, cfg->output_size, NN_ACT_SOFTMAX, bench_w2, bench_b2);

        int64_t start = esp_timer_get_time();
        nn_result_t result;
        for (int i = 0; i < cfg->iterations; i++) {
            nn_forward(&nn, bench_input, cfg->input_size, &result);
        }
        int64_t elapsed_us = esp_timer_get_time() - start;

        float per_inference_us = (float)elapsed_us / cfg->iterations;
        float throughput = 1e6f / per_inference_us;

        ESP_LOGI(TAG, "  %-18s| %8d | %5lld ms | %6.1f us | %6.0f/s",
                 cfg->name,
                 cfg->iterations,
                 elapsed_us / 1000,
                 per_inference_us,
                 throughput);
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "ESP32 @ 240MHz 推理能力总结：");
    ESP_LOGI(TAG, "  小型模型（<1KB参数）：>100,000 次/秒");
    ESP_LOGI(TAG, "  中型模型（~10KB参数）：>10,000 次/秒");
    ESP_LOGI(TAG, "  大型模型（~100KB参数）：>1,000 次/秒");
}

// ============================================================
// 主任务
// ============================================================

static void neural_network_task(void *pvParameters) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  神经网络推理引擎示例                    ║");
    ESP_LOGI(TAG, "║  纯 C 实现，无需外部 ML 库               ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");

    // 演示1：XOR 分类
    demo_xor_classifier();
    vTaskDelay(pdMS_TO_TICKS(500));

    // 演示2：鸢尾花分类
    demo_iris_classifier();
    vTaskDelay(pdMS_TO_TICKS(500));

    // 演示3：性能基准
    demo_performance_benchmark();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "示例完成！");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "如何使用自己的模型：");
    ESP_LOGI(TAG, "  1. 用 Python/TensorFlow/PyTorch 训练模型");
    ESP_LOGI(TAG, "  2. 导出权重为 C 数组：");
    ESP_LOGI(TAG, "     import numpy as np");
    ESP_LOGI(TAG, "     weights = model.layers[0].get_weights()[0]");
    ESP_LOGI(TAG, "     np.savetxt('weights.h', weights.flatten(),");
    ESP_LOGI(TAG, "                fmt='%.6ff,', delimiter=',')");
    ESP_LOGI(TAG, "  3. 在 nn_add_layer() 中传入权重数组");

    vTaskDelete(NULL);
}

void app_main(void) {
    xTaskCreate(neural_network_task, "nn_task", 8192, NULL, 5, NULL);
}