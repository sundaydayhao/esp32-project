/**
 * @file main.c
 * @brief 卡尔曼滤波器示例 - 传感器数据融合与噪声滤除
 *
 * 本示例演示卡尔曼滤波器在 ESP32 传感器数据处理中的应用。
 * 卡尔曼滤波器是一种最优线性估计算法，广泛用于：
 *   - 传感器噪声滤除（温度、加速度计、陀螺仪）
 *   - 多传感器数据融合（IMU 姿态估计）
 *   - 目标跟踪与预测
 *   - 机器人定位（SLAM）
 *
 * 本示例实现：
 *   1. 一维卡尔曼滤波器（温度传感器去噪）
 *   2. 互补滤波器（加速度计+陀螺仪姿态融合）
 *   3. 与简单移动平均的性能对比
 *
 * 卡尔曼滤波器原理：
 *   预测步骤：
 *     x̂⁻ₖ = A·x̂ₖ₋₁ + B·uₖ      （状态预测）
 *     P⁻ₖ  = A·Pₖ₋₁·Aᵀ + Q       （误差协方差预测）
 *   更新步骤：
 *     Kₖ   = P⁻ₖ·Hᵀ·(H·P⁻ₖ·Hᵀ + R)⁻¹  （卡尔曼增益）
 *     x̂ₖ  = x̂⁻ₖ + Kₖ·(zₖ - H·x̂⁻ₖ)   （状态更新）
 *     Pₖ   = (I - Kₖ·H)·P⁻ₖ             （误差协方差更新）
 *
 * 硬件要求：无（使用模拟数据）
 * 串口输出：115200 baud
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "KalmanFilter";

// ============================================================
// 一维卡尔曼滤波器
// ============================================================

/**
 * @brief 一维卡尔曼滤波器状态
 *
 * 适用于标量状态估计（如温度、距离、电压）
 */
typedef struct {
    float x;    ///< 状态估计值（当前最优估计）
    float p;    ///< 估计误差协方差
    float q;    ///< 过程噪声协方差（系统不确定性）
    float r;    ///< 测量噪声协方差（传感器噪声）
    float k;    ///< 卡尔曼增益（上次计算值，用于调试）
} kalman1d_t;

/**
 * @brief 初始化一维卡尔曼滤波器
 * @param kf 滤波器指针
 * @param initial_value 初始状态估计值
 * @param q 过程噪声（越大，越信任测量值；越小，越信任预测值）
 * @param r 测量噪声（传感器噪声方差，可通过静态测量估计）
 */
static void kalman1d_init(kalman1d_t *kf, float initial_value,
                           float q, float r) {
    kf->x = initial_value;
    kf->p = 1.0f;   // 初始误差协方差（不确定性较大）
    kf->q = q;
    kf->r = r;
    kf->k = 0.0f;
}

/**
 * @brief 一维卡尔曼滤波器更新（预测+更新合并）
 *
 * 对于匀速/静态系统（A=1, H=1, B=0），简化为：
 *   预测：x̂⁻ = x̂，P⁻ = P + Q
 *   更新：K = P⁻/(P⁻ + R)，x̂ = x̂⁻ + K*(z - x̂⁻)，P = (1-K)*P⁻
 *
 * @param kf 滤波器指针
 * @param measurement 新的测量值
 * @return 滤波后的状态估计值
 */
static float kalman1d_update(kalman1d_t *kf, float measurement) {
    // 预测步骤（假设状态不变：A=1）
    float p_pred = kf->p + kf->q;

    // 更新步骤
    kf->k = p_pred / (p_pred + kf->r);                    // 卡尔曼增益
    kf->x = kf->x + kf->k * (measurement - kf->x);        // 状态更新
    kf->p = (1.0f - kf->k) * p_pred;                      // 协方差更新

    return kf->x;
}

// ============================================================
// 互补滤波器（加速度计 + 陀螺仪姿态融合）
// ============================================================

/**
 * @brief 互补滤波器状态（用于姿态估计）
 *
 * 互补滤波器是卡尔曼滤波器的简化版本：
 *   - 陀螺仪：短期精确，长期漂移（高频信号）
 *   - 加速度计：长期稳定，短期噪声大（低频信号）
 *   - 互补滤波：angle = α*(angle + gyro*dt) + (1-α)*accel_angle
 */
typedef struct {
    float angle;    ///< 当前角度估计（度）
    float alpha;    ///< 互补系数（0.9~0.99，越大越信任陀螺仪）
    float dt;       ///< 采样时间间隔（秒）
} complementary_filter_t;

static void comp_filter_init(complementary_filter_t *cf,
                              float initial_angle, float alpha, float dt) {
    cf->angle = initial_angle;
    cf->alpha = alpha;
    cf->dt = dt;
}

/**
 * @brief 互补滤波器更新
 * @param cf 滤波器指针
 * @param gyro_rate 陀螺仪角速度（度/秒）
 * @param accel_angle 加速度计计算的角度（度）
 * @return 融合后的角度估计
 */
static float comp_filter_update(complementary_filter_t *cf,
                                 float gyro_rate, float accel_angle) {
    // 陀螺仪积分
    float gyro_angle = cf->angle + gyro_rate * cf->dt;

    // 互补融合
    cf->angle = cf->alpha * gyro_angle + (1.0f - cf->alpha) * accel_angle;

    return cf->angle;
}

// ============================================================
// 移动平均滤波器（用于对比）
// ============================================================

#define MA_SIZE 8

typedef struct {
    float buffer[MA_SIZE];
    int head;
    float sum;
    int count;
} moving_avg_t;

static void ma_init(moving_avg_t *ma) {
    memset(ma, 0, sizeof(moving_avg_t));
}

static float ma_update(moving_avg_t *ma, float value) {
    if (ma->count < MA_SIZE) {
        ma->count++;
    } else {
        ma->sum -= ma->buffer[ma->head];
    }
    ma->buffer[ma->head] = value;
    ma->head = (ma->head + 1) % MA_SIZE;
    ma->sum += value;
    return ma->sum / ma->count;
}

// ============================================================
// 模拟数据生成
// ============================================================

static uint32_t rng_state = 98765;

static float rng_randf(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)(rng_state >> 16) / 65535.0f - 0.5f;  // [-0.5, 0.5]
}

static float gaussian_randf(float std) {
    // Box-Muller 变换
    float u1 = (float)(rng_state >> 16) / 65535.0f + 1e-7f;
    rng_state = rng_state * 1664525u + 1013904223u;
    float u2 = (float)(rng_state >> 16) / 65535.0f;
    rng_state = rng_state * 1664525u + 1013904223u;
    return std * sqrtf(-2.0f * logf(u1)) * cosf(2.0f * (float)M_PI * u2);
}

// ============================================================
// 演示1：温度传感器去噪
// ============================================================

static void demo_temperature_denoising(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 演示1：温度传感器卡尔曼去噪 ===");
    ESP_LOGI(TAG, "真实温度：25°C（缓慢变化）");
    ESP_LOGI(TAG, "传感器噪声：±2°C 高斯噪声");
    ESP_LOGI(TAG, "");

    // 初始化滤波器
    kalman1d_t kf;
    kalman1d_init(&kf,
                  25.0f,   // 初始估计值
                  0.01f,   // 过程噪声 Q（温度变化缓慢）
                  4.0f);   // 测量噪声 R（传感器方差 = 2°C² ≈ 4）

    moving_avg_t ma;
    ma_init(&ma);

    float true_temp = 25.0f;
    float total_raw_err = 0.0f;
    float total_kf_err = 0.0f;
    float total_ma_err = 0.0f;
    const int n = 50;

    ESP_LOGI(TAG, "  步骤 | 真实值 | 测量值 | 卡尔曼 | 移动均值 | 增益K");
    ESP_LOGI(TAG, "  -----|--------|--------|--------|----------|------");

    for (int i = 0; i < n; i++) {
        // 真实温度缓慢变化（模拟环境温度）
        true_temp += 0.05f * sinf(2.0f * (float)M_PI * i / 30.0f);

        // 模拟带噪声的传感器读数
        float measured = true_temp + gaussian_randf(2.0f);

        // 卡尔曼滤波
        float kf_est = kalman1d_update(&kf, measured);

        // 移动平均
        float ma_est = ma_update(&ma, measured);

        // 计算误差
        total_raw_err += fabsf(measured - true_temp);
        total_kf_err  += fabsf(kf_est - true_temp);
        total_ma_err  += fabsf(ma_est - true_temp);

        if (i % 5 == 0) {
            ESP_LOGI(TAG, "  %4d | %6.2f | %6.2f | %6.2f | %8.2f | %.3f",
                     i, true_temp, measured, kf_est, ma_est, kf.k);
        }
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "滤波效果对比（平均绝对误差 MAE）：");
    ESP_LOGI(TAG, "  原始测量值：  %.3f °C", total_raw_err / n);
    ESP_LOGI(TAG, "  卡尔曼滤波：  %.3f °C  (降噪 %.0f%%)",
             total_kf_err / n,
             100.0f * (1.0f - (total_kf_err / total_raw_err)));
    ESP_LOGI(TAG, "  移动平均：    %.3f °C  (降噪 %.0f%%)",
             total_ma_err / n,
             100.0f * (1.0f - (total_ma_err / total_raw_err)));
}

// ============================================================
// 演示2：IMU 姿态融合（互补滤波器）
// ============================================================

static void demo_imu_fusion(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 演示2：IMU 姿态融合（互补滤波器）===");
    ESP_LOGI(TAG, "模拟：加速度计 + 陀螺仪 -> 俯仰角估计");
    ESP_LOGI(TAG, "");

    const float dt = 0.01f;  // 100Hz 采样率

    // 互补滤波器（α=0.98：98%信任陀螺仪，2%信任加速度计）
    complementary_filter_t cf;
    comp_filter_init(&cf, 0.0f, 0.98f, dt);

    // 卡尔曼滤波器（用于对比）
    kalman1d_t kf;
    kalman1d_init(&kf, 0.0f, 0.001f, 0.1f);

    float true_angle = 0.0f;
    float gyro_drift = 0.05f;  // 陀螺仪漂移（度/秒）

    ESP_LOGI(TAG, "  时间(s) | 真实角度 | 加速度计 | 陀螺仪积分 | 互补滤波 | 卡尔曼");
    ESP_LOGI(TAG, "  --------|----------|----------|------------|----------|-------");

    float gyro_integrated = 0.0f;

    for (int i = 0; i < 200; i++) {
        float t = i * dt;

        // 真实角度：正弦波运动（模拟设备倾斜）
        true_angle = 30.0f * sinf(2.0f * (float)M_PI * t / 2.0f);

        // 真实角速度
        float true_rate = 30.0f * 2.0f * (float)M_PI / 2.0f
                          * cosf(2.0f * (float)M_PI * t / 2.0f);

        // 模拟陀螺仪（有漂移和噪声）
        float gyro_rate = true_rate + gyro_drift + gaussian_randf(0.5f);

        // 模拟加速度计角度（有噪声，但无漂移）
        float accel_angle = true_angle + gaussian_randf(3.0f);

        // 陀螺仪积分（无滤波，会漂移）
        gyro_integrated += gyro_rate * dt;

        // 互补滤波
        float cf_angle = comp_filter_update(&cf, gyro_rate, accel_angle);

        // 卡尔曼滤波（仅用加速度计）
        float kf_angle = kalman1d_update(&kf, accel_angle);

        if (i % 20 == 0) {
            ESP_LOGI(TAG, "  %7.2f | %8.2f | %8.2f | %10.2f | %8.2f | %6.2f",
                     t, true_angle, accel_angle, gyro_integrated,
                     cf_angle, kf_angle);
        }
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "结论：");
    ESP_LOGI(TAG, "  - 陀螺仪积分：短期精确，长期漂移严重");
    ESP_LOGI(TAG, "  - 加速度计：长期稳定，短期噪声大");
    ESP_LOGI(TAG, "  - 互补滤波：结合两者优点，实时性好");
    ESP_LOGI(TAG, "  - 卡尔曼滤波：最优估计，但计算量稍大");
}

// ============================================================
// 演示3：卡尔曼增益的直觉理解
// ============================================================

static void demo_kalman_gain_intuition(void) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 演示3：卡尔曼增益直觉理解 ===");
    ESP_LOGI(TAG, "展示不同 Q/R 参数对滤波效果的影响");
    ESP_LOGI(TAG, "");

    // 测试不同参数配置
    typedef struct {
        const char *desc;
        float q;
        float r;
    } config_t;

    const config_t configs[] = {
        {"Q小R大（信任预测）", 0.001f, 10.0f},
        {"Q=R（平衡）",        1.0f,   1.0f},
        {"Q大R小（信任测量）", 10.0f,  0.001f},
    };

    // 生成测试数据：阶跃信号（从0跳变到10）
    const int n = 30;
    float measurements[30];
    for (int i = 0; i < n; i++) {
        float true_val = (i < 10) ? 0.0f : 10.0f;
        measurements[i] = true_val + gaussian_randf(1.0f);
    }

    for (int c = 0; c < 3; c++) {
        kalman1d_t kf;
        kalman1d_init(&kf, 0.0f, configs[c].q, configs[c].r);

        ESP_LOGI(TAG, "配置：%s (Q=%.3f, R=%.3f)",
                 configs[c].desc, configs[c].q, configs[c].r);

        float total_err = 0.0f;
        for (int i = 0; i < n; i++) {
            float true_val = (i < 10) ? 0.0f : 10.0f;
            float est = kalman1d_update(&kf, measurements[i]);
            total_err += fabsf(est - true_val);

            // 简单可视化
            int bar = (int)(est * 3);
            if (bar < 0) bar = 0;
            if (bar > 30) bar = 30;
            char vis[32] = {0};
            for (int j = 0; j < bar && j < 31; j++) vis[j] = '#';
            if (i % 5 == 0) {
                ESP_LOGI(TAG, "  [%2d] est=%.2f K=%.3f |%s",
                         i, est, kf.k, vis);
            }
        }
        ESP_LOGI(TAG, "  MAE=%.3f", total_err / n);
        ESP_LOGI(TAG, "");
    }

    ESP_LOGI(TAG, "参数调优指南：");
    ESP_LOGI(TAG, "  Q（过程噪声）：系统状态变化的不确定性");
    ESP_LOGI(TAG, "    Q 小 -> 相信模型预测，滤波平滑但响应慢");
    ESP_LOGI(TAG, "    Q 大 -> 相信测量值，响应快但噪声大");
    ESP_LOGI(TAG, "  R（测量噪声）：传感器测量的不确定性");
    ESP_LOGI(TAG, "    R 小 -> 相信传感器，跟踪快");
    ESP_LOGI(TAG, "    R 大 -> 不信任传感器，滤波强");
    ESP_LOGI(TAG, "  实践：R 可通过传感器静态测量估计（方差）");
    ESP_LOGI(TAG, "        Q 需要根据系统动态特性调整");
}

// ============================================================
// 主任务
// ============================================================

static void kalman_filter_task(void *pvParameters) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  卡尔曼滤波器示例                        ║");
    ESP_LOGI(TAG, "║  传感器去噪 & 多传感器数据融合           ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "卡尔曼滤波器是 AI/ML 数据预处理的重要工具");
    ESP_LOGI(TAG, "广泛用于：机器人、无人机、自动驾驶、IoT");

    // 演示1：温度去噪
    demo_temperature_denoising();
    vTaskDelay(pdMS_TO_TICKS(500));

    // 演示2：IMU 姿态融合
    demo_imu_fusion();
    vTaskDelay(pdMS_TO_TICKS(500));

    // 演示3：参数调优
    demo_kalman_gain_intuition();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "示例完成！");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "实际应用建议：");
    ESP_LOGI(TAG, "  1. MPU6050 + 卡尔曼 -> 精确姿态角");
    ESP_LOGI(TAG, "  2. DHT11/BMP280 + 卡尔曼 -> 平滑温度曲线");
    ESP_LOGI(TAG, "  3. HC-SR04 + 卡尔曼 -> 稳定距离测量");
    ESP_LOGI(TAG, "  4. ADC + 卡尔曼 -> 消除 ADC 量化噪声");
    ESP_LOGI(TAG, "  5. 卡尔曼输出 -> 神经网络输入特征");

    vTaskDelete(NULL);
}

void app_main(void) {
    xTaskCreate(kalman_filter_task, "kalman_task", 8192, NULL, 5, NULL);
}