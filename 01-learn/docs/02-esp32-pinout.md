# ESP32 引脚说明

## ESP32 DevKit V1 引脚图

```
                    ┌─────────────────────┐
               3.3V │ 3V3             GND │ GND
                GND │ GND             D23 │ GPIO23 (MOSI/SPI)
               D15  │ GPIO15          D22 │ GPIO22 (SCL/I2C)
               D2   │ GPIO2           TX0 │ GPIO1  (UART0 TX)
               D4   │ GPIO4           RX0 │ GPIO3  (UART0 RX)
               RX2  │ GPIO16          D21 │ GPIO21 (SDA/I2C)
               TX2  │ GPIO17          D19 │ GPIO19 (MISO/SPI)
               D5   │ GPIO5           D18 │ GPIO18 (SCK/SPI)
               D18  │ GPIO18           D5 │ GPIO5
               D19  │ GPIO19          TX2 │ GPIO17
               D21  │ GPIO21          RX2 │ GPIO16
               RX0  │ GPIO3            D4 │ GPIO4
               TX0  │ GPIO1            D2 │ GPIO2
               D22  │ GPIO22          D15 │ GPIO15
               D23  │ GPIO23          GND │ GND
                GND │ GND             3V3 │ 3.3V
                    │                     │
               D13  │ GPIO13          D12 │ GPIO12
               D12  │ GPIO12          D14 │ GPIO14
               D14  │ GPIO14          D27 │ GPIO27
               D27  │ GPIO27          D26 │ GPIO26
               D26  │ GPIO26          D25 │ GPIO25
               D25  │ GPIO25          D33 │ GPIO33
               D33  │ GPIO33          D32 │ GPIO32
               D32  │ GPIO32          D35 │ GPIO35 (仅输入)
               TDI  │ GPIO35          D34 │ GPIO34 (仅输入)
               TDO  │ GPIO34          VN  │ GPIO39 (仅输入)
               TCK  │ GPIO39          VP  │ GPIO36 (仅输入)
               TMS  │ GPIO36          EN  │ ENABLE
                EN  │ ENABLE         GND  │ GND
               GND  │ GND            VIN  │ 5V
               VIN  │ 5V                  │
                    └─────────────────────┘
```

## GPIO 功能说明

### 数字 I/O GPIO

| GPIO | 功能 | 备注 |
|------|------|------|
| GPIO0 | Boot/IO | 上电时低电平进入下载模式 |
| GPIO1 | UART0 TX | 默认串口发送 |
| GPIO2 | IO/LED | 板载 LED（部分开发板） |
| GPIO3 | UART0 RX | 默认串口接收 |
| GPIO4 | IO | 通用 |
| GPIO5 | IO/SPI CS | SPI 片选 |
| GPIO12 | IO/JTAG TDI | 上电时影响 Flash 电压 |
| GPIO13 | IO/JTAG TCK | 通用 |
| GPIO14 | IO/JTAG TMS | 通用 |
| GPIO15 | IO/JTAG TDO | 上电时影响调试输出 |
| GPIO16 | IO/UART2 RX | 通用 |
| GPIO17 | IO/UART2 TX | 通用 |
| GPIO18 | IO/SPI SCK | SPI 时钟 |
| GPIO19 | IO/SPI MISO | SPI 主入从出 |
| GPIO21 | IO/I2C SDA | I2C 数据 |
| GPIO22 | IO/I2C SCL | I2C 时钟 |
| GPIO23 | IO/SPI MOSI | SPI 主出从入 |
| GPIO25 | IO/DAC1 | DAC 输出 1 |
| GPIO26 | IO/DAC2 | DAC 输出 2 |
| GPIO27 | IO | 通用 |
| GPIO32 | IO/ADC1_CH4 | ADC 输入 |
| GPIO33 | IO/ADC1_CH5 | ADC 输入 |
| GPIO34 | 仅输入/ADC1_CH6 | 无内部上下拉 |
| GPIO35 | 仅输入/ADC1_CH7 | 无内部上下拉 |
| GPIO36 (VP) | 仅输入/ADC1_CH0 | 无内部上下拉 |
| GPIO39 (VN) | 仅输入/ADC1_CH3 | 无内部上下拉 |

### ADC 通道

| ADC | GPIO | 说明 |
|-----|------|------|
| ADC1_CH0 | GPIO36 (VP) | 12位精度 |
| ADC1_CH1 | GPIO37 | 12位精度 |
| ADC1_CH2 | GPIO38 | 12位精度 |
| ADC1_CH3 | GPIO39 (VN) | 12位精度 |
| ADC1_CH4 | GPIO32 | 12位精度 |
| ADC1_CH5 | GPIO33 | 12位精度 |
| ADC1_CH6 | GPIO34 | 12位精度 |
| ADC1_CH7 | GPIO35 | 12位精度 |
| ADC2_CH0 | GPIO4 | WiFi 使用时不可用 |
| ADC2_CH1 | GPIO0 | WiFi 使用时不可用 |
| ADC2_CH2 | GPIO2 | WiFi 使用时不可用 |
| ADC2_CH3 | GPIO15 | WiFi 使用时不可用 |
| ADC2_CH4 | GPIO13 | WiFi 使用时不可用 |
| ADC2_CH5 | GPIO12 | WiFi 使用时不可用 |
| ADC2_CH6 | GPIO14 | WiFi 使用时不可用 |
| ADC2_CH7 | GPIO27 | WiFi 使用时不可用 |
| ADC2_CH8 | GPIO25 | WiFi 使用时不可用 |
| ADC2_CH9 | GPIO26 | WiFi 使用时不可用 |

> ⚠️ **注意**: ADC2 在 WiFi 启用时无法使用，建议优先使用 ADC1。

### 默认通信接口

| 接口 | 引脚 | 说明 |
|------|------|------|
| I2C SDA | GPIO21 | 可软件重映射 |
| I2C SCL | GPIO22 | 可软件重映射 |
| SPI MOSI | GPIO23 | VSPI |
| SPI MISO | GPIO19 | VSPI |
| SPI SCK | GPIO18 | VSPI |
| SPI CS | GPIO5 | VSPI |
| UART0 TX | GPIO1 | 调试串口 |
| UART0 RX | GPIO3 | 调试串口 |
| UART2 TX | GPIO17 | 用户串口 |
| UART2 RX | GPIO16 | 用户串口 |

## 注意事项

1. **输入输出电压**: ESP32 GPIO 为 **3.3V** 逻辑电平
2. **最大电流**: 单个 GPIO 最大输出电流约 **40mA**，总电流不超过 **1200mA**
3. **仅输入引脚**: GPIO34、GPIO35、GPIO36、GPIO39 只能作为输入，无内部上下拉
4. **启动引脚**: GPIO0、GPIO2、GPIO12、GPIO15 在启动时有特殊功能，使用时需注意
5. **ADC 精度**: 使用 WiFi 时，ADC2 不可用；ADC 在高电压时精度较低
6. **上拉/下拉**: 大多数 GPIO 支持内部上拉（约 45kΩ）和下拉电阻