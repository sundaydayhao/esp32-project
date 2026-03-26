/**
 * @file locker.c
 * @brief 格口管理模块实现
 */

#include "locker.h"
#include "config.h"
#include "storage.h"
#include "buzzer.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "locker";

// ============================================================
// 内部状态
// ============================================================

static locker_info_t s_lockers[LOCKER_COUNT];
static SemaphoreHandle_t s_mutex = NULL;

// ============================================================
// 舵机控制
// ============================================================

/**
 * @brief 将微秒脉宽转换为 LEDC duty 值
 */
static uint32_t servo_us_to_duty(uint32_t us)
{
    // 14-bit resolution, 50Hz: period = 20ms = 20000us
    // duty = (us / 20000) * 2^14
    return (uint32_t)((uint64_t)us * ((1 << 14) - 1) / 20000);
}

/**
 * @brief 初始化舵机 LEDC
 */
static esp_err_t servo_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode      = SERVO_LEDC_MODE,
        .timer_num       = SERVO_LEDC_TIMER,
        .duty_resolution = SERVO_RESOLUTION,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) return ret;

    for (int i = 0; i < LOCKER_COUNT; i++) {
        ledc_channel_config_t ch_conf = {
            .speed_mode = SERVO_LEDC_MODE,
            .channel    = (ledc_channel_t)i,
            .timer_sel  = SERVO_LEDC_TIMER,
            .intr_type  = LEDC_INTR_DISABLE,
            .gpio_num   = SERVO_GPIOS[i],
            .duty       = servo_us_to_duty(SERVO_LOCKED_US),
            .hpoint     = 0,
        };
        ret = ledc_channel_config(&ch_conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "舵机%d LEDC配置失败", i);
            return ret;
        }
    }
    ESP_LOGI(TAG, "舵机初始化完成");
    return ESP_OK;
}

/**
 * @brief 设置舵机位置
 * @param id     格口编号
 * @param locked true=锁定, false=解锁
 */
static void servo_set(uint8_t id, bool locked)
{
    if (id >= LOCKER_COUNT) return;
    uint32_t us = locked ? SERVO_LOCKED_US : SERVO_UNLOCKED_US;
    uint32_t duty = servo_us_to_duty(us);
    ledc_set_duty(SERVO_LEDC_MODE, (ledc_channel_t)id, duty);
    ledc_update_duty(SERVO_LEDC_MODE, (ledc_channel_t)id);
    ESP_LOGD(TAG, "格口%d 舵机 -> %s", id, locked ? "锁定" : "解锁");
}

// ============================================================
// LED 控制
// ============================================================

static esp_err_t led_init(void)
{
    gpio_config_t io_conf = {
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    for (int i = 0; i < LOCKER_COUNT; i++) {
        io_conf.pin_bit_mask = (1ULL << LED_GPIOS[i]);
        gpio_config(&io_conf);
        gpio_set_level(LED_GPIOS[i], 0);
    }
    return ESP_OK;
}

static void led_set(uint8_t id, bool on)
{
    if (id >= LOCKER_COUNT) return;
    gpio_set_level(LED_GPIOS[id], on ? 1 : 0);
}

// ============================================================
// 门磁传感器
// ============================================================

static esp_err_t door_sensor_init(void)
{
    gpio_config_t io_conf = {
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    for (int i = 0; i < LOCKER_COUNT; i++) {
        io_conf.pin_bit_mask = (1ULL << DOOR_GPIOS[i]);
        gpio_config(&io_conf);
    }
    return ESP_OK;
}

bool locker_is_door_closed(uint8_t id)
{
    if (id >= LOCKER_COUNT) return true;
    // 低电平 = 门关闭 (磁铁靠近传感器)
    return (gpio_get_level(DOOR_GPIOS[id]) == 0);
}

// ============================================================
// 取件码生成
// ============================================================

static void generate_code(char *code)
{
    for (int i = 0; i < CODE_LENGTH; i++) {
        code[i] = '0' + (rand() % 10);
    }
    code[CODE_LENGTH] = '\0';
}

// ============================================================
// 初始化
// ============================================================

esp_err_t locker_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "创建互斥锁失败");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(servo_init());
    ESP_ERROR_CHECK(led_init());
    ESP_ERROR_CHECK(door_sensor_init());

    // 从 NVS 恢复格口状态
    for (int i = 0; i < LOCKER_COUNT; i++) {
        memset(&s_lockers[i], 0, sizeof(locker_info_t));
        s_lockers[i].id = i;

        uint8_t state = 0;
        storage_load_state(i, &state);
        s_lockers[i].state = (locker_state_t)state;

        if (state == LOCKER_STATE_OCCUPIED) {
            storage_load_code(i, s_lockers[i].code, sizeof(s_lockers[i].code));
            storage_load_timestamp(i, &s_lockers[i].deposit_time);
            storage_load_fail_count(i, &s_lockers[i].fail_count);
        }

        servo_set(i, true); // 上电默认锁定
        led_set(i, state == LOCKER_STATE_OCCUPIED);

        ESP_LOGI(TAG, "格口%d 状态: %d, 取件码: %s",
                 i, state,
                 state == LOCKER_STATE_OCCUPIED ? s_lockers[i].code : "N/A");
    }

    ESP_LOGI(TAG, "格口管理模块初始化完成");
    return ESP_OK;
}

// ============================================================
// 格口管理任务 (200ms 轮询)
// ============================================================

void locker_task(void *arg)
{
    ESP_LOGI(TAG, "格口管理任务启动");

    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000;

        xSemaphoreTake(s_mutex, portMAX_DELAY);

        for (int i = 0; i < LOCKER_COUNT; i++) {
            locker_info_t *lk = &s_lockers[i];
            bool door_closed = locker_is_door_closed(i);
            lk->door_open = !door_closed;

            switch (lk->state) {
            case LOCKER_STATE_UNLOCKED:
                // 自动关锁超时
                if (lk->unlock_time > 0 &&
                    (now_ms - lk->unlock_time) > (AUTO_LOCK_TIMEOUT_S * 1000LL)) {
                    ESP_LOGW(TAG, "格口%d 解锁超时，自动关锁", i);
                    servo_set(i, true);
                    lk->state = LOCKER_STATE_EMPTY;
                    lk->code[0] = '\0';
                    lk->fail_count = 0;
                    lk->unlock_time = 0;
                    storage_save_state(i, LOCKER_STATE_EMPTY);
                    storage_clear_code(i);
                    led_set(i, false);
                }
                // 门关闭 -> 取件完成
                else if (door_closed && lk->unlock_time > 0 &&
                         (now_ms - lk->unlock_time) > 2000) {
                    ESP_LOGI(TAG, "格口%d 门已关闭，取件完成", i);
                    servo_set(i, true);
                    lk->state = LOCKER_STATE_EMPTY;
                    lk->code[0] = '\0';
                    lk->fail_count = 0;
                    lk->unlock_time = 0;
                    storage_save_state(i, LOCKER_STATE_EMPTY);
                    storage_clear_code(i);
                    storage_save_fail_count(i, 0);
                    led_set(i, false);
                }
                break;

            case LOCKER_STATE_LOCKED_OUT:
                // 锁定期到期
                if (now_ms >= lk->lockout_until) {
                    ESP_LOGI(TAG, "格口%d 锁定期结束", i);
                    lk->state = LOCKER_STATE_OCCUPIED;
                    lk->fail_count = 0;
                    storage_save_state(i, LOCKER_STATE_OCCUPIED);
                    storage_save_fail_count(i, 0);
                }
                break;

            default:
                break;
            }
        }

        xSemaphoreGive(s_mutex);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ============================================================
// 格口操作 API
// ============================================================

locker_result_t locker_get_info(uint8_t id, locker_info_t *info)
{
    if (id >= LOCKER_COUNT || !info) return LOCKER_ERR_INVALID;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(info, &s_lockers[id], sizeof(locker_info_t));
    xSemaphoreGive(s_mutex);

    return LOCKER_OK;
}

locker_result_t locker_deposit(uint8_t id, const char *code,
                                uint8_t *out_id, char *out_code)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // 自动选择空闲格口
    if (id == 0xFF) {
        id = 0xFF;
        for (int i = 0; i < LOCKER_COUNT; i++) {
            if (s_lockers[i].state == LOCKER_STATE_EMPTY) {
                id = i;
                break;
            }
        }
        if (id == 0xFF) {
            xSemaphoreGive(s_mutex);
            ESP_LOGW(TAG, "没有空闲格口");
            return LOCKER_ERR_STATE;
        }
    }

    if (id >= LOCKER_COUNT) {
        xSemaphoreGive(s_mutex);
        return LOCKER_ERR_INVALID;
    }

    if (s_lockers[id].state != LOCKER_STATE_EMPTY) {
        xSemaphoreGive(s_mutex);
        ESP_LOGW(TAG, "格口%d 不是空闲状态", id);
        return LOCKER_ERR_STATE;
    }

    // 生成或使用指定取件码
    if (code && strlen(code) == CODE_LENGTH) {
        strncpy(s_lockers[id].code, code, CODE_LENGTH);
        s_lockers[id].code[CODE_LENGTH] = '\0';
    } else {
        generate_code(s_lockers[id].code);
    }

    s_lockers[id].state = LOCKER_STATE_OCCUPIED;
    s_lockers[id].deposit_time = esp_timer_get_time() / 1000;
    s_lockers[id].fail_count = 0;
    s_lockers[id].unlock_time = 0;

    // 解锁让快递员放件
    servo_set(id, false);
    led_set(id, true);

    storage_save_state(id, LOCKER_STATE_OCCUPIED);
    storage_save_code(id, s_lockers[id].code);
    storage_save_timestamp(id, s_lockers[id].deposit_time);
    storage_save_fail_count(id, 0);

    if (out_id)   *out_id = id;
    if (out_code) strncpy(out_code, s_lockers[id].code, CODE_LENGTH + 1);

    ESP_LOGI(TAG, "格口%d 存件成功，取件码: %s", id, s_lockers[id].code);

    xSemaphoreGive(s_mutex);
    return LOCKER_OK;
}

locker_result_t locker_pickup(const char *code, uint8_t *out_id)
{
    if (!code || strlen(code) != CODE_LENGTH) return LOCKER_ERR_INVALID;

    int64_t now_ms = esp_timer_get_time() / 1000;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // 遍历所有格口查找匹配的取件码
    for (int i = 0; i < LOCKER_COUNT; i++) {
        locker_info_t *lk = &s_lockers[i];

        if (lk->state == LOCKER_STATE_LOCKED_OUT) {
            // 检查是否还在锁定期
            if (now_ms < lk->lockout_until) {
                continue; // 跳过被锁定的格口
            }
        }

        if (lk->state != LOCKER_STATE_OCCUPIED) continue;

        if (strcmp(lk->code, code) == 0) {
            // 取件码匹配
            lk->state = LOCKER_STATE_UNLOCKED;
            lk->unlock_time = now_ms;
            lk->fail_count = 0;

            servo_set(i, false); // 解锁
            led_set(i, false);

            storage_save_state(i, LOCKER_STATE_UNLOCKED);
            storage_save_fail_count(i, 0);

            if (out_id) *out_id = i;

            ESP_LOGI(TAG, "格口%d 取件码验证成功，已解锁", i);
            xSemaphoreGive(s_mutex);
            return LOCKER_OK;
        }
    }

    // 取件码不匹配，增加失败计数
    // 找到对应格口增加失败次数
    for (int i = 0; i < LOCKER_COUNT; i++) {
        locker_info_t *lk = &s_lockers[i];
        if (lk->state == LOCKER_STATE_OCCUPIED) {
            lk->fail_count++;
            storage_save_fail_count(i, lk->fail_count);

            if (lk->fail_count >= MAX_FAILED_ATTEMPTS) {
                lk->state = LOCKER_STATE_LOCKED_OUT;
                lk->lockout_until = now_ms + (LOCKOUT_DURATION_S * 1000LL);
                storage_save_state(i, LOCKER_STATE_LOCKED_OUT);
                ESP_LOGW(TAG, "格口%d 失败次数过多，锁定%d秒", i, LOCKOUT_DURATION_S);
            }
        }
    }

    ESP_LOGW(TAG, "取件码 %s 验证失败", code);
    xSemaphoreGive(s_mutex);
    return LOCKER_ERR_CODE;
}

locker_result_t locker_force_unlock(uint8_t id)
{
    if (id >= LOCKER_COUNT) return LOCKER_ERR_INVALID;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_lockers[id].state = LOCKER_STATE_UNLOCKED;
    s_lockers[id].unlock_time = esp_timer_get_time() / 1000;

    servo_set(id, false);
    led_set(id, false);

    storage_save_state(id, LOCKER_STATE_UNLOCKED);

    ESP_LOGI(TAG, "格口%d 管理员强制解锁", id);
    xSemaphoreGive(s_mutex);
    return LOCKER_OK;
}

locker_result_t locker_force_lock(uint8_t id)
{
    if (id >= LOCKER_COUNT) return LOCKER_ERR_INVALID;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    servo_set(id, true);
    led_set(id, s_lockers[id].state == LOCKER_STATE_OCCUPIED);

    ESP_LOGI(TAG, "格口%d 强制锁定", id);
    xSemaphoreGive(s_mutex);
    return LOCKER_OK;
}

locker_result_t locker_reset(uint8_t id)
{
    if (id >= LOCKER_COUNT) return LOCKER_ERR_INVALID;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_lockers[id].state = LOCKER_STATE_EMPTY;
    s_lockers[id].code[0] = '\0';
    s_lockers[id].fail_count = 0;
    s_lockers[id].deposit_time = 0;
    s_lockers[id].unlock_time = 0;
    s_lockers[id].lockout_until = 0;

    servo_set(id, true);
    led_set(id, false);

    storage_save_state(id, LOCKER_STATE_EMPTY);
    storage_clear_code(id);
    storage_save_fail_count(id, 0);

    ESP_LOGI(TAG, "格口%d 已重置为空闲", id);
    xSemaphoreGive(s_mutex);
    return LOCKER_OK;
}

uint8_t locker_get_empty_count(void)
{
    uint8_t count = 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < LOCKER_COUNT; i++) {
        if (s_lockers[i].state == LOCKER_STATE_EMPTY) count++;
    }
    xSemaphoreGive(s_mutex);
    return count;
}