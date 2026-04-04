/**
 * @file lcd12864.h
 * @brief LCD12864 (ST7920) 8位并行接口驱动头文件
 *
 * 接口：8位并行（Intel 时序）
 * 控制器：ST7920
 * 分辨率：128 x 64 单色点阵
 * 电源：5V（ESP32 3.3V 输出可直接驱动 5V TTL 输入）
 *
 * 引脚连接（默认，可在 Kconfig 中修改）：
 *   ESP32 GPIO32 -> LCD D0
 *   ESP32 GPIO13 -> LCD D1
 *   ESP32 GPIO14 -> LCD D2
 *   ESP32 GPIO15 -> LCD D3
 *   ESP32 GPIO16 -> LCD D4
 *   ESP32 GPIO17 -> LCD D5
 *   ESP32 GPIO18 -> LCD D6
 *   ESP32 GPIO19 -> LCD D7
 *   ESP32 GPIO21 -> LCD RS  (0=命令, 1=数据)
 *   ESP32 GPIO22 -> LCD E   (使能，下降沿写入)
 *   ESP32 GPIO4  -> LCD RST (低电平复位)
 *   GND          -> LCD RW  (始终接GND=写模式，防止5V反向驱动ESP32)
 *   VCC(5V)      -> LCD PSB (接高电平=并行模式)
 *   VCC(5V)      -> LCD VCC
 *   GND          -> LCD GND
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
 * 引脚定义（优先使用 Kconfig，否则使用默认值）
 * ---------------------------------------------------------------- */
#ifdef CONFIG_LCD_PIN_D0
#define LCD_PIN_D0   CONFIG_LCD_PIN_D0
#else
#define LCD_PIN_D0   32
#endif
#ifdef CONFIG_LCD_PIN_D1
#define LCD_PIN_D1   CONFIG_LCD_PIN_D1
#else
#define LCD_PIN_D1   13
#endif
#ifdef CONFIG_LCD_PIN_D2
#define LCD_PIN_D2   CONFIG_LCD_PIN_D2
#else
#define LCD_PIN_D2   14
#endif
#ifdef CONFIG_LCD_PIN_D3
#define LCD_PIN_D3   CONFIG_LCD_PIN_D3
#else
#define LCD_PIN_D3   15
#endif
#ifdef CONFIG_LCD_PIN_D4
#define LCD_PIN_D4   CONFIG_LCD_PIN_D4
#else
#define LCD_PIN_D4   16
#endif
#ifdef CONFIG_LCD_PIN_D5
#define LCD_PIN_D5   CONFIG_LCD_PIN_D5
#else
#define LCD_PIN_D5   17
#endif
#ifdef CONFIG_LCD_PIN_D6
#define LCD_PIN_D6   CONFIG_LCD_PIN_D6
#else
#define LCD_PIN_D6   18
#endif
#ifdef CONFIG_LCD_PIN_D7
#define LCD_PIN_D7   CONFIG_LCD_PIN_D7
#else
#define LCD_PIN_D7   19
#endif
#ifdef CONFIG_LCD_PIN_RS
#define LCD_PIN_RS   CONFIG_LCD_PIN_RS
#else
#define LCD_PIN_RS   21
#endif
#ifdef CONFIG_LCD_PIN_E
#define LCD_PIN_E    CONFIG_LCD_PIN_E
#else
#define LCD_PIN_E    22
#endif
#ifdef CONFIG_LCD_PIN_RST
#define LCD_PIN_RST  CONFIG_LCD_PIN_RST
#else
#define LCD_PIN_RST  4
#endif

/* ----------------------------------------------------------------
 * 屏幕尺寸
 * ---------------------------------------------------------------- */
#define LCD_WIDTH       128
#define LCD_HEIGHT      64

/* ----------------------------------------------------------------
 * 文本模式 DDRAM 行首地址（ST7920）
 * 4行，每行 8 个汉字（16 字节）
 * 行1=0x80, 行2=0x90, 行3=0x88, 行4=0x98
 * ---------------------------------------------------------------- */
#define LCD_LINE1_ADDR  0x80
#define LCD_LINE2_ADDR  0x90
#define LCD_LINE3_ADDR  0x88
#define LCD_LINE4_ADDR  0x98

/* ----------------------------------------------------------------
 * API
 * ---------------------------------------------------------------- */

/**
 * @brief 初始化 LCD12864（8位并行模式）
 * @return ESP_OK 成功
 */
esp_err_t lcd12864_init(void);

/**
 * @brief 清屏
 */
void lcd12864_clear(void);

/**
 * @brief 设置文本模式光标位置
 * @param row  行号 0~3
 * @param col  列号 0~7（每列对应一个汉字宽度，即 2 字节）
 */
void lcd12864_set_cursor(uint8_t row, uint8_t col);

/**
 * @brief 在当前光标位置写入字符串（支持 GB2312 中文）
 * @param str  以 '\0' 结尾的字符串
 */
void lcd12864_print(const char *str);

/**
 * @brief 在指定行列写入字符串（一步到位）
 * @param row  行号 0~3
 * @param col  列号 0~7
 * @param str  字符串
 */
void lcd12864_print_at(uint8_t row, uint8_t col, const char *str);

/**
 * @brief 开启/关闭显示
 * @param on  true=开启，false=关闭
 */
void lcd12864_display_on(bool on);

/**
 * @brief 进入图形模式（GDRAM）
 */
void lcd12864_enter_graphic_mode(void);

/**
 * @brief 进入文本模式（DDRAM，默认）
 */
void lcd12864_enter_text_mode(void);

/**
 * @brief 在图形模式下绘制一个像素
 * @param x  列 0~127
 * @param y  行 0~63
 * @param on true=点亮，false=熄灭
 */
void lcd12864_draw_pixel(uint8_t x, uint8_t y, bool on);

/**
 * @brief 刷新整个图形缓冲区到屏幕（图形模式）
 * @param buf  128*64/8 = 1024 字节的位图缓冲区
 */
void lcd12864_flush_bitmap(const uint8_t *buf);

#ifdef __cplusplus
}
#endif
