# ESP32-S3 AS608 Fingerprint Module

这是一个独立的 ESP-IDF 示例工程，用 ESP32-S3 通过 UART 驱动
AS608/R30x 兼容指纹模块，支持读取参数、录入、验证搜索、删除和清空模板库。

## 接线

默认配置避开了常见下载/监视串口：

| AS608 模块 | ESP32-S3 默认配置 |
| --- | --- |
| VCC | 按模块丝印接 3V3 或 5V |
| GND | GND |
| TX | GPIO18 / ESP RX |
| RX | GPIO17 / ESP TX |

UART 必须交叉连接：`AS608 TX -> ESP RX`，`AS608 RX -> ESP TX`。
如果你的 AS608 TX 是 5V TTL，请先加电平转换或分压后再进 ESP32-S3 RX。

你图里的开发板顶部有丝印 `TX/RX`，常见对应 ESP32-S3 的
`GPIO43/GPIO44`。如果想接这两个丝印脚，在 `idf.py menuconfig` 中把：

- `AS608 UART -> ESP32 TX GPIO` 改成 `43`
- `AS608 UART -> ESP32 RX GPIO` 改成 `44`

如果串口监视器也占用这组脚，建议继续使用默认 `GPIO17/18`，或者把日志走
USB Serial/JTAG。

## 编译烧录

```bash
cd as608
idf.py set-target esp32s3
idf.py menuconfig
idf.py flash monitor
```

大多数 AS608 模块默认 `57600 8N1`、地址 `0xffffffff`、密码
`0x00000000`。如果初始化超时，优先检查供电、共地、TX/RX 是否接反，以及
`menuconfig` 里的 UART 引脚和波特率。

## 串口命令

烧录后在 monitor 里输入：

```text
h              显示帮助
p              读取模块系统参数
c              读取已存模板数量
v              放手指并验证/搜索
e <id>         录入到模板 id，例如 e 1
d <id> [n]     从 id 开始删除 n 个模板，默认 n=1
x yes          清空整个指纹库
```

录入时会提示放手指、移开手指、再次放同一根手指。模板 `id` 必须小于模块
返回的库容量，常见 AS608 是 `0..126`。

## 交互控制台排查

如果日志能打印，但输入命令没有反应，说明 `idf.py monitor` 连接的端口可能不是
ESP-IDF 当前选择的主控制台端口。进入 `idf.py menuconfig` 检查：

- `Component config -> ESP System Settings -> Channel for console output`
- 使用 USB 口时可选 `USB Serial/JTAG Controller`
- 使用板载 USB 转串口时保持 `Default: UART0`

本示例会直接监听 USB Serial/JTAG 和主控制台 UART0 两路输入；启动日志里应看到
`command input enabled on USB Serial/JTAG` 或 `command input enabled on UART0`。
这样即使 ESP-IDF 把日志打印到副控制台，命令输入也能被读取。

## 驱动文件

- `main/as608.h`：AS608 驱动 API 和状态码
- `main/as608.c`：标准包协议、校验和、UART 收发和高级录入/验证封装
- `main/main.c`：可直接使用的 ESP-IDF 串口命令示例
