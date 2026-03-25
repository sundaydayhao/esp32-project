#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// 异常检测器配置
// ============================================================

/** 滑动窗口大小（用于计算统计特征） */
#define ANOMALY_WINDOW_SIZE     32

/** Z-Score 异常阈值（超过此值判定为异常） */
#define ANOMALY_ZSCORE_THRESHOLD  3.0f

/** IQR 异常阈值倍数 */
#define ANOMALY_IQR_MULTIPLIER    1.5f

/** 移动平均窗口大小 */
#define ANOMALY_MA_WINDOW         8

// ============================================================
// 数据结构
// ============================================================

/**
 * @brief 统计特征结构体
 */
typedef struct {
    float mean;         ///< 均值
    float variance;     ///< 方差
    float std_dev;      ///< 标准差
    float min_val;      ///< 最小值
    float max_val;      ///< 最大值
    float range;        ///< 极差
    float q1;           ///< 第一四分位数
    float q3;           ///< 第三四分位数
    float iqr;          ///< 四分位距
} stats_features_t;

/**
 * @brief 异常检测结果
 */
typedef struct {
    bool is_anomaly;        ///< 是否为异常
    float zscore;           ///< Z-Score 值
    float value;            ///< 当前值
    float mean;             ///< 当前均值
    float std_dev;          ///< 当前标准差
    const char *method;     ///< 检测方法名称
} anomaly_result_t;

/**
 * @brief 异常检测器状态
 */
typedef struct {
    float window[ANOMALY_WINDOW_SIZE];  ///< 数据窗口
    int window_count;                    ///< 当前窗口数据量
    int window_head;                     ///< 窗口头指针（循环缓冲区）
    float running_sum;                   ///< 运行总和（快速均值计算）
    float running_sum_sq;                ///< 运行平方和（快速方差计算）
    uint32_t total_samples;              ///< 总样本数
    uint32_t anomaly_count;              ///< 异常计数
    float ma_buffer[ANOMALY_MA_WINDOW];  ///< 移动平均缓冲区
    int ma_head;                         ///< 移动平均头指针
    float ma_sum;                        ///< 移动平均总和
} anomaly_detector_t;

// ============================================================
// 函数声明
// ============================================================

/**
 * @brief 初始化异常检测器
 * @param detector 检测器指针
 */
void anomaly_detector_init(anomaly_detector_t *detector);

/**
 * @brief 向检测器添加新数据点并检测异常
 * @param detector 检测器指针
 * @param value 新数据值
 * @param result 检测结果（输出）
 * @return true 如果检测到异常
 */
bool anomaly_detector_update(anomaly_detector_t *detector,
                              float value,
                              anomaly_result_t *result);

/**
 * @brief 计算窗口内的统计特征
 * @param detector 检测器指针
 * @param features 统计特征（输出）
 */
void anomaly_detector_get_stats(const anomaly_detector_t *detector,
                                 stats_features_t *features);

/**
 * @brief 获取移动平均值
 * @param detector 检测器指针
 * @return 移动平均值
 */
float anomaly_detector_get_ma(const anomaly_detector_t *detector);

/**
 * @brief 重置检测器状态
 * @param detector 检测器指针
 */
void anomaly_detector_reset(anomaly_detector_t *detector);

/**
 * @brief 获取检测器统计信息字符串
 * @param detector 检测器指针
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 */
void anomaly_detector_get_info(const anomaly_detector_t *detector,
                                char *buf, int buf_size);

#ifdef __cplusplus
}
#endif