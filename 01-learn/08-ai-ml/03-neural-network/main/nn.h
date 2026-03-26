#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// 神经网络配置（编译时常量）
// ============================================================

/** 最大层数 */
#define NN_MAX_LAYERS       8

/** 最大每层神经元数 */
#define NN_MAX_NEURONS      32

/** 最大权重数量 */
#define NN_MAX_WEIGHTS      (NN_MAX_NEURONS * NN_MAX_NEURONS * NN_MAX_LAYERS)

/** 最大偏置数量 */
#define NN_MAX_BIASES       (NN_MAX_NEURONS * NN_MAX_LAYERS)

// ============================================================
// 激活函数类型
// ============================================================

typedef enum {
    NN_ACT_LINEAR = 0,  ///< 线性（无激活）
    NN_ACT_RELU,        ///< ReLU: max(0, x)
    NN_ACT_SIGMOID,     ///< Sigmoid: 1/(1+e^-x)
    NN_ACT_TANH,        ///< Tanh: (e^x - e^-x)/(e^x + e^-x)
    NN_ACT_SOFTMAX,     ///< Softmax（仅用于输出层）
} nn_activation_t;

// ============================================================
// 数据结构
// ============================================================

/**
 * @brief 神经网络层描述
 */
typedef struct {
    int input_size;             ///< 输入维度
    int output_size;            ///< 输出维度（神经元数）
    nn_activation_t activation; ///< 激活函数
    const float *weights;       ///< 权重矩阵 [output_size x input_size]（行主序）
    const float *biases;        ///< 偏置向量 [output_size]
} nn_layer_t;

/**
 * @brief 神经网络结构
 */
typedef struct {
    nn_layer_t layers[NN_MAX_LAYERS];   ///< 层数组
    int num_layers;                      ///< 实际层数
    float buffer[2][NN_MAX_NEURONS];     ///< 推理缓冲区（乒乓缓冲）
} nn_t;

/**
 * @brief 推理结果
 */
typedef struct {
    float outputs[NN_MAX_NEURONS];  ///< 输出值
    int output_size;                 ///< 输出维度
    int predicted_class;             ///< 预测类别（argmax）
    float confidence;                ///< 置信度（最大输出值）
    uint32_t inference_us;           ///< 推理耗时（微秒）
} nn_result_t;

// ============================================================
// 函数声明
// ============================================================

/**
 * @brief 初始化神经网络
 * @param nn 神经网络指针
 */
void nn_init(nn_t *nn);

/**
 * @brief 添加全连接层
 * @param nn 神经网络指针
 * @param input_size 输入维度
 * @param output_size 输出维度
 * @param activation 激活函数
 * @param weights 权重数组（行主序，大小 = output_size * input_size）
 * @param biases 偏置数组（大小 = output_size）
 * @return true 成功
 */
bool nn_add_layer(nn_t *nn, int input_size, int output_size,
                  nn_activation_t activation,
                  const float *weights, const float *biases);

/**
 * @brief 执行前向推理
 * @param nn 神经网络指针
 * @param input 输入数据
 * @param input_size 输入维度
 * @param result 推理结果（输出）
 * @return true 成功
 */
bool nn_forward(nn_t *nn, const float *input, int input_size,
                nn_result_t *result);

/**
 * @brief 激活函数名称字符串
 */
const char *nn_activation_name(nn_activation_t act);

/**
 * @brief 打印网络结构信息
 */
void nn_print_info(const nn_t *nn);

#ifdef __cplusplus
}
#endif