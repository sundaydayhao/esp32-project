#pragma once

// ============================================================
// 项目信息
// ============================================================
#define PROJECT_NAME        "A型桌面菜单屏"
#define PROJECT_VERSION     "1.0.0"
#define FIRMWARE_VERSION    "v1.0.0"

// ============================================================
// LCD 引脚定义（ILI9488 7寸 800×480 SPI屏）
// ============================================================
#define LCD_PIN_MOSI        11      // SPI 数据输出
#define LCD_PIN_SCLK        12      // SPI 时钟
#define LCD_PIN_CS          10      // 片选（低有效）
#define LCD_PIN_DC          13      // 数据/命令选择
#define LCD_PIN_RST         14      // 复位（低有效）
#define LCD_PIN_BL          15      // 背光控制（PWM）

#define LCD_SPI_HOST        SPI2_HOST
#define LCD_SPI_FREQ_HZ     (40 * 1000 * 1000)  // 40MHz SPI 时钟
#define LCD_WIDTH           800
#define LCD_HEIGHT          480

// ============================================================
// 显示布局参数
// ============================================================
#define HEADER_HEIGHT       70      // 标题栏高度（px）
#define FOOTER_HEIGHT       50      // 促销栏高度（px）
#define MENU_AREA_HEIGHT    (LCD_HEIGHT - HEADER_HEIGHT - FOOTER_HEIGHT)  // 360px
#define MENU_COLS           2       // 菜单列数
#define MENU_ROWS           4       // 菜单行数（每列）
#define MAX_MENU_ITEMS      (MENU_COLS * MENU_ROWS * 2)  // 最多16项

// ============================================================
// 颜色主题（RGB565 格式）
// ============================================================
// 主题0：红色（中国风）
#define THEME0_HEADER_BG    0xA000  // 深红
#define THEME0_HEADER_FG    0xFFFF  // 白
#define THEME0_MENU_BG      0xFFF0  // 米白
#define THEME0_MENU_FG      0x2000  // 深红
#define THEME0_PRICE_FG     0xF800  // 红
#define THEME0_FOOTER_BG    0xF800  // 红
#define THEME0_FOOTER_FG    0xFFFF  // 白

// 主题1：蓝色（清爽）
#define THEME1_HEADER_BG    0x001F  // 深蓝
#define THEME1_HEADER_FG    0xFFFF
#define THEME1_MENU_BG      0xF7FF  // 浅蓝白
#define THEME1_MENU_FG      0x000F  // 深蓝
#define THEME1_PRICE_FG     0x001F  // 蓝
#define THEME1_FOOTER_BG    0x001F
#define THEME1_FOOTER_FG    0xFFFF

// 主题2：绿色（健康）
#define THEME2_HEADER_BG    0x0400  // 深绿
#define THEME2_HEADER_FG    0xFFFF
#define THEME2_MENU_BG      0xF7F0  // 浅绿白
#define THEME2_MENU_FG      0x0200  // 深绿
#define THEME2_PRICE_FG     0x07E0  // 绿
#define THEME2_FOOTER_BG    0x0400
#define THEME2_FOOTER_FG    0xFFFF

// 主题3：橙色（活力）
#define THEME3_HEADER_BG    0xFB40  // 深橙
#define THEME3_HEADER_FG    0xFFFF
#define THEME3_MENU_BG      0xFFF8  // 浅橙白
#define THEME3_MENU_FG      0x8200  // 深橙
#define THEME3_PRICE_FG     0xFD20  // 橙
#define THEME3_FOOTER_BG    0xFB40
#define THEME3_FOOTER_FG    0xFFFF

// ============================================================
// WiFi 配置
// ============================================================
#define WIFI_AP_SSID_PREFIX "MenuDisplay-"
#define WIFI_AP_PASSWORD    "12345678"
#define WIFI_AP_CHANNEL     6
#define WIFI_AP_MAX_CONN    4
#define WIFI_AP_IP          "192.168.4.1"

#define WIFI_STA_MAX_RETRY  5
#define WIFI_CONNECT_TIMEOUT_MS  15000

// ============================================================
// Web 服务器配置
// ============================================================
#define WEB_SERVER_PORT     80
#define WEB_MAX_OPEN_SOCKETS 7

// ============================================================
// NVS 存储键名
// ============================================================
#define NVS_NAMESPACE       "menu_app"
#define NVS_KEY_WIFI_SSID   "wifi_ssid"
#define NVS_KEY_WIFI_PASS   "wifi_pass"
#define NVS_KEY_CONTENT     "content"
#define NVS_KEY_THEME       "theme"

// ============================================================
// 内容限制
// ============================================================
#define MAX_RESTAURANT_NAME_LEN  32
#define MAX_ITEM_NAME_LEN        20
#define MAX_ITEM_PRICE_LEN       8
#define MAX_PROMO_TEXT_LEN       80
#define MAX_WIFI_SSID_LEN        32
#define MAX_WIFI_PASS_LEN        64

// ============================================================
// 任务配置
// ============================================================
#define DISPLAY_TASK_STACK_SIZE  8192
#define DISPLAY_TASK_PRIORITY    5
#define DISPLAY_TASK_CORE        1      // 显示任务固定在 Core 1

#define WEB_TASK_STACK_SIZE      8192
#define WEB_TASK_PRIORITY        4

// ============================================================
// 显示刷新
// ============================================================
#define DISPLAY_REFRESH_MS       1000   // 时钟每秒刷新
#define PROMO_SCROLL_SPEED_MS    50     // 促销文字滚动速度