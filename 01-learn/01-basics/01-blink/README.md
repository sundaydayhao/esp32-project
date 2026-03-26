# 01 - LED 闪烁 (Blink)

## 功能说明

控制 ESP32 的 GPIO2 引脚上的 LED 每隔 500ms 闪烁一次，这是学习 ESP-IDF 的第一个程序。

## 硬件连接

```
ESP32 GPIO2 ──── 220Ω 电阻 ──── LED 正极
                               LED 负极 ──── GND
```

> 大多数 ESP32 开发板的 GPIO2 上已有板载 LED，可直接使用。

## 知识点

- `gpio_config_t` 结构体配置 GPIO
- `gpio_set_level()` 控制 GPIO 输出电平
- `vTaskDelay()` FreeRTOS 延时函数
- `pdMS_TO_TICKS()` 毫秒转换为 FreeRTOS ticks
- `ESP_LOGI()` 日志输出

## 编译和运行

```bash
cd 01-basics/01-blink
idf.py set-target esp32
idf.py build
idf.py -p COM3 flash monitor
```

## 预期输出

```
I (300) Blink: LED 闪烁示例启动
I (310) Blink: GPIO2 配置完成，开始闪烁...
I (320) Blink: LED ON
I (820) Blink: LED OFF
I (1320) Blink: LED ON
...
```

## 扩展练习

1. 修改闪烁频率（改变 `vTaskDelay` 的参数）
2. 实现 SOS 摩斯密码闪烁（... --- ...）
3. 使用不同 GPIO 控制多个 LED