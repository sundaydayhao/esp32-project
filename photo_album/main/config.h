/**
 * @file config.h
 * @brief Global project configuration
 */
#pragma once

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/spi_common.h"

#define FW_VERSION_STR      "1.0.0"
#define DEVICE_NAME         "ESP32-S3 Photo Album"

#if !defined(CONFIG_PHOTO_ALBUM_BOARD_WAVESHARE_7) && !defined(CONFIG_PHOTO_ALBUM_BOARD_NT35510)
#define CONFIG_PHOTO_ALBUM_BOARD_WAVESHARE_7 1
#endif

#define LCD_BITS_PER_PIXEL  16

#if CONFIG_PHOTO_ALBUM_BOARD_WAVESHARE_7

#define DISPLAY_BOARD_NAME          "Waveshare ESP32-S3-Touch-LCD-7"
#define LCD_USE_RGB_INTERFACE       1
#define LCD_USE_I80_INTERFACE       0

#define LCD_H_RES                   800
#define LCD_V_RES                   480
#define LCD_HSYNC_BACK_PORCH        8
#define LCD_HSYNC_FRONT_PORCH       8
#define LCD_HSYNC_PULSE_WIDTH       4
#define LCD_VSYNC_BACK_PORCH        16
#define LCD_VSYNC_FRONT_PORCH       16
#define LCD_VSYNC_PULSE_WIDTH       4
#define LCD_PCLK_HZ                 (16 * 1000 * 1000)

#define LCD_PIN_PCLK                GPIO_NUM_7
#define LCD_PIN_VSYNC               GPIO_NUM_3
#define LCD_PIN_HSYNC               GPIO_NUM_46
#define LCD_PIN_DE                  GPIO_NUM_5
#define LCD_PIN_BACKLIGHT           GPIO_NUM_NC

#define LCD_PIN_D0                  GPIO_NUM_14
#define LCD_PIN_D1                  GPIO_NUM_38
#define LCD_PIN_D2                  GPIO_NUM_18
#define LCD_PIN_D3                  GPIO_NUM_17
#define LCD_PIN_D4                  GPIO_NUM_10
#define LCD_PIN_D5                  GPIO_NUM_39
#define LCD_PIN_D6                  GPIO_NUM_0
#define LCD_PIN_D7                  GPIO_NUM_45
#define LCD_PIN_D8                  GPIO_NUM_48
#define LCD_PIN_D9                  GPIO_NUM_47
#define LCD_PIN_D10                 GPIO_NUM_21
#define LCD_PIN_D11                 GPIO_NUM_1
#define LCD_PIN_D12                 GPIO_NUM_2
#define LCD_PIN_D13                 GPIO_NUM_42
#define LCD_PIN_D14                 GPIO_NUM_41
#define LCD_PIN_D15                 GPIO_NUM_40

#define LCD_NUM_FB                  2

#define LCD_BL_LEDC_TIMER           LEDC_TIMER_0
#define LCD_BL_LEDC_CHANNEL         LEDC_CHANNEL_0
#define LCD_BL_LEDC_FREQ            5000
#define LCD_BL_LEDC_DUTY            8192

#define TOUCH_I2C_PORT              I2C_NUM_0
#define TOUCH_I2C_SDA               GPIO_NUM_8
#define TOUCH_I2C_SCL               GPIO_NUM_9
#define TOUCH_I2C_ADDR              0x5D
#define TOUCH_I2C_FREQ              400000
#define TOUCH_RST_PIN               GPIO_NUM_NC
#define TOUCH_INT_PIN               GPIO_NUM_4

#define SD_SPI_HOST                 SPI3_HOST
#define SD_PIN_MOSI                 GPIO_NUM_11
#define SD_PIN_MISO                 GPIO_NUM_13
#define SD_PIN_CLK                  GPIO_NUM_12
#define SD_PIN_CS                   GPIO_NUM_NC
#define SD_SPI_FREQ_KHZ             20000

#define BOARD_HAS_CH422G_CTRL       1
#define BOARD_HAS_CH422G_SD_CS      1
#define BOARD_CH422G_I2C_PORT       I2C_NUM_0
#define BOARD_CH422G_I2C_SDA        GPIO_NUM_8
#define BOARD_CH422G_I2C_SCL        GPIO_NUM_9
#define BOARD_CH422G_I2C_FREQ       400000
#define BOARD_CH422G_PIN_TP_RST     1
#define BOARD_CH422G_PIN_LCD_BL     2
#define BOARD_CH422G_PIN_LCD_RST    3
#define BOARD_CH422G_PIN_SD_CS      4
#define BOARD_CH422G_PIN_USB_SEL    5

#elif CONFIG_PHOTO_ALBUM_BOARD_NT35510

#define DISPLAY_BOARD_NAME          "Generic NT35510 I80 Board"
#define LCD_USE_RGB_INTERFACE       0
#define LCD_USE_I80_INTERFACE       1

#define LCD_H_RES                   CONFIG_PHOTO_ALBUM_NT35510_H_RES
#define LCD_V_RES                   CONFIG_PHOTO_ALBUM_NT35510_V_RES
#define LCD_NUM_FB                  2

#define LCD_I80_PIXEL_CLOCK_HZ      CONFIG_PHOTO_ALBUM_NT35510_PIXEL_CLOCK_HZ
#define LCD_I80_BUS_WIDTH           CONFIG_PHOTO_ALBUM_NT35510_I80_BUS_WIDTH
#define LCD_I80_DRAW_BUF_LINES      CONFIG_PHOTO_ALBUM_NT35510_DRAW_BUF_LINES
#define LCD_I80_CMD_BITS            16
#define LCD_I80_PARAM_BITS          16

#define LCD_I80_PIN_WR              ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_WR)
#define LCD_I80_PIN_CS              ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_CS)
#define LCD_I80_PIN_DC              ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_DC)
#define LCD_I80_PIN_RST             ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_RST)
#define LCD_I80_PIN_BACKLIGHT       ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_BK_LIGHT)
#define LCD_I80_PIN_POWER           ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_POWER)

#define LCD_I80_PIN_D0              ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D0)
#define LCD_I80_PIN_D1              ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D1)
#define LCD_I80_PIN_D2              ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D2)
#define LCD_I80_PIN_D3              ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D3)
#define LCD_I80_PIN_D4              ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D4)
#define LCD_I80_PIN_D5              ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D5)
#define LCD_I80_PIN_D6              ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D6)
#define LCD_I80_PIN_D7              ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D7)

#if CONFIG_PHOTO_ALBUM_NT35510_I80_BUS_WIDTH == 16
#define LCD_I80_PIN_D8              ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D8)
#define LCD_I80_PIN_D9              ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D9)
#define LCD_I80_PIN_D10             ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D10)
#define LCD_I80_PIN_D11             ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D11)
#define LCD_I80_PIN_D12             ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D12)
#define LCD_I80_PIN_D13             ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D13)
#define LCD_I80_PIN_D14             ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D14)
#define LCD_I80_PIN_D15             ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_D15)
#else
#define LCD_I80_PIN_D8              GPIO_NUM_NC
#define LCD_I80_PIN_D9              GPIO_NUM_NC
#define LCD_I80_PIN_D10             GPIO_NUM_NC
#define LCD_I80_PIN_D11             GPIO_NUM_NC
#define LCD_I80_PIN_D12             GPIO_NUM_NC
#define LCD_I80_PIN_D13             GPIO_NUM_NC
#define LCD_I80_PIN_D14             GPIO_NUM_NC
#define LCD_I80_PIN_D15             GPIO_NUM_NC
#endif

#define SD_SPI_HOST                 SPI3_HOST
#define SD_PIN_MOSI                 ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_SD_MOSI)
#define SD_PIN_MISO                 ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_SD_MISO)
#define SD_PIN_CLK                  ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_SD_CLK)
#define SD_PIN_CS                   ((gpio_num_t)CONFIG_PHOTO_ALBUM_NT35510_PIN_SD_CS)
#define SD_SPI_FREQ_KHZ             CONFIG_PHOTO_ALBUM_NT35510_SD_SPI_FREQ_KHZ

#define BOARD_HAS_CH422G_CTRL       0
#define BOARD_HAS_CH422G_SD_CS      0

#else
#error "Unsupported photo album board selection"
#endif

#define SD_MOUNT_POINT      "/sdcard"
#define SD_MAX_FILES        8

#define PHOTO_EXTS          { ".jpg", ".jpeg", ".JPG", ".JPEG", ".bmp", ".BMP" }
#define PHOTO_EXT_COUNT     6
#define PHOTO_SCAN_PATH     "/sdcard"
#define PHOTO_MAX_COUNT     2048
#define PHOTO_PATH_MAX_LEN  256

#define SLIDESHOW_INTERVAL_MS_DEFAULT   5000
#define SLIDESHOW_INTERVAL_MS_MIN       1000
#define SLIDESHOW_INTERVAL_MS_MAX       60000

#define TRANSITION_DURATION_MS          500
#define TRANSITION_STEPS                25

typedef enum {
    SCALE_FIT = 0,
    SCALE_FILL = 1,
    SCALE_STRETCH = 2,
} scale_mode_t;

#define SCALE_MODE_DEFAULT  SCALE_FIT

#define NVS_NAMESPACE       "photo_album"
#define NVS_KEY_INTERVAL    "interval"
#define NVS_KEY_BRIGHTNESS  "brightness"
#define NVS_KEY_SCALE_MODE  "scale_mode"
#define NVS_KEY_LAST_INDEX  "last_idx"
#define NVS_KEY_ORDER       "order"

#define TAG_MAIN        "MAIN"
#define TAG_LCD         "LCD"
#define TAG_SD          "SD"
#define TAG_SCANNER     "SCANNER"
#define TAG_JPEG        "JPEG"
#define TAG_BMP         "BMP"
#define TAG_SLIDESHOW   "SLIDESHOW"

#define TASK_STACK_SLIDESHOW    (8 * 1024)
#define TASK_STACK_DECODE       (16 * 1024)
#define TASK_PRIO_SLIDESHOW     5
#define TASK_PRIO_DECODE        6
