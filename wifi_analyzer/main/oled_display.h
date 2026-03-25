#pragma once

#include "esp_err.h"
#include "wifi_scanner.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Hardware configuration                                             */
/* ------------------------------------------------------------------ */
#define OLED_I2C_PORT       0
#define OLED_SDA_GPIO       8   /* ESP32-S3 default SDA pin */
#define OLED_SCL_GPIO       9   /* ESP32-S3 default SCL pin; GPIO22 is invalid on S3 */
#define OLED_I2C_FREQ_HZ    400000
#define OLED_I2C_ADDR       0x3C

#define OLED_WIDTH          128
#define OLED_HEIGHT         64
#define OLED_PAGES          (OLED_HEIGHT / 8)

esp_err_t oled_display_init(void);
void oled_display_update(const wifi_scan_result_t *result);
void oled_display_next_page(void);
void oled_display_show_boot(const char *ap_ip);

#ifdef __cplusplus
}
#endif
