/**
 * @file main.c
 * @brief 传感器数据异常检测示例
 *
 * 本示例演示如何在 ESP32 上实现实时传感器数据异常检测。
 * 使用统计学方法（Z-Score）对模拟传感器数据进行在线异常检测。
 *
 * 应用场景：
 *   - 工业设备振动监测（检测异常振动）
 *   - 温度传感器故障检测（检测温度突变）
 *   - 电流/电压监测（检测电气异常）
 *   - 水质监测（检测污染事件）
 *
 * 算法原理：
 *   Z-Score = (x - μ) / σ
 *   其中 μ 为滑动窗口均值，σ 为滑动窗口标准差
 *   当 |Z-Score| > 阈值（默认3.0）时，判定为异常
 *
 * 硬件连接（可选，默认使用模拟数据）：
 *   ADC1_CH6 (GPIO34) - 模拟传感器输入
 *
 * 串口输出：115200 baud
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "anomaly_detector.h"

static const char *TAG = "AnomalyDetect";

// ============================================================
// 模拟传感器数据生成
// ============================================================

/** 模拟数据类型 */
typedef enum {
    DATA_NORMAL = 0,    ///< 正常数据（带高斯噪声的正弦波）
    DATA_SPIKE,         ///< 尖峰异常（突然的大幅跳变）
    DATA_DRIFT,         ///< 漂移异常（缓慢偏移）
    DATA_NOISE,         ///< 噪声异常（高频噪声）
} data_type_t;

/** 简单线性同余随机数生成器（不依赖 rand()） */
static uint32_t lcg_state = 12345;

static float lcg_randf(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return (float)(lcg_state >> 16) / 65535.0f;
}

/** Box-Muller 变换生成高斯随机数 */
static float gaussian_noise(float mean, float std) {
    float u1 = lcg_randf();
    float u2 = lcg_randf();
    if (u1 < 1e-7f) u1 = 1e-7f;
    float z = sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
    return mean + std * z;
}

/**
 * @brief 生成模拟传感器数据
 * @param t 时间步（样本索引）
 * @param type 数据类型
 * @return 模拟传感器值（单位：°C，模拟温度传感器）
 */
static float generate_sensor_data(int t, data_type_t type) {
    // 基础信号：25°C 附近的正弦波（模拟日温差）
    float base = 25.0f + 3.0f * sinf(2.0f * (float)M_PI * t / 50.0f);
    float noise = gaussian_noise(0.0f, 0.3f);  // 0.3°C 标准差噪声

    switch (type) {
    case DATA_NORMAL:
        return base + noise;

    case DATA_SPIKE:
        // 尖峰：突然升高 8-12°C
        return base + noise + 8.0f + lcg_randf() * 4.0f;

    case DATA_DRIFT:
        // 漂移：缓慢偏移（传感器老化）
        return base + noise + (float)(t % 20) * 0.5f;

    case DATA_NOISE:
        // 高噪声：标准差增大 5 倍
        return base + gaussian_noise(0.0f, 1.5f);

    default:
        return base + noise;
    }
}

// ============================================================
// 可视化辅助函数
// ============================================================

/**
 * @brief 在串口绘制简单的 ASCII 折线图
 */
static void print_ascii_chart(float value, float min_val, float max_val,
                               int width, bool is_anomaly) {
    char bar[64] = {0};
    int pos = 0;

    if (max_val > min_val) {
        pos = (int)((value - min_val) / (max_val - min_val) * (width - 1));
        if (pos < 0) pos = 0;
        if (pos >= width) pos = width - 1;
    }

    for (int i = 0; i < width; i++) {
        if (i == pos) {
            bar[i] = is_anomaly ? '!' : '*';
        } else if (i == width / 2) {
            bar[i] = '|';
        } else {
            bar[i] = '.';
        }
    }
    bar[width] = '\0';

    printf("[%s] %.1f%s\n", bar, value, is_anomaly ? " <<ANOMALY>>" : "");
}

// ============================================================
// 测试场景
// ============================================================

/**
 * @brief 场景1：正常数据流测试
 */
static void test_normal_data(anomaly_detector_t *detector) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 场景1：正常传感器数据 ===");
    ESP_LOGI(TAG, "预期：无异常或极少误报");

    anomaly_detector_reset(detector);
    int anomaly_count = 0;

    for (int i = 0; i < 100; i++) {
        float value = generate_sensor_data(i, DATA_NORMAL);
        anomaly_result_t result;
        bool is_anomaly = anomaly_detector_update(detector, value, &result);

        if (is_anomaly) {
            anomaly_count++;
            ESP_LOGW(TAG, "[%3d] 误报! 值=%.2f Z=%.2f", i, value, result.zscore);
        }
    }

    char info[128];
    anomaly_detector_get_info(detector, info, sizeof(info));
    ESP_LOGI(TAG, "结果：%s", info);
    ESP_LOGI(TAG, "误报数：%d（正常应 < 1%%）", anomaly_count);
}

/**
 * @brief 场景2：尖峰异常注入测试
 */
static void test_spike_anomaly(anomaly_detector_t *detector) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 场景2：尖峰异常注入 ===");
    ESP_LOGI(TAG, "在第 30、60、90 个样本注入尖峰异常");

    anomaly_detector_reset(detector);

    const int spike_positions[] = {30, 60, 90};
    const int num_spikes = 3;
    int detected_spikes = 0;

    for (int i = 0; i < 120; i++) {
        bool inject = false;
        for (int j = 0; j < num_spikes; j++) {
            if (i == spike_positions[j]) {
                inject = true;
                break;
            }
        }

        float value = generate_sensor_data(i, inject ? DATA_SPIKE : DATA_NORMAL);
        anomaly_result_t result;
        bool is_anomaly = anomaly_detector_update(detector, value, &result);

        if (inject) {
            if (is_anomaly) {
                detected_spikes++;
                ESP_LOGW(TAG, "[%3d] 检测到注入异常! 值=%.2f Z=%.2f",
                         i, value, result.zscore);
            } else {
                ESP_LOGE(TAG, "[%3d] 漏检! 值=%.2f Z=%.2f",
                         i, value, result.zscore);
            }
        }
    }

    ESP_LOGI(TAG, "检测率：%d/%d (%.0f%%)",
             detected_spikes, num_spikes,
             100.0f * detected_spikes / num_spikes);
}

/**
 * @brief 场景3：实时可视化演示
 */
static void test_realtime_visualization(anomaly_detector_t *detector) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 场景3：实时数据流可视化 ===");
    ESP_LOGI(TAG, "图例：* = 正常点，! = 异常点，| = 中心线");
    ESP_LOGI(TAG, "");

    anomaly_detector_reset(detector);

    for (int i = 0; i < 80; i++) {
        data_type_t dtype;
        if (i >= 20 && i < 25) {
            dtype = DATA_SPIKE;
        } else if (i >= 50 && i < 70) {
            dtype = DATA_DRIFT;
        } else {
            dtype = DATA_NORMAL;
        }

        float value = generate_sensor_data(i, dtype);
        anomaly_result_t result;
        bool is_anomaly = anomaly_detector_update(detector, value, &result);

        print_ascii_chart(value, 15.0f, 40.0f, 40, is_anomaly);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief 场景4：统计特征分析
 */
static void test_statistics_analysis(anomaly_detector_t *detector) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 场景4：统计特征分析 ===");

    anomaly_detector_reset(detector);

    for (int i = 0; i < ANOMALY_WINDOW_SIZE; i++) {
        float value = generate_sensor_data(i, DATA_NORMAL);
        anomaly_result_t result;
        anomaly_detector_update(detector, value, &result);
    }

    stats_features_t stats;
    anomaly_detector_get_stats(detector, &stats);

    ESP_LOGI(TAG, "窗口统计特征（最近 %d 个样本）：", ANOMALY_WINDOW_SIZE);
    ESP_LOGI(TAG, "  均值 (μ):     %.3f °C", stats.mean);
    ESP_LOGI(TAG, "  标准差 (σ):   %.3f °C", stats.std_dev);
    ESP_LOGI(TAG, "  方差 (σ²):    %.3f", stats.variance);
    ESP_LOGI(TAG, "  最小值:       %.3f °C", stats.min_val);
    ESP_LOGI(TAG, "  最大值:       %.3f °C", stats.max_val);
    ESP_LOGI(TAG, "  极差:         %.3f °C", stats.range);
    ESP_LOGI(TAG, "  Q1 (25%%):    %.3f °C", stats.q1);
    ESP_LOGI(TAG, "  Q3 (75%%):    %.3f °C", stats.q3);
    ESP_LOGI(TAG, "  IQR:          %.3f °C", stats.iqr);
    ESP_LOGI(TAG, "  移动平均:     %.3f °C",
             anomaly_detector_get_ma(detector));
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Z-Score 阈值: %.1f", (float)ANOMALY_ZSCORE_THRESHOLD);
    ESP_LOGI(TAG, "正常范围: [%.2f, %.2f] °C",
             stats.mean - ANOMALY_ZSCORE_THRESHOLD * stats.std_dev,
             stats.mean + ANOMALY_ZSCORE_THRESHOLD * stats.std_dev);
}

// ============================================================
// 主任务
// ============================================================

static void anomaly_detection_task(void *pvParameters) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  传感器数据异常检测示例                  ║");
    ESP_LOGI(TAG, "║  算法：Z-Score 滑动窗口统计检测          ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "配置参数：");
    ESP_LOGI(TAG, "  滑动窗口大小: %d 个样本", ANOMALY_WINDOW_SIZE);
    ESP_LOGI(TAG, "  Z-Score 阈值: %.1f", (float)ANOMALY_ZSCORE_THRESHOLD);
    ESP_LOGI(TAG, "  移动平均窗口: %d 个样本", ANOMALY_MA_WINDOW);

    anomaly_detector_t detector;
    anomaly_detector_init(&detector);

    // 运行各测试场景
    test_normal_data(&detector);
    vTaskDelay(pdMS_TO_TICKS(500));

    test_spike_anomaly(&detector);
    vTaskDelay(pdMS_TO_TICKS(500));

    test_statistics_analysis(&detector);
    vTaskDelay(pdMS_TO_TICKS(500));

    test_realtime_visualization(&detector);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "所有测试完成！");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "扩展建议：");
    ESP_LOGI(TAG, "  1. 接入真实 ADC 传感器替换模拟数据");
    ESP_LOGI(TAG, "  2. 调整 ANOMALY_ZSCORE_THRESHOLD 适应不同场景");
    ESP_LOGI(TAG, "  3. 异常触发时通过 MQTT 上报告警");
    ESP_LOGI(TAG, "  4. 结合 NVS 存储历史统计数据");

    vTaskDelete(NULL);
}

void app_main(void) {
    xTaskCreate(anomaly_detection_task, "anomaly_task", 8192, NULL, 5, NULL);
}