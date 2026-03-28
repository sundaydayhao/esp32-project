# 02 函数调用关系图（Call Graph）

> 分析时间：2026-03-19  
> 方法：C/C++ 静态源码分析（人工 + 工具辅助）  
> 工具可用性：Graphviz `dot` — **不可用**；Doxygen — **不可用**；Python SVG 生成 — **已使用**

---

## 1. 工具可用性说明

| 工具 | 状态 | 替代方案 |
|------|------|----------|
| `doxygen` | ❌ 未安装 | 手动静态分析 |
| `dot` (Graphviz) | ❌ 未安装 | Python 直接生成 SVG |
| `pyan3` (Python) | N/A（无 Python 源码） | — |

**替代方案**：通过完整阅读所有 `.c` 文件，手动构建调用图，并用 Python 生成 SVG 和 Graphviz DOT 文件。

> 若需使用 Graphviz 渲染完整版，安装后执行：
> ```bash
> dot -Tsvg docs/diagrams/callgraph_firmware.dot -o docs/diagrams/callgraph_firmware_full.svg
> dot -Tsvg docs/diagrams/module_dependency.dot  -o docs/diagrams/module_dependency_full.svg
> ```

---

## 2. 图文件索引

| 文件 | 格式 | 说明 |
|------|------|------|
| [`diagrams/callgraph_firmware.svg`](diagrams/callgraph_firmware.svg) | SVG | 固件函数调用图（Python 生成） |
| [`diagrams/module_dependency.svg`](diagrams/module_dependency.svg) | SVG | 模块/文件依赖图（Python 生成） |
| [`diagrams/callgraph_firmware.dot`](diagrams/callgraph_firmware.dot) | DOT | 固件调用图 Graphviz 源文件 |
| [`diagrams/module_dependency.dot`](diagrams/module_dependency.dot) | DOT | 模块依赖图 Graphviz 源文件 |

---

## 3. 如何阅读调用图

### 3.1 固件调用图（`callgraph_firmware.svg`）

**节点颜色含义**：
- 🔴 红色（`app_main`）：固件唯一入口
- 🔵 蓝色：跨模块公共 API（`adc_sampler_init`, `wifi_ap_init`, `http_server_start` 等）
- 🟠 橙色：FreeRTOS 任务入口（`adc_sampler_task`, `ws_broadcast_task`）
- 🟡 黄色：模块内部静态函数（HTTP 回调等）
- 🟢 绿色：ESP-IDF / FreeRTOS 系统 API

**边含义**：
- 实线箭头：直接函数调用
- 虚线橙色箭头：`xTaskCreatePinnedToCore` 创建任务（异步）
- 虚线蓝色箭头：跨模块数据读取（`adc_sampler_get_frame`）
- 虚线灰色：注册回调（`wifi_event_handler`）

**覆盖范围**：
- 覆盖所有 4 个 `.c` 文件中的 19 个函数
- 包含主要 ESP-IDF API 调用点
- **不覆盖**：ESP-IDF 内部调用链、宏展开后的调用

### 3.2 模块依赖图（`module_dependency.svg`）

**节点形状含义**：
- 矩形：源文件（`.c`）
- 便签形：头文件（`.h`）
- 组件形：Web 文件（嵌入式）
- 椭圆：ESP-IDF 组件

**边含义**：
- 实线：`#include` 编译时依赖
- 橙色虚线：`EMBED_FILES` 编译时嵌入
- 蓝色虚线：运行时数据流

---

## 4. 完整调用关系文本描述

### 4.1 `app_main`（`main.c:16`）

```
app_main()
├── nvs_flash_init()                    [ESP-IDF]
│   └── (若失败) nvs_flash_erase()
├── adc_sampler_init()                  [adc_sampler.c:140]
│   ├── xSemaphoreCreateMutex()         [FreeRTOS]
│   ├── adc_oneshot_new_unit()          [esp_adc]
│   ├── adc_oneshot_config_channel() ×2 [esp_adc]
│   ├── adc_calibration_init() ×2       [adc_sampler.c:35, static]
│   │   ├── adc_cali_create_scheme_curve_fitting()  [#if CURVE_FITTING]
│   │   └── adc_cali_create_scheme_line_fitting()   [#if LINE_FITTING]
│   └── xTaskCreatePinnedToCore(adc_sampler_task, core=1, pri=5)
├── wifi_ap_init()                      [wifi_ap.c:35]
│   ├── esp_netif_init()
│   ├── esp_event_loop_create_default()
│   ├── esp_netif_create_default_wifi_ap()
│   ├── esp_wifi_init()
│   ├── esp_event_handler_instance_register(→ wifi_event_handler)
│   ├── esp_wifi_set_mode(WIFI_MODE_AP)
│   ├── esp_wifi_set_config()
│   └── esp_wifi_start()
└── http_server_start()                 [http_server.c:261]
    ├── httpd_start()
    ├── httpd_register_uri_handler() ×6
    │   ├── GET /          → index_html_handler
    │   ├── GET /app.js    → app_js_handler
    │   ├── GET /style.css → style_css_handler
    │   ├── GET /api/info  → api_info_handler
    │   ├── POST /api/config → api_config_handler
    │   └── WS  /ws        → ws_handler
    └── xTaskCreatePinnedToCore(ws_broadcast_task, core=0, pri=4)
```

### 4.2 `adc_sampler_task`（`adc_sampler.c:83`，Core 1，无限循环）

```
adc_sampler_task()
└── while(1):
    ├── for i in 0..1023:
    │   ├── adc_oneshot_read(ADC_CHANNEL_0) → raw_ch1
    │   ├── adc_oneshot_read(ADC_CHANNEL_1) → raw_ch2
    │   ├── esp_timer_get_time()
    │   └── esp_rom_delay_us()  [if period_us > 10]
    ├── esp_timer_get_time()    [计算实际采样率]
    ├── xSemaphoreTake(frame_mutex)
    ├── [swap write_idx ↔ read_idx]
    ├── new_frame_ready = true
    └── xSemaphoreGive(frame_mutex)
```

### 4.3 `ws_broadcast_task`（`http_server.c:76`，Core 0，无限循环）

```
ws_broadcast_task()
└── while(1):
    ├── adc_sampler_get_frame(&frame)   [adc_sampler.c:193]
    │   ├── [check new_frame_ready]
    │   ├── xSemaphoreTake(frame_mutex)
    │   ├── memcpy(frame, frame_buf[read_idx])
    │   └── xSemaphoreGive(frame_mutex)
    ├── pack_frame(&frame, tx_buf)      [http_server.c:47, static]
    │   └── [手动序列化 header + interleaved uint16 data]
    └── for each ws_client:
        └── httpd_ws_send_frame_async(server, fd, &ws_pkt)
            └── [若失败则移除客户端]
```

### 4.4 HTTP/WebSocket 回调

```
ws_handler()                            [http_server.c:183]
├── [HTTP_GET] → 注册新 WebSocket 客户端
│   └── ws_fds[ws_client_count++] = fd
└── [WS frame received]
    ├── httpd_ws_recv_frame()
    ├── [CLOSE] → 从 ws_fds[] 移除
    └── [TEXT] → cJSON_Parse() → adc_sampler_set_rate()

api_config_handler()                    [http_server.c:158]
├── httpd_req_recv()
├── cJSON_Parse()
├── adc_sampler_set_rate()
└── httpd_resp_sendstr("{\"status\":\"ok\"}")

api_info_handler()                      [http_server.c:140]
├── adc_sampler_get_rate()
├── cJSON_CreateObject() + cJSON_Add*()
├── cJSON_Print()
└── httpd_resp_sendstr()
```

---

## 5. JavaScript 调用图（`web/app.js`）

```
init()                                  [app.js:617]
├── resizeAll()
│   └── resizeCanvas() ×2
├── initControls()                      [app.js:490]
│   └── addEventListener() × ~15 controls
├── connectWS()                         [app.js:88]
│   └── new WebSocket(WS_URL)
│       ├── onopen → sendConfig()
│       ├── onclose → setTimeout(connectWS, 5000)
│       └── onmessage → parseFrame()   [app.js:113]
│           ├── DataView.getUint*()    [二进制解析]
│           ├── applyTrigger()         [app.js:163]
│           ├── renderCh() ×2          [app.js:183]
│           │   ├── drawGrid()         [app.js:207]
│           │   ├── drawTrigLine()     [app.js:243]
│           │   ├── drawWave()         [app.js:263]
│           │   └── drawCursor()       [app.js:305]
│           └── calcMeas() ×2          [app.js:340]
└── setTimeout(startDemo, 2000)        [若 WS 未连接]
    └── startDemo()                    [app.js:580]
        └── tick() [定时器循环]
            ├── [生成模拟正弦波]
            ├── applyTrigger()
            └── renderCh() ×2
```

---

## 6. 限制说明

| 限制 | 说明 |
|------|------|
| 函数指针/回调 | `wifi_event_handler`、所有 HTTP handler 通过函数指针注册，静态分析已识别但标注为虚线 |
| 宏展开 | `ESP_ERROR_CHECK`、`ESP_LOGI` 等宏展开后的调用未展开 |
| 条件编译 | `adc_calibration_init` 中 `#if ADC_CALI_SCHEME_*` 分支取决于目标平台 |
| 动态调用 | JavaScript 中 `addEventListener` 回调为动态绑定，已在文本描述中列出 |
| ESP-IDF 内部 | `httpd` 内部任务、LwIP 协议栈内部调用链未展开 |