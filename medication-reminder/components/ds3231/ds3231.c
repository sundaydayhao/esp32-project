/**
 * @file ds3231.c
 * @brief DS3231 高精度实时时钟（RTC）驱动实现
 */

#include "ds3231.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "DS3231";

/* ==================== BCD 转换辅助函数 ==================== */

static inline uint8_t bcd2dec(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

static inline uint8_t dec2bcd(uint8_t dec)
{
    return ((dec / 10) << 4) | (dec % 10);
}

/* ==================== I2C 底层操作 ==================== */

static esp_err_t ds3231_write_reg(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(DS3231_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t ds3231_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(DS3231_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* ==================== 公共接口实现 ==================== */

esp_err_t ds3231_init(int sda_pin, int scl_pin)
{
    ESP_LOGI(TAG, "初始化 DS3231 RTC (SDA=%d, SCL=%d)", sda_pin, scl_pin);

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = DS3231_I2C_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(DS3231_I2C_PORT, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 参数配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(DS3231_I2C_PORT, conf.mode, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "I2C 驱动安装失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 清除 OSF 标志位（振荡器停止标志） */
    uint8_t status;
    ret = ds3231_read_reg(DS3231_REG_STATUS, &status, 1);
    if (ret == ESP_OK) {
        status &= ~0x80; /* 清除 OSF 位 */
        ds3231_write_reg(DS3231_REG_STATUS, &status, 1);
        ESP_LOGI(TAG, "DS3231 初始化成功");
    } else {
        ESP_LOGW(TAG, "DS3231 通信失败，请检查连接: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

esp_err_t ds3231_get_time(struct tm *tm)
{
    uint8_t data[7];
    esp_err_t ret = ds3231_read_reg(DS3231_REG_SECONDS, data, 7);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取时间失败: %s", esp_err_to_name(ret));
        return ret;
    }

    tm->tm_sec  = bcd2dec(data[0] & 0x7F);
    tm->tm_min  = bcd2dec(data[1] & 0x7F);
    tm->tm_hour = bcd2dec(data[2] & 0x3F); /* 24小时制 */
    tm->tm_wday = (data[3] & 0x07) - 1;    /* DS3231: 1=周日, tm: 0=周日 */
    tm->tm_mday = bcd2dec(data[4] & 0x3F);
    tm->tm_mon  = bcd2dec(data[5] & 0x1F) - 1; /* tm: 0-11 */
    tm->tm_year = bcd2dec(data[6]) + 100;       /* tm: 从1900年起 */
    tm->tm_isdst = -1;

    return ESP_OK;
}

esp_err_t ds3231_set_time(const struct tm *tm)
{
    uint8_t data[7];

    data[0] = dec2bcd(tm->tm_sec);
    data[1] = dec2bcd(tm->tm_min);
    data[2] = dec2bcd(tm->tm_hour);
    data[3] = (tm->tm_wday + 1) & 0x07; /* DS3231: 1=周日 */
    data[4] = dec2bcd(tm->tm_mday);
    data[5] = dec2bcd(tm->tm_mon + 1);
    data[6] = dec2bcd(tm->tm_year - 100);

    esp_err_t ret = ds3231_write_reg(DS3231_REG_SECONDS, data, 7);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "写入时间失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "时间已写入 DS3231: %04d-%02d-%02d %02d:%02d:%02d",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
    }
    return ret;
}

esp_err_t ds3231_get_temperature(float *temp)
{
    uint8_t data[2];
    esp_err_t ret = ds3231_read_reg(DS3231_REG_TEMP_MSB, data, 2);
    if (ret != ESP_OK) {
        return ret;
    }

    int8_t msb = (int8_t)data[0];
    uint8_t lsb = data[1] >> 6;
    *temp = msb + lsb * 0.25f;
    return ESP_OK;
}

bool ds3231_is_running(void)
{
    uint8_t status;
    esp_err_t ret = ds3231_read_reg(DS3231_REG_STATUS, &status, 1);
    if (ret != ESP_OK) {
        return false;
    }
    /* OSF 位为 0 表示振荡器正常运行 */
    return (status & 0x80) == 0;
}