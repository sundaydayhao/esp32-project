/**
 * @file imu_sensor.c
 * @brief IMU传感器实现 (MPU6050)
 */

#include "imu_sensor.h"
#include "config.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "imu_sensor";

/* MPU6050 寄存器地址 */
#define MPU6050_REG_PWR_MGMT_1      0x6B
#define MPU6050_REG_SMPLRT_DIV      0x19
#define MPU6050_REG_CONFIG          0x1A
#define MPU6050_REG_GYRO_CONFIG     0x1B
#define MPU6050_REG_ACCEL_CONFIG    0x1C
#define MPU6050_REG_ACCEL_XOUT_H    0x3B
#define MPU6050_REG_TEMP_OUT_H      0x41
#define MPU6050_REG_GYRO_XOUT_H     0x43
#define MPU6050_REG_WHO_AM_I        0x75

/* 量程配置 */
#define ACCEL_SCALE_2G              16384.0f  // ±2g
#define GYRO_SCALE_250DPS           131.0f    // ±250°/s

/* 内部状态 */
static imu_raw_data_t s_raw_data = {0};
static driving_behavior_t s_behavior = {0};
static SemaphoreHandle_t s_data_mutex = NULL;
static imu_callback_t s_callback = NULL;
static bool s_imu_available = false;

/* 校准偏移量 */
static float s_accel_offset[3] = {0};
static float s_gyro_offset[3] = {0};

/* ============================================================
 * I2C 读写辅助函数
 * ============================================================ */

static esp_err_t mpu6050_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = {reg, data};
    return i2c_master_write_to_device(I2C_MASTER_NUM, MPU6050_ADDR, buf, 2,
                                       pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}

static esp_err_t mpu6050_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_MASTER_NUM, MPU6050_ADDR,
                                         &reg, 1, data, len,
                                         pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
}

/* ============================================================
 * MPU6050 初始化
 * ============================================================ */

static esp_err_t mpu6050_init(void)
{
    /* 检查设备ID */
    uint8_t who_am_i = 0;
    esp_err_t ret = mpu6050_read_reg(MPU6050_REG_WHO_AM_I, &who_am_i, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MPU6050未检测到 (I2C错误: %s)", esp_err_to_name(ret));
        return ret;
    }

    if (who_am_i != 0x68 && who_am_i != 0x72) {
        ESP_LOGW(TAG, "MPU6050 WHO_AM_I错误: 0x%02X (期望0x68)", who_am_i);
        return ESP_ERR_NOT_FOUND;
    }

    /* 唤醒MPU6050 */
    ESP_ERROR_CHECK(mpu6050_write_reg(MPU6050_REG_PWR_MGMT_1, 0x00));
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 设置采样率 = 1kHz / (1 + 9) = 100Hz */
    ESP_ERROR_CHECK(mpu6050_write_reg(MPU6050_REG_SMPLRT_DIV, 9));

    /* 低通滤波器配置 */
    ESP_ERROR_CHECK(mpu6050_write_reg(MPU6050_REG_CONFIG, 0x03));

    /* 陀螺仪量程: ±250°/s */
    ESP_ERROR_CHECK(mpu6050_write_reg(MPU6050_REG_GYRO_CONFIG, 0x00));

    /* 加速度计量程: ±2g */
    ESP_ERROR_CHECK(mpu6050_write_reg(MPU6050_REG_ACCEL_CONFIG, 0x00));

    ESP_LOGI(TAG, "MPU6050初始化完成 (WHO_AM_I=0x%02X)", who_am_i);
    return ESP_OK;
}

/* ============================================================
 * 读取传感器数据
 * ============================================================ */

static esp_err_t mpu6050_read_all(imu_raw_data_t *raw)
{
    uint8_t buf[14];
    esp_err_t ret = mpu6050_read_reg(MPU6050_REG_ACCEL_XOUT_H, buf, 14);
    if (ret != ESP_OK) return ret;

    /* 解析加速度 (大端序) */
    int16_t ax = (int16_t)((buf[0] << 8) | buf[1]);
    int16_t ay = (int16_t)((buf[2] << 8) | buf[3]);
    int16_t az = (int16_t)((buf[4] << 8) | buf[5]);

    /* 解析温度 */
    int16_t temp_raw = (int16_t)((buf[6] << 8) | buf[7]);

    /* 解析陀螺仪 */
    int16_t gx = (int16_t)((buf[8] << 8) | buf[9]);
    int16_t gy = (int16_t)((buf[10] << 8) | buf[11]);
    int16_t gz = (int16_t)((buf[12] << 8) | buf[13]);

    /* 转换为物理量，减去校准偏移 */
    raw->accel_x = (float)ax / ACCEL_SCALE_2G - s_accel_offset[0];
    raw->accel_y = (float)ay / ACCEL_SCALE_2G - s_accel_offset[1];
    raw->accel_z = (float)az / ACCEL_SCALE_2G - s_accel_offset[2];
    raw->gyro_x  = (float)gx / GYRO_SCALE_250DPS - s_gyro_offset[0];
    raw->gyro_y  = (float)gy / GYRO_SCALE_250DPS - s_gyro_offset[1];
    raw->gyro_z  = (float)gz / GYRO_SCALE_250DPS - s_gyro_offset[2];
    raw->temperature = (float)temp_raw / 340.0f + 36.53f;

    return ESP_OK;
}

/* ============================================================
 * 驾驶行为分析
 * ============================================================ */

static void analyze_driving_behavior(const imu_raw_data_t *raw,
                                      driving_behavior_t *behavior)
{
    /* 转向检测：使用Z轴陀螺仪（偏航角速度） */
    behavior->steering_angle = raw->gyro_z;
    behavior->is_turning_left  = (raw->gyro_z < -TURN_SLIGHT);
    behavior->is_turning_right = (raw->gyro_z >  TURN_SLIGHT);

    /* 横向G力：Y轴加速度 */
    behavior->lateral_g = raw->accel_y;

    /* 纵向G力：X轴加速度（前后方向） */
    behavior->longitudinal_g = raw->accel_x;

    /* 垂直G力：Z轴加速度（去除重力1g） */
    behavior->vertical_g = raw->accel_z - 1.0f;

    /* 急刹车检测：纵向减速度超过阈值 */
    behavior->is_braking_hard = (raw->accel_x < -0.4f);  // 约0.4g

    /* 颠簸检测：垂直加速度变化超过阈值 */
    behavior->is_bumpy = (fabsf(behavior->vertical_g) > 0.3f);

    behavior->imu_valid = true;
}

/* ============================================================
 * IMU 读取任务
 * ============================================================ */

static void imu_task(void *arg)
{
    imu_raw_data_t raw;
    driving_behavior_t behavior;

    ESP_LOGI(TAG, "IMU读取任务启动");

    while (1) {
        if (s_imu_available) {
            esp_err_t ret = mpu6050_read_all(&raw);
            if (ret == ESP_OK) {
                analyze_driving_behavior(&raw, &behavior);

                xSemaphoreTake(s_data_mutex, portMAX_DELAY);
                s_raw_data = raw;
                s_behavior = behavior;
                xSemaphoreGive(s_data_mutex);

                if (s_callback) {
                    s_callback(&behavior);
                }
            } else {
                /* I2C读取失败，标记IMU不可用 */
                ESP_LOGW(TAG, "IMU读取失败: %s", esp_err_to_name(ret));
                xSemaphoreTake(s_data_mutex, portMAX_DELAY);
                s_behavior.imu_valid = false;
                xSemaphoreGive(s_data_mutex);
            }
        } else {
            /* IMU不可用时，生成模拟转向数据 */
            static float sim_angle = 0;
            static int sim_dir = 1;
            static uint32_t sim_timer = 0;

            sim_timer++;
            if (sim_timer > 50) {
                sim_dir = -sim_dir;
                sim_timer = 0;
            }
            sim_angle += sim_dir * 2.0f;
            if (sim_angle > 30) sim_angle = 30;
            if (sim_angle < -30) sim_angle = -30;

            xSemaphoreTake(s_data_mutex, portMAX_DELAY);
            s_behavior.steering_angle = sim_angle;
            s_behavior.is_turning_left  = (sim_angle < -TURN_SLIGHT);
            s_behavior.is_turning_right = (sim_angle >  TURN_SLIGHT);
            s_behavior.lateral_g = sim_angle / 100.0f;
            s_behavior.longitudinal_g = 0;
            s_behavior.vertical_g = 0;
            s_behavior.is_braking_hard = false;
            s_behavior.is_bumpy = false;
            s_behavior.imu_valid = false;
            xSemaphoreGive(s_data_mutex);

            if (s_callback) {
                s_callback(&s_behavior);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(IMU_UPDATE_INTERVAL_MS));
    }

    vTaskDelete(NULL);
}

/* ============================================================
 * 公共接口实现
 * ============================================================ */

esp_err_t imu_sensor_init(void)
{
    ESP_LOGI(TAG, "初始化IMU传感器 (MPU6050)");

    s_data_mutex = xSemaphoreCreateMutex();
    if (s_data_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memset(&s_raw_data, 0, sizeof(s_raw_data));
    memset(&s_behavior, 0, sizeof(s_behavior));

    /* 初始化I2C总线 */
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C参数配置失败");
        goto start_task;
    }

    ret = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C驱动安装失败，使用模拟IMU数据");
        goto start_task;
    }

    /* 初始化MPU6050 */
    ret = mpu6050_init();
    if (ret == ESP_OK) {
        s_imu_available = true;
        ESP_LOGI(TAG, "MPU6050就绪 (SDA:%d, SCL:%d)",
                 I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    } else {
        ESP_LOGW(TAG, "MPU6050不可用，使用模拟转向数据");
    }

start_task:
    /* 启动IMU读取任务 */
    xTaskCreate(imu_task, "imu_task", 3072, NULL, 6, NULL);

    ESP_LOGI(TAG, "IMU传感器初始化完成 (硬件: %s)",
             s_imu_available ? "MPU6050" : "模拟");
    return ESP_OK;
}

void imu_sensor_register_callback(imu_callback_t cb)
{
    s_callback = cb;
}

void imu_sensor_get_behavior(driving_behavior_t *behavior)
{
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    *behavior = s_behavior;
    xSemaphoreGive(s_data_mutex);
}

void imu_sensor_get_raw(imu_raw_data_t *raw)
{
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    *raw = s_raw_data;
    xSemaphoreGive(s_data_mutex);
}

void imu_sensor_calibrate(void)
{
    if (!s_imu_available) return;

    ESP_LOGI(TAG, "开始IMU校准，请保持设备静止...");

    float ax_sum = 0, ay_sum = 0, az_sum = 0;
    float gx_sum = 0, gy_sum = 0, gz_sum = 0;
    const int samples = 100;

    for (int i = 0; i < samples; i++) {
        imu_raw_data_t raw;
        /* 临时清零偏移量进行采样 */
        float saved_offsets[6];
        memcpy(saved_offsets, s_accel_offset, sizeof(s_accel_offset));
        memcpy(saved_offsets + 3, s_gyro_offset, sizeof(s_gyro_offset));
        memset(s_accel_offset, 0, sizeof(s_accel_offset));
        memset(s_gyro_offset, 0, sizeof(s_gyro_offset));

        if (mpu6050_read_all(&raw) == ESP_OK) {
            ax_sum += raw.accel_x;
            ay_sum += raw.accel_y;
            az_sum += raw.accel_z;
            gx_sum += raw.gyro_x;
            gy_sum += raw.gyro_y;
            gz_sum += raw.gyro_z;
        }

        memcpy(s_accel_offset, saved_offsets, sizeof(s_accel_offset));
        memcpy(s_gyro_offset, saved_offsets + 3, sizeof(s_gyro_offset));
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* 计算平均偏移（Z轴加速度应为1g，其余为0） */
    s_accel_offset[0] = ax_sum / samples;
    s_accel_offset[1] = ay_sum / samples;
    s_accel_offset[2] = az_sum / samples - 1.0f;  // 减去重力
    s_gyro_offset[0]  = gx_sum / samples;
    s_gyro_offset[1]  = gy_sum / samples;
    s_gyro_offset[2]  = gz_sum / samples;

    ESP_LOGI(TAG, "校准完成: accel_offset=[%.3f, %.3f, %.3f] gyro_offset=[%.3f, %.3f, %.3f]",
             s_accel_offset[0], s_accel_offset[1], s_accel_offset[2],
             s_gyro_offset[0], s_gyro_offset[1], s_gyro_offset[2]);
}