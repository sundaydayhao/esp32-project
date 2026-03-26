/**
 * app_config.h
 * 全局配置与类型定义 / Global Configuration & Type Definitions
 *
 * AI 智能垃圾分类系统 - ESP32-S3 (ESP-IDF)
 *
 * 硬件连接 / Hardware Connections:
 *
 *  摄像头 OV2640 (SCCB + DVP 接口):
 *    XCLK  -> GPIO 10
 *    SIOD  -> GPIO 40 (SDA)
 *    SIOC  -> GPIO 39 (SCL)
 *    D0    -> GPIO 15
 *    D1    -> GPIO 17
 *    D2    -> GPIO 18
 *    D3    -> GPIO 16
 *    D4    -> GPIO 14
 *    D5    -> GPIO 12
 *    D6    -> GPIO 11
 *    D7    -> GPIO 48
 *    VSYNC -> GPIO 38
 *    HREF  -> GPIO 47
 *    PCLK  -> GPIO 13
 *    PWDN  -> -1 (不连接)
 *    RESET -> -1 (不连接)
 *
 *  舵机 (LEDC PWM):
 *    舵机1 (有害垃圾) -> GPIO 1
 *    舵机2 (可回收物) -> GPIO 2
 *    舵机3 (厨余垃圾) -> GPIO 3
 *    舵机4 (其他垃圾) -> GPIO 4
 *    舵机5 (传送带/平台) -> GPIO 5
 *
 *  DFPlayer Mini (UART2):
 *    TX -> GPIO 7  (ESP32-S3 RX)
 *    RX -> GPIO 6  (ESP32-S3 TX)
 *
 *  LED 指示灯 (GPIO):
 *    有害垃圾 LED (红) -> GPIO 35
 *    可回收物 LED (蓝) -> GPIO 36
 *    厨余垃圾 LED (绿) -> GPIO 37
 *    其他垃圾 LED (黄) -> GPIO 45
 *    状态 LED (白)     -> GPIO 46
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// 版本信息 / Version Info
// ============================================================
#define APP_VERSION_MAJOR   1
#define APP_VERSION_MINOR   0
#define APP_VERSION_PATCH   0
#define APP_VERSION_STR     "1.0.0"
#define APP_NAME            "AI智能垃圾分类系统"

// ============================================================
// 垃圾类型定义 / Garbage Type Definitions
// ============================================================
typedef enum {
    GARBAGE_HAZARDOUS  = 0,  ///< 有害垃圾 (红色卡片)
    GARBAGE_RECYCLABLE = 1,  ///< 可回收物 (蓝色卡片)
    GARBAGE_KITCHEN    = 2,  ///< 厨余垃圾 (绿色卡片)
    GARBAGE_OTHER      = 3,  ///< 其他垃圾 (灰色卡片)
    GARBAGE_UNKNOWN    = 4,  ///< 未知/未检测到
    GARBAGE_TYPE_COUNT = 5
} garbage_type_t;

// 垃圾类型中文名称
static const char* const GARBAGE_NAMES_CN[] = {
    "有害垃圾",
    "可回收物",
    "厨余垃圾",
    "其他垃圾",
    "未知类型"
};

// 垃圾类型英文名称
static const char* const GARBAGE_NAMES_EN[] = {
    "Hazardous Waste",
    "Recyclable Waste",
    "Kitchen Waste",
    "Other Waste",
    "Unknown"
};

// ============================================================
// 系统状态定义 / System State Definitions
// ============================================================
typedef enum {
    SYS_STATE_INIT       = 0,  ///< 初始化中
    SYS_STATE_IDLE       = 1,  ///< 空闲等待
    SYS_STATE_DETECTING  = 2,  ///< 检测物体中
    SYS_STATE_CLASSIFYING= 3,  ///< 分类识别中
    SYS_STATE_SORTING    = 4,  ///< 分拣投放中
    SYS_STATE_AUDIO      = 5,  ///< 播放音频中
    SYS_STATE_RESETTING  = 6,  ///< 复位中
    SYS_STATE_ERROR      = 7   ///< 错误状态
} sys_state_t;

// ============================================================
// 分类结果结构体 / Classification Result
// ============================================================
typedef struct {
    garbage_type_t type;      ///< 垃圾类型
    int            confidence; ///< 置信度 0-100
    uint8_t        avg_r;     ///< ROI 平均红色分量
    uint8_t        avg_g;     ///< ROI 平均绿色分量
    uint8_t        avg_b;     ///< ROI 平均蓝色分量
    float          hue;       ///< 色相 0-360
    float          saturation;///< 饱和度 0-1
    float          value;     ///< 明度 0-1
} classification_result_t;

// ============================================================
// 摄像头引脚配置 / Camera Pin Configuration
// ============================================================
// 选择开发板类型 (取消注释对应的板子)
#define BOARD_ESP32S3_DEVKIT   // ESP32-S3-DevKitC-1 + 外接 OV2640
// #define BOARD_ESP32S3_EYE   // ESP32-S3-EYE 内置摄像头

#ifdef BOARD_ESP32S3_EYE
    #define CAM_PIN_PWDN    -1
    #define CAM_PIN_RESET   -1
    #define CAM_PIN_XCLK    15
    #define CAM_PIN_SIOD    4
    #define CAM_PIN_SIOC    5
    #define CAM_PIN_D7      16
    #define CAM_PIN_D6      17
    #define CAM_PIN_D5      18
    #define CAM_PIN_D4      12
    #define CAM_PIN_D3      10
    #define CAM_PIN_D2      8
    #define CAM_PIN_D1      9
    #define CAM_PIN_D0      11
    #define CAM_PIN_VSYNC   6
    #define CAM_PIN_HREF    7
    #define CAM_PIN_PCLK    13
#else
    // ESP32-S3-DevKitC-1 标准配置
    #define CAM_PIN_PWDN    -1
    #define CAM_PIN_RESET   -1
    #define CAM_PIN_XCLK    10
    #define CAM_PIN_SIOD    40
    #define CAM_PIN_SIOC    39
    #define CAM_PIN_D7      48
    #define CAM_PIN_D6      11
    #define CAM_PIN_D5      12
    #define CAM_PIN_D4      14
    #define CAM_PIN_D3      16
    #define CAM_PIN_D2      18
    #define CAM_PIN_D1      17
    #define CAM_PIN_D0      15
    #define CAM_PIN_VSYNC   38
    #define CAM_PIN_HREF    47
    #define CAM_PIN_PCLK    13
#endif

// ============================================================
// 舵机引脚配置 / Servo Pin Configuration
// ============================================================
#define SERVO_HAZARDOUS_GPIO    1   ///< 有害垃圾桶舵机
#define SERVO_RECYCLABLE_GPIO   2   ///< 可回收物桶舵机
#define SERVO_KITCHEN_GPIO      3   ///< 厨余垃圾桶舵机
#define SERVO_OTHER_GPIO        4   ///< 其他垃圾桶舵机
#define SERVO_PLATFORM_GPIO     5   ///< 传送平台舵机

// 舵机 PWM 参数
#define SERVO_FREQ_HZ           50      ///< 舵机频率 50Hz
#define SERVO_TIMER_RESOLUTION  LEDC_TIMER_14_BIT  ///< 14位分辨率
#define SERVO_MIN_PULSE_US      500     ///< 最小脉冲宽度 (0度)
#define SERVO_MAX_PULSE_US      2500    ///< 最大脉冲宽度 (180度)
#define SERVO_PERIOD_US         20000   ///< 周期 20ms

// 舵机角度定义
#define SERVO_ANGLE_CLOSED      0       ///< 挡板关闭角度
#define SERVO_ANGLE_OPEN        90      ///< 挡板打开角度
#define SERVO_PLATFORM_HOME     90      ///< 平台初始位置
#define SERVO_PLATFORM_HAZARDOUS  0     ///< 有害垃圾方向
#define SERVO_PLATFORM_RECYCLABLE 60    ///< 可回收物方向
#define SERVO_PLATFORM_KITCHEN    120   ///< 厨余垃圾方向
#define SERVO_PLATFORM_OTHER      180   ///< 其他垃圾方向

// ============================================================
// DFPlayer Mini 配置 / DFPlayer Mini Configuration
// ============================================================
#define DFPLAYER_UART_NUM       UART_NUM_1
#define DFPLAYER_TX_GPIO        6   ///< ESP32-S3 TX -> DFPlayer RX
#define DFPLAYER_RX_GPIO        7   ///< ESP32-S3 RX -> DFPlayer TX
#define DFPLAYER_BAUD_RATE      9600
#define DFPLAYER_UART_BUF_SIZE  256

// 音频文件编号 (SD卡 /01/ 目录下)
#define AUDIO_TRACK_SYSTEM_READY    1   ///< "系统就绪"
#define AUDIO_TRACK_HAZARDOUS       2   ///< "有害垃圾，请注意安全处理"
#define AUDIO_TRACK_RECYCLABLE      3   ///< "可回收物，感谢您的环保行动"
#define AUDIO_TRACK_KITCHEN         4   ///< "厨余垃圾，请妥善处理"
#define AUDIO_TRACK_OTHER           5   ///< "其他垃圾"
#define AUDIO_TRACK_UNKNOWN         6   ///< "无法识别，请重新放置"
#define AUDIO_TRACK_ERROR           7   ///< "系统错误"

// ============================================================
// LED 引脚配置 / LED Pin Configuration
// ============================================================
#define LED_HAZARDOUS_GPIO      35  ///< 有害垃圾 LED (红色)
#define LED_RECYCLABLE_GPIO     36  ///< 可回收物 LED (蓝色)
#define LED_KITCHEN_GPIO        37  ///< 厨余垃圾 LED (绿色)
#define LED_OTHER_GPIO          45  ///< 其他垃圾 LED (黄色)
#define LED_STATUS_GPIO         46  ///< 系统状态 LED (白色)

// ============================================================
// 图像处理参数 / Image Processing Parameters
// ============================================================
#define IMG_WIDTH               320
#define IMG_HEIGHT              240

// ROI (感兴趣区域) - 图像中心区域
#define ROI_X_START             (IMG_WIDTH / 4)
#define ROI_Y_START             (IMG_HEIGHT / 4)
#define ROI_WIDTH               (IMG_WIDTH / 2)
#define ROI_HEIGHT              (IMG_HEIGHT / 2)

// ============================================================
// 系统时序参数 / System Timing Parameters
// ============================================================
#define DETECTION_COOLDOWN_MS   3000    ///< 检测冷却时间
#define SORTING_DURATION_MS     2000    ///< 分拣持续时间
#define RESET_DELAY_MS          1500    ///< 复位延迟
#define CONFIDENCE_THRESHOLD    70      ///< 置信度阈值 (%)
#define MOTION_THRESHOLD        30      ///< 运动检测阈值
#define MOTION_PIXEL_COUNT      500     ///< 运动像素数阈值

// ============================================================
// 识别模式配置 / Recognition Mode Configuration
// ============================================================

// 识别模式选择:
//   MODE_COLOR_CARD  - 颜色卡片识别 (默认，无需模型，离线)
//   MODE_TFLITE_LOOKUP - MobileNetV2 识别物体 + 本地查找表
//   MODE_TFLITE_ONLINE - MobileNetV2 识别物体 + WiFi 在线查询
typedef enum {
    MODE_COLOR_CARD    = 0,  ///< 颜色卡片识别 (离线)
    MODE_TFLITE_LOOKUP = 1,  ///< TFLite 物体识别 + 本地查找表 (离线)
    MODE_TFLITE_ONLINE = 2,  ///< TFLite 物体识别 + WiFi 在线查询
} recognition_mode_t;

// 当前识别模式 (修改此处切换模式)
#define RECOGNITION_MODE    MODE_COLOR_CARD

// ============================================================
// WiFi 配置 / WiFi Configuration
// (仅 MODE_TFLITE_ONLINE 模式需要)
// ============================================================
#define WIFI_SSID       "your_wifi_ssid"      ///< WiFi 名称
#define WIFI_PASSWORD   "your_wifi_password"  ///< WiFi 密码
#define WIFI_TIMEOUT_MS 10000                 ///< 连接超时 10秒

// 在线 API 服务器 (可自建或使用公共服务)
// 请求格式: GET http://<server>/classify?item=<object_name>
// 响应格式: {"type":"recyclable","name_cn":"可回收物","confidence":95}
#define ONLINE_API_URL  "http://your-server.com/garbage/classify"

// ============================================================
// TFLite 模型配置 / TFLite Model Configuration
// (仅 MODE_TFLITE_LOOKUP 和 MODE_TFLITE_ONLINE 模式需要)
// ============================================================
#define TFLITE_MODEL_INPUT_SIZE   96    ///< 模型输入尺寸 96x96
#define TFLITE_NUM_CLASSES        1000  ///< ImageNet 类别数 (MobileNetV2)
#define TFLITE_TOP_K              3     ///< 取前3个预测结果

// ============================================================
// FreeRTOS 任务配置 / FreeRTOS Task Configuration
// ============================================================
#define MAIN_TASK_STACK_SIZE    8192
#define MAIN_TASK_PRIORITY      5
#define CAMERA_TASK_STACK_SIZE  8192
#define CAMERA_TASK_PRIORITY    4
#define AUDIO_TASK_STACK_SIZE   4096
#define AUDIO_TASK_PRIORITY     3

#ifdef __cplusplus
}
#endif
