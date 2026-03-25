/**
 * @file anomaly_detector.c
 * @brief 基于统计方法的传感器数据异常检测实现
 *
 * 实现了以下异常检测算法：
 * 1. Z-Score 方法：基于均值和标准差
 * 2. IQR 方法：基于四分位距（对异常值更鲁棒）
 * 3. 移动平均偏差：检测趋势变化
 */

#include "anomaly_detector.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================
// 内部辅助函数
// ============================================================

/**
 * @brief 比较函数（用于 qsort）
 */
static int compare_float(const void *a, const void *b) {
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

/**
 * @brief 计算数组的均值
 */
static float calc_mean(const float *data, int n) {
    if (n == 0) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += data[i];
    }
    return sum / (float)n;
}

/**
 * @brief 计算数组的方差
 */
static float calc_variance(const float *data, int n, float mean) {
    if (n < 2) return 0.0f;
    float sum_sq = 0.0f;
    for (int i = 0; i < n; i++) {
        float diff = data[i] - mean;
        sum_sq += diff * diff;
    }
    return sum_sq / (float)(n - 1);  // 样本方差（贝塞尔校正）
}

// ============================================================
// 公共接口实现
// ============================================================

void anomaly_detector_init(anomaly_detector_t *detector) {
    memset(detector, 0, sizeof(anomaly_detector_t));
}

bool anomaly_detector_update(anomaly_detector_t *detector,
                              float value,
                              anomaly_result_t *result) {
    // 初始化结果
    result->is_anomaly = false;
    result->value = value;
    result->zscore = 0.0f;
    result->method = "Z-Score";

    // 更新移动平均
    float old_ma_val = detector->ma_buffer[detector->ma_head];
    detector->ma_buffer[detector->ma_head] = value;
    detector->ma_head = (detector->ma_head + 1) % ANOMALY_MA_WINDOW;
    detector->ma_sum -= old_ma_val;
    detector->ma_sum += value;

    // 更新循环窗口缓冲区
    int old_idx = detector->window_head;
    float old_val = detector->window[old_idx];

    // 更新运行统计量（Welford 在线算法的简化版）
    if (detector->window_count == ANOMALY_WINDOW_SIZE) {
        // 窗口已满，移除最旧的值
        detector->running_sum -= old_val;
        detector->running_sum_sq -= old_val * old_val;
    } else {
        detector->window_count++;
    }

    // 添加新值
    detector->window[old_idx] = value;
    detector->window_head = (old_idx + 1) % ANOMALY_WINDOW_SIZE;
    detector->running_sum += value;
    detector->running_sum_sq += value * value;
    detector->total_samples++;

    // 需要足够的数据才能检测异常
    if (detector->window_count < ANOMALY_MA_WINDOW) {
        result->mean = value;
        result->std_dev = 0.0f;
        return false;
    }

    // 计算均值和标准差
    float n = (float)detector->window_count;
    float mean = detector->running_sum / n;
    float variance = (detector->running_sum_sq / n) - (mean * mean);
    if (variance < 0.0f) variance = 0.0f;  // 数值稳定性
    float std_dev = sqrtf(variance);

    result->mean = mean;
    result->std_dev = std_dev;

    // Z-Score 异常检测
    if (std_dev > 1e-6f) {
        float zscore = fabsf(value - mean) / std_dev;
        result->zscore = zscore;

        if (zscore > ANOMALY_ZSCORE_THRESHOLD) {
            result->is_anomaly = true;
            result->method = "Z-Score";
            detector->anomaly_count++;
        }
    }

    return result->is_anomaly;
}

void anomaly_detector_get_stats(const anomaly_detector_t *detector,
                                 stats_features_t *features) {
    if (detector->window_count == 0) {
        memset(features, 0, sizeof(stats_features_t));
        return;
    }

    // 复制窗口数据用于排序
    float sorted[ANOMALY_WINDOW_SIZE];
    int n = detector->window_count;

    // 从循环缓冲区中按顺序提取数据
    int start = (detector->window_head - n + ANOMALY_WINDOW_SIZE) % ANOMALY_WINDOW_SIZE;
    for (int i = 0; i < n; i++) {
        sorted[i] = detector->window[(start + i) % ANOMALY_WINDOW_SIZE];
    }

    // 计算基本统计量
    features->mean = calc_mean(sorted, n);
    features->variance = calc_variance(sorted, n, features->mean);
    features->std_dev = sqrtf(features->variance);

    // 排序用于分位数计算
    qsort(sorted, n, sizeof(float), compare_float);

    features->min_val = sorted[0];
    features->max_val = sorted[n - 1];
    features->range = features->max_val - features->min_val;

    // 四分位数
    features->q1 = sorted[n / 4];
    features->q3 = sorted[3 * n / 4];
    features->iqr = features->q3 - features->q1;
}

float anomaly_detector_get_ma(const anomaly_detector_t *detector) {
    int count = (detector->total_samples < ANOMALY_MA_WINDOW)
                ? (int)detector->total_samples
                : ANOMALY_MA_WINDOW;
    if (count == 0) return 0.0f;
    return detector->ma_sum / (float)count;
}

void anomaly_detector_reset(anomaly_detector_t *detector) {
    memset(detector, 0, sizeof(anomaly_detector_t));
}

void anomaly_detector_get_info(const anomaly_detector_t *detector,
                                char *buf, int buf_size) {
    float anomaly_rate = 0.0f;
    if (detector->total_samples > 0) {
        anomaly_rate = 100.0f * (float)detector->anomaly_count
                       / (float)detector->total_samples;
    }

    snprintf(buf, buf_size,
             "总样本: %lu | 异常数: %lu | 异常率: %.1f%%",
             (unsigned long)detector->total_samples,
             (unsigned long)detector->anomaly_count,
             anomaly_rate);
}