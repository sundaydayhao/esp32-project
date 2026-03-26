/**
 * motor_control.cpp
 * 舵机电机控制模块实现 / Motor Control Implementation
 *
 * 使用 ESP-IDF LEDC 外设生成 50Hz PWM 信号控制舵机
 *
 * 舵机 PWM 计算:
 *   频率: 50Hz → 周期 20ms = 20000us
 *   分辨率: 14bit → 最大计数 16383
 *   0度:   500us  → duty = 500/20000 * 16383 = 409
 *   90度:  1500us → duty = 1500/20000 * 16383 = 1228
 *   180度: 2500us → duty = 2500/20000 * 16383 = 2047
 */

#include "motor_control.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char* TAG = "MOTOR";

// ============================================================
// LEDC 通道分配 / LEDC Channel Assignment
// ============================================================
typedef struct {
    int             gpio;       // GPIO 引脚
    ledc_channel_t  channel;    // LEDC 通道
    int             cur_angle;  // 当前角度
    const char*     name;       // 名称
} servo_t;

static servo_t s_servos[] = {
    { SERVO_HAZARDOUS_GPIO,   LEDC_CHANNEL_0, SERVO_ANGLE_CLOSED, "有害垃圾挡板" },
    { SERVO_RECYCLABLE_GPIO,  LEDC_CHANNEL_1, SERVO_ANGLE_CLOSED, "可回收物挡板" },
    { SERVO_KITCHEN_GPIO,     LEDC_CHANNEL_2, SERVO_ANGLE_CLOSED, "厨余垃圾挡板" },
    { SERVO_OTHER_GPIO,       LEDC_CHANNEL_3, SERVO_ANGLE_CLOSED, "其他垃圾挡板" },
    { SERVO_PLATFORM_GPIO,    LEDC_CHANNEL_4, SERVO_PLATFORM_HOME,"传送平台"     },
};

#define SERVO_COUNT  (sizeof(s_servos) / sizeof(s_servos[0]))

// 平台舵机索引
#define SERVO_IDX_HAZARDOUS   0
#define SERVO_IDX_RECYCLABLE  1
#define SERVO_IDX_KITCHEN     2
#define SERVO_IDX_OTHER       3
#define SERVO_IDX_PLATFORM    4

static bool s_initialized = false;

// ============================================================
// 内部函数 / Internal Functions
// ============================================================

/**
 * 角度转 LEDC duty 值
 * duty = (pulse_us / period_us) * (2^resolution - 1)
 */
static uint32_t angle_to_duty(int angle)
{
    // 限制角度范围
    if (angle < 0)   angle = 0;
    if (angle > 180) angle = 180;

    // 线性插值: 0度=500us, 180度=2500us
    uint32_t pulse_us = SERVO_MIN_PULSE_US +
                        (uint32_t)((SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * angle / 180);

    // 14bit 分辨率: 2^14 - 1 = 16383
    uint32_t duty = (pulse_us * 16383) / SERVO_PERIOD_US;
    return duty;
}

/**
 * 设置 LEDC 通道 duty
 */
static void ledc_set_duty_angle(ledc_channel_t channel, int angle)
{
    uint32_t duty = angle_to_duty(angle);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

/**
 * 查找 GPIO 对应的舵机索引
 */
static int find_servo_by_gpio(int gpio_num)
{
    for (int i = 0; i < (int)SERVO_COUNT; i++) {
        if (s_servos[i].gpio == gpio_num) return i;
    }
    return -1;
}

// ============================================================
// 公共接口实现 / Public Interface Implementation
// ============================================================

esp_err_t motor_init(void)
{
    if (s_initialized) return ESP_OK;

    // 配置 LEDC 定时器 (50Hz, 14bit)
    ledc_timer_config_t timer_conf = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_14_BIT,
        .timer_num       = LEDC_TIMER_1,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };

    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC 定时器配置失败: %d", ret);
        return ret;
    }

    // 配置每个舵机的 LEDC 通道
    for (int i = 0; i < (int)SERVO_COUNT; i++) {
        ledc_channel_config_t ch_conf = {
            .gpio_num   = s_servos[i].gpio,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel    = s_servos[i].channel,
            .intr_type  = LEDC_INTR_DISABLE,
            .timer_sel  = LEDC_TIMER_1,
            .duty       = angle_to_duty(s_servos[i].cur_angle),
            .hpoint     = 0,
        };

        ret = ledc_channel_config(&ch_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "舵机 %s (GPIO%d) 配置失败: %d",
                     s_servos[i].name, s_servos[i].gpio, ret);
            return ret;
        }

        ESP_LOGI(TAG, "  舵机 %-12s GPIO%-2d 通道%d 初始角度%d°",
                 s_servos[i].name, s_servos[i].gpio,
                 s_servos[i].channel, s_servos[i].cur_angle);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "舵机初始化完成 (%d个)", (int)SERVO_COUNT);
    return ESP_OK;
}

void motor_set_angle_immediate(int gpio_num, int angle)
{
    if (!s_initialized) return;

    int idx = find_servo_by_gpio(gpio_num);
    if (idx < 0) {
        ESP_LOGW(TAG, "未找到 GPIO%d 对应的舵机", gpio_num);
        return;
    }

    ledc_set_duty_angle(s_servos[idx].channel, angle);
    s_servos[idx].cur_angle = angle;
}

void motor_set_angle(int gpio_num, int angle)
{
    if (!s_initialized) return;

    int idx = find_servo_by_gpio(gpio_num);
    if (idx < 0) {
        ESP_LOGW(TAG, "未找到 GPIO%d 对应的舵机", gpio_num);
        return;
    }

    int from = s_servos[idx].cur_angle;
    int to   = angle;

    if (from == to) return;

    int step = (to > from) ? 1 : -1;

    ESP_LOGD(TAG, "舵机 %s: %d° -> %d°", s_servos[idx].name, from, to);

    for (int a = from; a != to; a += step) {
        ledc_set_duty_angle(s_servos[idx].channel, a);
        vTaskDelay(pdMS_TO_TICKS(12));  // 每度 12ms，平滑移动
    }
    ledc_set_duty_angle(s_servos[idx].channel, to);
    s_servos[idx].cur_angle = to;
}

void motor_reset_all(void)
{
    if (!s_initialized) return;

    ESP_LOGI(TAG, "所有舵机复位...");

    // 先关闭所有挡板
    motor_set_angle_immediate(SERVO_HAZARDOUS_GPIO,  SERVO_ANGLE_CLOSED);
    motor_set_angle_immediate(SERVO_RECYCLABLE_GPIO, SERVO_ANGLE_CLOSED);
    motor_set_angle_immediate(SERVO_KITCHEN_GPIO,    SERVO_ANGLE_CLOSED);
    motor_set_angle_immediate(SERVO_OTHER_GPIO,      SERVO_ANGLE_CLOSED);
    vTaskDelay(pdMS_TO_TICKS(300));

    // 平台回到初始位置
    motor_set_angle(SERVO_PLATFORM_GPIO, SERVO_PLATFORM_HOME);

    ESP_LOGI(TAG, "舵机复位完成");
}

void motor_sort_garbage(garbage_type_t type)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "电机未初始化，跳过分拣");
        return;
    }

    int platform_angle = SERVO_PLATFORM_HOME;
    int gate_gpio      = -1;
    const char* bin_name = "未知";

    switch (type) {
        case GARBAGE_HAZARDOUS:
            platform_angle = SERVO_PLATFORM_HAZARDOUS;
            gate_gpio      = SERVO_HAZARDOUS_GPIO;
            bin_name       = "有害垃圾桶";
            break;

        case GARBAGE_RECYCLABLE:
            platform_angle = SERVO_PLATFORM_RECYCLABLE;
            gate_gpio      = SERVO_RECYCLABLE_GPIO;
            bin_name       = "可回收物桶";
            break;

        case GARBAGE_KITCHEN:
            platform_angle = SERVO_PLATFORM_KITCHEN;
            gate_gpio      = SERVO_KITCHEN_GPIO;
            bin_name       = "厨余垃圾桶";
            break;

        case GARBAGE_OTHER:
            platform_angle = SERVO_PLATFORM_OTHER;
            gate_gpio      = SERVO_OTHER_GPIO;
            bin_name       = "其他垃圾桶";
            break;

        default:
            ESP_LOGW(TAG, "未知垃圾类型，不执行分拣");
            return;
    }

    ESP_LOGI(TAG, "分拣: 投入 %s (平台角度: %d°)", bin_name, platform_angle);

    // 步骤1: 旋转平台到目标方向
    motor_set_angle(SERVO_PLATFORM_GPIO, platform_angle);
    vTaskDelay(pdMS_TO_TICKS(300));

    // 步骤2: 打开对应垃圾桶挡板
    if (gate_gpio >= 0) {
        ESP_LOGI(TAG, "打开 %s 挡板", bin_name);
        motor_set_angle(gate_gpio, SERVO_ANGLE_OPEN);

        // 保持打开，等待垃圾落入
        vTaskDelay(pdMS_TO_TICKS(SORTING_DURATION_MS));

        // 关闭挡板
        motor_set_angle(gate_gpio, SERVO_ANGLE_CLOSED);
        ESP_LOGI(TAG, "关闭 %s 挡板", bin_name);
    }

    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "分拣完成");
}