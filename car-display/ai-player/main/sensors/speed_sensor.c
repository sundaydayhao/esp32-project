/**
 * @file speed_sensor.c
 * @brief 速度传感器实现 (GPS NMEA + 模拟模式)
 */

#include "speed_sensor.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "speed_sensor";

/* 内部状态 */
static speed_data_t s_speed_data = {0};
static SemaphoreHandle_t s_data_mutex = NULL;
static speed_callback_t s_callback = NULL;
static bool s_simulation_mode = false;
static bool s_initialized = false;

/* 行程统计 */
static uint32_t s_trip_start_time = 0;
static float s_speed_sum = 0;
static uint32_t s_speed_samples = 0;
static float s_last_speed = 0;
static int64_t s_last_update_us = 0;

/* ============================================================
 * NMEA 解析
 * ============================================================ */

/**
 * @brief 解析NMEA GPRMC/GNRMC语句获取速度
 * 格式: $GPRMC,HHMMSS.ss,A,LLLL.LL,a,YYYYY.YY,a,x.x,x.x,DDMMYY,x.x,a*hh
 * 字段5: 速度（节）
 */
static bool parse_nmea_rmc(const char *sentence, float *speed_knots)
{
    if (strncmp(sentence, "$GPRMC", 6) != 0 && 
        strncmp(sentence, "$GNRMC", 6) != 0) {
        return false;
    }

    /* 找到第2个逗号后的状态字段 */
    const char *p = sentence;
    int comma_count = 0;
    
    while (*p && comma_count < 2) {
        if (*p == ',') comma_count++;
        p++;
    }
    
    /* 检查状态 A=有效, V=无效 */
    if (*p != 'A') {
        return false;
    }
    
    /* 跳到第5个逗号（速度字段） */
    while (*p && comma_count < 7) {
        if (*p == ',') comma_count++;
        p++;
    }
    
    if (*p == '\0' || *p == ',') {
        return false;
    }
    
    *speed_knots = atof(p);
    return true;
}

/**
 * @brief 解析NMEA GPVTG语句获取速度
 * 格式: $GPVTG,x.x,T,x.x,M,x.x,N,x.x,K,a*hh
 * 字段7: 速度（km/h）
 */
static bool parse_nmea_vtg(const char *sentence, float *speed_kmh)
{
    if (strncmp(sentence, "$GPVTG", 6) != 0 && 
        strncmp(sentence, "$GNVTG", 6) != 0) {
        return false;
    }

    const char *p = sentence;
    int comma_count = 0;
    
    /* 跳到第7个逗号（km/h速度字段） */
    while (*p && comma_count < 7) {
        if (*p == ',') comma_count++;
        p++;
    }
    
    if (*p == '\0' || *p == ',') {
        return false;
    }
    
    *speed_kmh = atof(p);
    return true;
}

/* ============================================================
 * 速度数据更新
 * ============================================================ */

static void update_speed(float new_speed_kmh, bool gps_valid)
{
    int64_t now_us = esp_timer_get_time();
    
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    
    /* 计算加速度 */
    if (s_last_update_us > 0) {
        float dt_s = (now_us - s_last_update_us) / 1e6f;
        if (dt_s > 0 && dt_s < 5.0f) {
            s_speed_data.acceleration = (new_speed_kmh - s_last_speed) / dt_s;
        }
    }
    
    s_speed_data.speed_kmh = new_speed_kmh;
    s_speed_data.speed_ms = new_speed_kmh / 3.6f;
    s_speed_data.gps_valid = gps_valid;
    s_speed_data.is_simulated = s_simulation_mode;
    
    /* 更新最高速度 */
    if (new_speed_kmh > s_speed_data.max_speed_kmh) {
        s_speed_data.max_speed_kmh = new_speed_kmh;
    }
    
    /* 更新平均速度 */
    if (new_speed_kmh > 0) {
        s_speed_sum += new_speed_kmh;
        s_speed_samples++;
        s_speed_data.avg_speed_kmh = s_speed_sum / s_speed_samples;
    }
    
    /* 更新里程 */
    if (s_last_update_us > 0 && new_speed_kmh > 0) {
        float dt_s = (now_us - s_last_update_us) / 1e6f;
        s_speed_data.distance_m += (uint32_t)(s_speed_data.speed_ms * dt_s);
    }
    
    s_last_speed = new_speed_kmh;
    s_last_update_us = now_us;
    
    speed_data_t data_copy = s_speed_data;
    xSemaphoreGive(s_data_mutex);
    
    /* 触发回调 */
    if (s_callback) {
        s_callback(&data_copy);
    }
}

/* ============================================================
 * GPS 读取任务
 * ============================================================ */

static void gps_task(void *arg)
{
    uint8_t *buf = malloc(GPS_UART_BUF_SIZE);
    char sentence[128];
    int sentence_len = 0;
    
    ESP_LOGI(TAG, "GPS读取任务启动");
    
    while (1) {
        int len = uart_read_bytes(GPS_UART_NUM, buf, GPS_UART_BUF_SIZE - 1,
                                   pdMS_TO_TICKS(100));
        if (len > 0) {
            buf[len] = '\0';
            
            /* 逐字符解析NMEA语句 */
            for (int i = 0; i < len; i++) {
                char c = buf[i];
                
                if (c == '$') {
                    sentence_len = 0;
                    sentence[sentence_len++] = c;
                } else if (c == '\n' || c == '\r') {
                    if (sentence_len > 6) {
                        sentence[sentence_len] = '\0';
                        
                        float speed_val = 0;
                        
                        /* 尝试解析VTG（直接km/h） */
                        if (parse_nmea_vtg(sentence, &speed_val)) {
                            update_speed(speed_val, true);
                        }
                        /* 尝试解析RMC（节转km/h） */
                        else if (parse_nmea_rmc(sentence, &speed_val)) {
                            update_speed(speed_val * 1.852f, true);
                        }
                    }
                    sentence_len = 0;
                } else if (sentence_len < 127) {
                    sentence[sentence_len++] = c;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    free(buf);
    vTaskDelete(NULL);
}

/* ============================================================
 * 模拟速度任务
 * ============================================================ */

static void simulation_task(void *arg)
{
    float sim_speed = 0;
    float sim_target = 60;
    int sim_phase = 0;
    uint32_t phase_timer = 0;
    
    ESP_LOGI(TAG, "速度模拟任务启动");
    
    while (1) {
        /* 模拟真实驾驶场景 */
        phase_timer++;
        
        switch (sim_phase) {
            case 0: /* 起步加速 0->60 */
                sim_target = 60;
                if (sim_speed < sim_target) {
                    sim_speed += 2.0f;
                } else {
                    sim_phase = 1;
                    phase_timer = 0;
                }
                break;
                
            case 1: /* 匀速行驶 */
                sim_speed = 60 + (float)(rand() % 10 - 5) * 0.5f;
                if (phase_timer > 100) {
                    sim_phase = 2;
                    phase_timer = 0;
                }
                break;
                
            case 2: /* 加速超车 60->100 */
                sim_target = 100;
                if (sim_speed < sim_target) {
                    sim_speed += 3.0f;
                } else {
                    sim_phase = 3;
                    phase_timer = 0;
                }
                break;
                
            case 3: /* 高速行驶 */
                sim_speed = 100 + (float)(rand() % 10 - 5) * 0.5f;
                if (phase_timer > 80) {
                    sim_phase = 4;
                    phase_timer = 0;
                }
                break;
                
            case 4: /* 减速 100->40 */
                sim_target = 40;
                if (sim_speed > sim_target) {
                    sim_speed -= 2.5f;
                } else {
                    sim_phase = 5;
                    phase_timer = 0;
                }
                break;
                
            case 5: /* 堵车低速 */
                sim_speed = 20 + (float)(rand() % 20);
                if (phase_timer > 60) {
                    sim_phase = 6;
                    phase_timer = 0;
                }
                break;
                
            case 6: /* 停车 */
                if (sim_speed > 0) {
                    sim_speed -= 3.0f;
                    if (sim_speed < 0) sim_speed = 0;
                } else {
                    if (phase_timer > 30) {
                        sim_phase = 0;
                        phase_timer = 0;
                    }
                }
                break;
        }
        
        if (sim_speed < 0) sim_speed = 0;
        
        if (s_simulation_mode) {
            update_speed(sim_speed, false);
        }
        
        vTaskDelay(pdMS_TO_TICKS(SPEED_UPDATE_INTERVAL_MS));
    }
    
    vTaskDelete(NULL);
}

/* ============================================================
 * 公共接口实现
 * ============================================================ */

esp_err_t speed_sensor_init(void)
{
    ESP_LOGI(TAG, "初始化速度传感器");
    
    s_data_mutex = xSemaphoreCreateMutex();
    if (s_data_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    memset(&s_speed_data, 0, sizeof(s_speed_data));
    s_trip_start_time = xTaskGetTickCount();
    
    /* 初始化GPS UART */
    uart_config_t uart_config = {
        .baud_rate = GPS_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t ret = uart_driver_install(GPS_UART_NUM, GPS_UART_BUF_SIZE * 2, 
                                         0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "GPS UART安装失败，启用模拟模式");
        s_simulation_mode = true;
    } else {
        uart_param_config(GPS_UART_NUM, &uart_config);
        uart_set_pin(GPS_UART_NUM, GPS_UART_TX_PIN, GPS_UART_RX_PIN,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        
        /* 启动GPS读取任务 */
        xTaskCreate(gps_task, "gps_task", 4096, NULL, 5, NULL);
        ESP_LOGI(TAG, "GPS UART初始化完成 (TX:%d, RX:%d, %dbps)",
                 GPS_UART_TX_PIN, GPS_UART_RX_PIN, GPS_UART_BAUD_RATE);
    }
    
    /* 始终启动模拟任务（GPS无效时使用） */
    s_simulation_mode = true;  // 默认使用模拟模式，GPS连接后自动切换
    xTaskCreate(simulation_task, "sim_speed", 2048, NULL, 4, NULL);
    
    s_initialized = true;
    ESP_LOGI(TAG, "速度传感器初始化完成 (模拟模式: %s)", 
             s_simulation_mode ? "开启" : "关闭");
    return ESP_OK;
}

void speed_sensor_register_callback(speed_callback_t cb)
{
    s_callback = cb;
}

void speed_sensor_get_data(speed_data_t *data)
{
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    *data = s_speed_data;
    xSemaphoreGive(s_data_mutex);
}

void speed_sensor_set_simulation(bool enable)
{
    s_simulation_mode = enable;
    ESP_LOGI(TAG, "模拟模式: %s", enable ? "开启" : "关闭");
}

void speed_sensor_reset_trip(void)
{
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    s_speed_data.max_speed_kmh = 0;
    s_speed_data.avg_speed_kmh = 0;
    s_speed_data.distance_m = 0;
    s_speed_sum = 0;
    s_speed_samples = 0;
    s_trip_start_time = xTaskGetTickCount();
    xSemaphoreGive(s_data_mutex);
    ESP_LOGI(TAG, "行程数据已重置");
}