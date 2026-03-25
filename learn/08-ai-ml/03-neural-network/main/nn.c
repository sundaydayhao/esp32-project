/**
 * @file nn.c
 * @brief 轻量级神经网络推理引擎（纯 C 实现）
 *
 * 实现了一个简单的多层感知机（MLP）前向推理引擎。
 * 支持全连接层、ReLU/Sigmoid/Tanh/Softmax 激活函数。
 * 无需任何外部 ML 库，适合资源受限的嵌入式系统。
 *
 * 内存使用：
 *   - 权重/偏置：存储在 Flash（只读）
 *   - 推理缓冲区：2 * NN_MAX_NEURONS * 4 字节（SRAM）
 */

#include "nn.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "NeuralNet";

// ============================================================
// 激活函数实现
// ============================================================

static float act_relu(float x) {
    return x > 0.0f ? x : 0.0f;
}

static float act_sigmoid(float x) {
    // 数值稳定的 sigmoid 实现
    if (x >= 0.0f) {
        float z = expf(-x);
        return 1.0f / (1.0f + z);
    } else {
        float z = expf(x);
        return z / (1.0f + z);
    }
}

static float act_tanh(float x) {
    return tanhf(x);
}

static void apply_softmax(float *data, int n) {
    // 数值稳定的 softmax：先减去最大值
    float max_val = data[0];
    for (int i = 1; i < n; i++) {
        if (data[i] > max_val) max_val = data[i];
    }

    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        data[i] = expf(data[i] - max_val);
        sum += data[i];
    }

    if (sum > 1e-7f) {
        for (int i = 0; i < n; i++) {
            data[i] /= sum;
        }
    }
}

// ============================================================
// 公共接口实现
// ============================================================

void nn_init(nn_t *nn) {
    memset(nn, 0, sizeof(nn_t));
}

bool nn_add_layer(nn_t *nn, int input_size, int output_size,
                  nn_activation_t activation,
                  const float *weights, const float *biases) {
    if (nn->num_layers >= NN_MAX_LAYERS) {
        ESP_LOGE(TAG, "超过最大层数 %d", NN_MAX_LAYERS);
        return false;
    }
    if (output_size > NN_MAX_NEURONS) {
        ESP_LOGE(TAG, "神经元数 %d 超过最大值 %d", output_size, NN_MAX_NEURONS);
        return false;
    }

    nn_layer_t *layer = &nn->layers[nn->num_layers];
    layer->input_size = input_size;
    layer->output_size = output_size;
    layer->activation = activation;
    layer->weights = weights;
    layer->biases = biases;

    nn->num_layers++;
    return true;
}

bool nn_forward(nn_t *nn, const float *input, int input_size,
                nn_result_t *result) {
    if (nn->num_layers == 0) {
        ESP_LOGE(TAG, "网络未初始化");
        return false;
    }

    uint32_t start_us = (uint32_t)esp_timer_get_time();

    // 复制输入到缓冲区0
    int buf_idx = 0;
    int cur_size = input_size;
    if (cur_size > NN_MAX_NEURONS) cur_size = NN_MAX_NEURONS;
    memcpy(nn->buffer[0], input, cur_size * sizeof(float));

    // 逐层前向传播
    for (int l = 0; l < nn->num_layers; l++) {
        const nn_layer_t *layer = &nn->layers[l];
        int in_size = layer->input_size;
        int out_size = layer->output_size;
        int next_buf = 1 - buf_idx;

        // 矩阵乘法：output = weights * input + bias
        // weights 形状：[out_size x in_size]（行主序）
        for (int o = 0; o < out_size; o++) {
            float sum = layer->biases[o];
            const float *row = &layer->weights[o * in_size];
            for (int i = 0; i < in_size; i++) {
                sum += row[i] * nn->buffer[buf_idx][i];
            }

            // 应用激活函数（Softmax 除外，最后统一处理）
            switch (layer->activation) {
            case NN_ACT_RELU:
                nn->buffer[next_buf][o] = act_relu(sum);
                break;
            case NN_ACT_SIGMOID:
                nn->buffer[next_buf][o] = act_sigmoid(sum);
                break;
            case NN_ACT_TANH:
                nn->buffer[next_buf][o] = act_tanh(sum);
                break;
            case NN_ACT_LINEAR:
            case NN_ACT_SOFTMAX:
            default:
                nn->buffer[next_buf][o] = sum;
                break;
            }
        }

        // Softmax 在整层计算完后统一处理
        if (layer->activation == NN_ACT_SOFTMAX) {
            apply_softmax(nn->buffer[next_buf], out_size);
        }

        buf_idx = next_buf;
        cur_size = out_size;
    }

    // 填充结果
    result->output_size = cur_size;
    memcpy(result->outputs, nn->buffer[buf_idx], cur_size * sizeof(float));

    // 找最大值（argmax）
    result->predicted_class = 0;
    result->confidence = result->outputs[0];
    for (int i = 1; i < cur_size; i++) {
        if (result->outputs[i] > result->confidence) {
            result->confidence = result->outputs[i];
            result->predicted_class = i;
        }
    }

    result->inference_us = (uint32_t)esp_timer_get_time() - start_us;
    return true;
}

const char *nn_activation_name(nn_activation_t act) {
    switch (act) {
    case NN_ACT_LINEAR:  return "Linear";
    case NN_ACT_RELU:    return "ReLU";
    case NN_ACT_SIGMOID: return "Sigmoid";
    case NN_ACT_TANH:    return "Tanh";
    case NN_ACT_SOFTMAX: return "Softmax";
    default:             return "Unknown";
    }
}

void nn_print_info(const nn_t *nn) {
    ESP_LOGI(TAG, "神经网络结构：");
    ESP_LOGI(TAG, "  层数：%d", nn->num_layers);

    int total_params = 0;
    for (int l = 0; l < nn->num_layers; l++) {
        const nn_layer_t *layer = &nn->layers[l];
        int params = layer->input_size * layer->output_size + layer->output_size;
        total_params += params;
        ESP_LOGI(TAG, "  Layer %d: Dense(%d -> %d, %s) | 参数: %d",
                 l + 1,
                 layer->input_size,
                 layer->output_size,
                 nn_activation_name(layer->activation),
                 params);
    }
    ESP_LOGI(TAG, "  总参数量：%d（%.1f KB Flash）",
             total_params, total_params * 4.0f / 1024.0f);
}