# ESP32-WROOM + 2.9 inch SPI E-Paper Example

这个目录现在是一个最小 `ESP-IDF` 示例工程，按你图片里的驱动板丝印接线，初始化后显示一行英文。

## 接线

驱动板引脚定义:

- `GND`: 地
- `3.3V`: 电源
- `SCL`: SPI 时钟
- `SDI`: SPI MOSI
- `RES`: 复位
- `D/C`: 数据/命令选择
- `CS`: 片选
- `BUSY`: 忙状态输出

示例使用的 ESP32-WROOM 引脚:

| 墨水屏驱动板 | ESP32-WROOM |
| --- | --- |
| `GND` | `GND` |
| `3.3V` | `3V3` |
| `SCL` | `GPIO18` |
| `SDI` | `GPIO23` |
| `RES` | `GPIO16` |
| `D/C` | `GPIO17` |
| `CS` | `GPIO5` |
| `BUSY` | `GPIO4` |

注意:

- 这块板只能接 `3.3V`
- `SCL` 是 `SPI SCK`，不是 `I2C SCL`
- 没有用到 `MISO`

## 控制器假设

这份初始化按常见 `2.9"` 黑白墨水屏的 `128x296` 原生面板处理，但绘制坐标默认按长边方向映射，也就是逻辑上按 `296x128` 使用。

代码现在会优先按你提供的 V14 例程对应型号去尝试:

- `GDEM029A01 / IL3820` + `BUSY active level = 0` + `update = 0xC4`
- `GDEM029A01 / IL3820` + `BUSY active level = 1` + `update = 0xC4`

如果这两组都不通，再回退到几组常见 `SSD1680 / UC8151` 兼容参数:

- `BUSY active level = 1` + `update = 0xF7`
- `BUSY active level = 0` + `update = 0xF7`
- `BUSY active level = 1` + `update = 0xC7`
- `BUSY active level = 0` + `update = 0xC7`

另外把首轮 `refresh` 超时从 `5s` 放宽到了 `15s`，避免部分面板第一次全刷较慢时被误判失败。

如果串口里仍然看到:

```text
BUSY timeout at refresh after 15000 ms
```

就说明问题更偏向于:

- 接线不对，尤其是 `BUSY / RES / D/C / CS`
- 屏幕分辨率或控制器型号并不是 `128x296` 的这类面板
- 模块供电不稳，刷新时电压掉下去

## 构建与烧录

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

## 显示内容

示例默认显示:

```text
HELLO EINK
```

文字绘制入口在 `main/main.c`，当前默认按长边横向显示:

```c
framebuffer_draw_text(20, 40, "HELLO EINK", 3);
```

如果你要显示中文，需要再接一个中文字库，或者直接把字先转成位图再写进缓冲区。
