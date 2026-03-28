# 00 代码盘点（Inventory）

> 分析时间：2026-03-19  
> 仓库根目录：`c:/code/esp32-oscilloscope`  
> 工具：静态文件扫描 + 人工阅读

---

## 1. 文件统计

| 类型 | 文件数 | 总行数 |
|------|--------|--------|
| C 源文件（`.c`） | 4 | 697 |
| C 头文件（`.h`） | 4 | 76 |
| JavaScript（`.js`） | 1 | 647 |
| HTML（`.html`） | 1 | 220 |
| CSS（`.css`） | 1 | 498 |
| CMakeLists.txt | 2 | 27 |
| 配置文件 | 2 | 48 |
| **合计** | **15** | **~2213** |

> 依据：`_sources_dump.txt` 中各文件行数统计（main.c:49, wifi_ap.c:74, adc_sampler.c:217, http_server.c:357, wifi_ap.h:11, adc_sampler.h:50, http_server.h:15, app.js:647, index.html:220, style.css:498）

---

## 2. 主要子目录（按代码密度排序）

| 目录 | 文件 | 代码行数 | 说明 |
|------|------|----------|------|
| `main/` | 8 个文件 | ~773 行 C/H | 固件核心逻辑 |
| `web/` | 3 个文件 | ~1365 行 JS/HTML/CSS | 浏览器前端 |
| `/`（根） | 4 个文件 | ~75 行 | 构建配置、分区表、README |

---

## 3. 入口文件 / 入口函数

### 固件入口

| 文件 | 函数 | 行号 | 说明 |
|------|------|------|------|
| `main/main.c` | `app_main(void)` | L16 | ESP-IDF 固件入口，等价于 `main()` |

`app_main` 是 ESP-IDF 框架规定的应用入口函数，由 FreeRTOS 调度器在启动后调用。

### Web 前端入口

| 文件 | 函数/表达式 | 行号 | 说明 |
|------|------------|------|------|
| `web/app.js` | `init()` | L617 | 页面加载完成后调用 |
| `web/app.js` | `document.readyState === 'loading' ? addEventListener('DOMContentLoaded', init) : init()` | L643-644 | 入口触发逻辑 |

---

## 4. 模块列表

| 模块 | 文件 | 函数数 | 说明 |
|------|------|--------|------|
| **主程序** | `main/main.c` | 1 | 初始化编排 |
| **WiFi AP** | `main/wifi_ap.c/h` | 2（1 static） | WiFi 接入点管理 |
| **ADC 采样器** | `main/adc_sampler.c/h` | 6（3 static） | ADC 采集、双缓冲、速率控制 |
| **HTTP/WS 服务器** | `main/http_server.c/h` | 10（8 static） | HTTP 静态文件、REST API、WebSocket 广播 |
| **Web 前端** | `web/app.js` | ~22 | 波形渲染、触发、测量、WebSocket 客户端 |
| **Web UI** | `web/index.html` | — | 双通道示波器界面 HTML |
| **Web 样式** | `web/style.css` | — | 深色主题 CSS |

---

## 5. 关键依赖（ESP-IDF 组件）

依据：`main/CMakeLists.txt` L8-L19

| 组件 | 用途 |
|------|------|
| `nvs_flash` | 非易失性存储（WiFi 校准数据） |
| `esp_wifi` | WiFi 驱动 |
| `esp_netif` | 网络接口抽象 |
| `esp_event` | 事件循环 |
| `esp_http_server` | HTTP + WebSocket 服务器 |
| `esp_adc` | ADC oneshot 驱动 + 校准 |
| `json` (cJSON) | JSON 序列化/反序列化 |
| `esp_timer` | 高精度定时器 |
| `freertos` | 实时操作系统 |
| `lwip` | TCP/IP 协议栈 |

---

## 6. 嵌入式 Web 文件

依据：`main/CMakeLists.txt` L20-L23（`EMBED_FILES` 指令）

```cmake
EMBED_FILES
    "../web/index.html"
    "../web/app.js"
    "../web/style.css"
```

Web 文件在编译时通过 ESP-IDF 的 `EMBED_FILES` 机制嵌入固件 Flash，运行时通过链接器符号 `_binary_*_start/_end` 访问（`http_server.c` L16-L21）。

---

## 7. 硬件配置

依据：`main/adc_sampler.h` L12-L16，`sdkconfig.defaults` L4

| 参数 | 值 | 来源 |
|------|-----|------|
| 目标芯片 | ESP32-S3 | `sdkconfig.defaults:4` |
| CH1 GPIO | GPIO1 (ADC1_CH0) | `adc_sampler.h:13` |
| CH2 GPIO | GPIO2 (ADC1_CH1) | `adc_sampler.h:14` |
| ADC 分辨率 | 12 bit (0-4095) | `adc_sampler.h:22-23` |
| 默认采样率 | 100,000 Hz | `adc_sampler.h:8` |
| 缓冲区大小 | 1024 点/帧 | `adc_sampler.h:9` |
| 参考电压 | 3300 mV | `adc_sampler.h:17` |
| Flash 大小 | 4 MB | `sdkconfig.defaults:22` |

---

## 8. 敏感信息扫描

> ⚠️ **已检测到疑似敏感信息**：
> - 位置：`main/wifi_ap.c` L14-L15，`sdkconfig.defaults` L7-L8
> - 类型：WiFi 接入点凭据（SSID + 密码，明文硬编码）
> - 建议：生产环境应通过 `menuconfig` 或 NVS 动态配置，避免硬编码

---

## 9. 忽略目录

以下目录未纳入分析（按 skill 约束）：
- `build/`（如存在）
- `.git/`
- `node_modules/`（不存在）
- `venv/`（不存在）