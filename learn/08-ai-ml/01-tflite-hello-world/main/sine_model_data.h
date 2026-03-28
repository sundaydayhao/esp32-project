#pragma once
#include <cstdint>

// 预训练的正弦波预测模型（TFLite Micro 格式）
// 该模型接受 [0, 2π] 范围内的 x 值，输出对应的 sin(x) 预测值
// 模型结构：Dense(16, relu) -> Dense(16, relu) -> Dense(1)
// 训练数据：1000 个均匀分布的 sin(x) 样本

extern const unsigned char g_sine_model_data[];
extern const int g_sine_model_data_len;