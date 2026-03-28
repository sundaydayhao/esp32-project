/**
 * 示例 01: LED 闪烁 (Blink)
 * 
 * 功能: 控制 ESP32 内置 LED 每秒闪烁一次
 * 
 * 硬件连接:
 *   - 使用 ESP32 内置 LED (GPIO2)
 *   - 或者: LED 正极 → GPIO2 → 220Ω电阻 → GND
 * 
 * 知识点:
 *   - pinMode() 设置引脚模式
 *   - digitalWrite() 控制数字输出
 *   - delay() 延时函数
 */

// 定义 LED 引脚（ESP32 内置 LED 通常在 GPIO2）
#define LED_PIN 2

void setup() {
  // 初始化串口，波特率 115200
  Serial.begin(115200);
