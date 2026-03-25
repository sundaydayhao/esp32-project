# ESP32-S3 示波器 — 软件设计文档（SDD）

> 版本：1.0  
> 分析时间：2026-03-19  
> 分析方法：静态源码分析（全量阅读）  
> 依据：`main/*.c/h`，`web/app.js`，`web/index.html`，`CMakeLists.txt`，`sdkconfig.defaults`

---

## 1. Purpose / Scope（目的与范围）

### 1.1 目的

本文档描述 ESP32-S3 模拟示波器项目的软件架构、关键设计决策、接口规范、风险与验证策略，供开发者理解、维护和扩展该系统。

### 1.2 范围

- **固件**：ESP-IDF v5.x，C 语言，FreeRTOS，运行于 ESP32-S3
- **Web 前端**：纯 JavaScript（无框架），运行于现代浏览器
- **不包含**：硬件原理图、PCB 设计、生产烧录流程

### 1.3 背景

该项目将 ESP32-S3 微控制器配置为 WiFi 接入点，通过内置 ADC 采集双通道模拟信号，经 WebSocket 实时推送到浏览器，在 HTML5 Canvas 上渲染示波器波形。无需专用示波器硬件，手机或电脑浏览器即可使用。

---

## 2. System Overview（系统概览）

### 2.1 系统边界

```
┌──────────────────────────────────────────────────────────────────┐
│                    ESP32-S3 示波器系统                            │
│                                                                   │
│  输入：GPIO1 (CH1), GPIO2 (CH2) 模拟信号 (0~3.3V)               │
│  输出：WiFi AP + HTTP/WebSocket 服务 (192.168.4.1)               │
│                                                                   │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────────────────┐ │
│  │  wifi_ap    │  │ adc_sampler  │  │     http_server         │ │
│  │  (WiFi AP)  │  │ (Core1,pri5) │  │  (Core0,pri4 + httpd)   │ │
│  └─────────────┘  └──────┬───────┘  └────────────┬────────────┘ │
│                          │ 双缓冲                  │              │
│                          └────────────────────────┘              │
└──────────────────────────────────────────────────────────────────┘
                                │ WiFi 802.11
                    ┌───────────┴───────────┐
                    │     浏览器客户端        │
                    │  app.js + Canvas 渲染  │
                    └───────────────────────┘
```

### 2.2 关键参数

| 参数 | 值 |
|------|-----|
| 目标芯片 | ESP32-S3 (Xtensa LX7 双核, 240 MHz) |
| ADC 分辨率 | 12 bit (0-4095) |
| 默认采样率 | 100 kSPS（双通道交替） |
| 可调采样率 | 100 Hz ~ 500 kHz |
| 缓冲区大小 | 1024 点/帧/通道 |
| WebSocket 帧大小 | ~4 KB |
| 最大并发客户端 | 4 |
| WiFi 模式 | AP（无需路由器） |
| 服务器 IP | 192.168.4.1 |

---

## 3. High Level Design（高层设计）

### 3.1 模块职责

| 模块 | 文件 | 核心 | 职责 |
|------|------|------|------|
| 主程序 | `main/main.c` | Core 0 | 初始化编排，系统监控 |
| WiFi AP | `main/wifi_ap.c/h` | Core 0 | WiFi 接入点，客户端事件 |
| ADC 采样器 | `main/adc_sampler.c/h` | **Core 1** | ADC 采集，双缓冲，速率控制 |
| HTTP/WS 服务器 | `main/http_server.c/h` | Core 0 | 静态文件，REST API，WS 广播 |
| Web 前端 | `web/app.js` | 浏览器 | 波形渲染，触发，测量，UI |

### 3.2 核心数据流

```
模拟信号 (GPIO1/GPIO2)
    ↓ ADC oneshot 采样 (×1024/帧)
adc_sampler_task [Core 1]
    ↓ 双缓冲 + mutex 交换
frame_buf[read_idx] (adc_frame_t, ~4KB)
    ↓ adc_sampler_get_frame() 轮询
ws_broadcast_task [Core 0]
    ↓ pack_frame() 二进制序列化
WebSocket 二进制帧 (~4KB)
    ↓ TCP/IP (LwIP) + WiFi 802.11
浏览器 WebSocket
    ↓ parseFrame() 反序列化
Float32Array (mV 单位)
    ↓ applyTrigger() 触发对齐
    ↓ renderCh() Canvas 绘制
用户界面（双通道波形）
```

### 3.3 双核任务分配

| 核心 | 任务 | 优先级 | 说明 |
|------|------|--------|------|
| Core 1 | `adc_sampler_task` | 5 | 独占 Core 1，避免 WiFi 干扰 |
| Core 0 | `ws_broadcast_task` | 4 | 与 httpd、WiFi 协同 |
| Core 0 | `httpd`（内部） | — | HTTP 请求处理 |
| Core 0 | `app_main` | — | 初始化后进入低优先级监控 |

---

## 4. Low Level Design（低层设计）

### 4.1 关键函数

#### `adc_sampler_task`（`adc_sampler.c:83`）

- **功能**：持续采集 ADC 数据，填充双缓冲
- **采样方式**：`adc_oneshot_read`（阻塞式单次采样）
- **速率控制**：`esp_rom_delay_us` 精确延迟
- **缓冲交换**：`frame_mutex` 保护的原子交换

#### `pack_frame`（`http_server.c:47`）

- **功能**：将 `adc_frame_t` 序列化为 WebSocket 二进制帧
- **格式**：12 字节头 + 交错 uint16 数据（Little Endian）
- **性能**：O(n) 内存拷贝，n=1024，约 4KB

#### `ws_broadcast_task`（`http_server.c:76`）

- **功能**：轮询新帧，广播给所有 WebSocket 客户端
- **客户端管理**：发送失败时 swap-with-last 移除（O(1)）
- **空闲处理**：无客户端时 10ms 延迟，无新帧时 1ms 延迟

#### `applyTrigger`（`app.js:163`）

- **功能**：在数据中搜索触发点，返回对齐后的数据切片
- **算法**：线性搜索第一个满足条件的边沿
- **触发条件**：上升沿（`p < level && c >= level`）或下降沿

#### `renderCh`（`app.js:183`）

- **功能**：在 Canvas 上渲染单通道波形
- **坐标变换**：`mv2y(mv, ch, H)` 将 mV 转换为像素坐标
- **显示模式**：line（折线）/ dot（散点）/ fill（填充）
- **余辉效果**：半透明清屏（`rgba(5,10,14, 1-persistence*0.08)`）

### 4.2 关键常量

| 常量 | 值 | 位置 | 说明 |
|------|-----|------|------|
| `ADC_SAMPLE_RATE_HZ` | 100,000 | `adc_sampler.h:8` | 默认采样率 |
| `ADC_BUFFER_SIZE` | 1024 | `adc_sampler.h:9` | 每帧采样点数 |
| `ADC_CHANNEL_COUNT` | 2 | `adc_sampler.h:10` | 通道数 |
| `ADC_VREF_MV` | 3300 | `adc_sampler.h:17` | 参考电压 mV |
| `MAX_WS_CLIENTS` | 4 | `http_server.c:24` | 最大 WS 客户端数 |
| `GRID_COLS` | 10 | `app.js:13` | 水平格数 |
| `GRID_ROWS` | 8 | `app.js:14` | 垂直格数 |
| `WS_URL` | `ws://${hostname}/ws` | `app.js:16` | WebSocket 地址 |

---

## 5. Call Flow（调用流程）

### 5.1 启动流程

```
[ESP-IDF 启动]
    → app_main()                        main.c:16
        → nvs_flash_init()              [NVS 初始化]
        → adc_sampler_init()            adc_sampler.c:140
            → xSemaphoreCreateMutex()
            → adc_oneshot_new_unit()    [ADC1 初始化]
            → adc_oneshot_config_channel() ×2
            → adc_calibration_init() ×2
            → xTaskCreatePinnedToCore(adc_sampler_task, Core1, pri=5)
        → wifi_ap_init()                wifi_ap.c:35
            → esp_netif_init()
            → esp_event_loop_create_default()
            → esp_wifi_init() + esp_wifi_start()
        → http_server_start()           http_server.c:261
            → httpd_start()
            → httpd_register_uri_handler() ×6
            → xTaskCreatePinnedToCore(ws_broadcast_task, Core0, pri=4)
        → while(1): vTaskDelay(10s) + log heap
```

### 5.2 数据采集与推送流程（稳态）

```
[Core 1 - adc_sampler_task]
    loop:
        采集 1024 × (CH1 + CH2)
        计算实际采样率
        mutex.lock → 交换缓冲区 → mutex.unlock

[Core 0 - ws_broadcast_task]
    loop:
        get_frame() → 若无新帧 delay(1ms)
        pack_frame() → 4108 字节二进制帧
        for each client: httpd_ws_send_frame_async()

[浏览器]
    ws.onmessage → parseFrame() → applyTrigger() → renderCh() × 2
```

### 5.3 WebSocket 连接建立流程

```
[浏览器] new WebSocket('ws://192.168.4.1/ws')
    → [ESP32] ws_handler() HTTP_GET
        → ws_fds[count++] = fd
    → [浏览器] ws.onopen → sendConfig({sample_rate: 50000})
    → [ESP32] ws_handler() TEXT frame
        → adc_sampler_set_rate(50000)
```

### 5.4 采样率修改流程

```
[浏览器] sampleRateSelect.change
    → sendConfig() → ws.send(JSON)
    → [ESP32] ws_handler() → adc_sampler_set_rate(rate)
        → sample_rate_hz = clamp(rate, 100, 500000)
    → [adc_sampler_task] 下一帧使用新速率
```

### 5.5 退出/复位流程

```
[硬件复位 / 看门狗]
    → ESP-IDF 重启
    → app_main() 重新执行
    （无软件关闭流程）
```

---

## 6. Error Handling & Logging（错误处理与日志）

### 6.1 错误处理策略

| 错误类型 | 处理方式 | 位置 |
|----------|----------|------|
| ESP-IDF API 失败（关键） | `ESP_ERROR_CHECK` → panic → 重启 | 全局 |
| HTTP 服务器启动失败 | 返回 NULL，**无后续处理** ⚠️ | `main.c:37` |
| ADC 读取失败 | `ESP_ERROR_CHECK` → panic | `adc_sampler.c:96-97` |
| WebSocket 发送失败 | 移除客户端，继续 | `http_server.c:107-111` |
| JSON 解析失败 | 返回 400 / 静默忽略 | `http_server.c:168-172` |
| 触发点未找到 | 不渲染当前帧 | `app.js:163-178` |
| WebSocket 断开 | 5 秒后自动重连 | `app.js:97-99` |

### 6.2 日志标签

| 模块 | TAG | 日志级别 |
|------|-----|----------|
| `main.c` | `oscilloscope_main` | INFO |
| `wifi_ap.c` | `wifi_ap` | INFO |
| `adc_sampler.c` | `adc_sampler` | INFO/WARN/ERROR |
| `http_server.c` | `http_server` | INFO/WARN/ERROR |

日志级别配置：`sdkconfig.defaults:33`（`CONFIG_LOG_DEFAULT_LEVEL_INFO`）

---

## 7. Performance Considerations（性能考量）

### 7.1 潜在瓶颈

| 瓶颈 | 说明 | 影响 |
|------|------|------|
| ADC oneshot 模式 | 每次采样需要完整的 ADC 转换周期，无 DMA | 限制最大采样率 ~200 kSPS |
| 双通道交替采样 | CH1 和 CH2 串行采样，非同步 | 两通道间有时序偏差 |
| WebSocket 广播 | 每帧 4KB × N 客户端，串行发送 | 多客户端时延迟增加 |
| `pack_frame` 内存拷贝 | 每帧 4KB memcpy | 可优化为零拷贝 |
| 前端 Canvas 渲染 | 每帧重绘 1024 点 × 2 通道 | 高帧率时 CPU 占用高 |

### 7.2 优化建议

1. **ADC DMA 模式**：使用 `adc_continuous` 驱动替代 `adc_oneshot`，可达更高采样率
2. **零拷贝 WebSocket**：直接在 `frame_buf` 上构建帧，避免 `pack_frame` 拷贝
3. **帧率限制**：前端添加帧率上限（如 60 fps），避免过度渲染
4. **WebSocket 压缩**：启用 permessage-deflate 压缩（需 httpd 支持）

---

## 8. Security Considerations（安全考量）

### 8.1 已识别安全问题

| 问题 | 严重度 | 位置 | 建议 |
|------|--------|------|------|
| WiFi 密码硬编码 | 低（本地 AP） | `wifi_ap.c:15` | 通过 menuconfig/NVS 配置 |
| HTTP 无认证 | 低（本地网络） | `http_server.c` | 可添加 Basic Auth |
| JSON 输入未严格校验 | 低 | `api_config_handler` | 已有类型检查，可加范围校验 |
| WebSocket 无认证 | 低（本地网络） | `ws_handler` | 可添加 token 验证 |
| ADC 输入无过压保护 | 高（硬件） | 硬件 | 添加限压电路 |

> ⚠️ **敏感信息**：已检测到 WiFi 凭据硬编码（位置：`wifi_ap.c:14-15`，`sdkconfig.defaults:7-8`，类型：SSID + 密码）。本系统为本地 AP 模式，风险较低，但建议生产环境通过配置文件管理。

### 8.2 输入校验

| 输入点 | 校验 | 位置 |
|--------|------|------|
| POST /api/config body | 长度限制 255B，JSON 解析，数值类型检查 | `http_server.c:158-180` |
| WS TEXT 命令 | 长度限制 128B，JSON 解析 | `http_server.c:225-235` |
| `sample_rate` 值 | 范围截断 [100, 500000] | `adc_sampler.c:208-209` |
| ADC 输入电压 | **无软件保护**，依赖硬件 | — |

---

## 9. Verification Plan（验证计划）

### 9.1 单元测试建议

| 测试项 | 方法 | 优先级 |
|--------|------|--------|
| `pack_frame` 序列化正确性 | 构造已知 `adc_frame_t`，验证输出字节序列 | 高 |
| `adc_sampler_set_rate` 边界 | 测试 0, 99, 100, 500000, 500001 | 高 |
| `applyTrigger` 触发逻辑 | 构造已知波形，验证触发点位置 | 高 |
| `calcMeas` 测量精度 | 构造正弦波，验证 Vpp/Vrms/Freq | 中 |
| `mv2y`/`y2mv` 坐标变换 | 验证往返转换精度 | 中 |
| 双缓冲并发安全 | 多线程压力测试（需 POSIX 模拟环境） | 中 |

### 9.2 集成测试点

| 测试场景 | 验证内容 |
|----------|----------|
| 上电启动 | WiFi AP 出现，HTTP 服务可访问 |
| 浏览器连接 | WebSocket 建立，波形数据流动 |
| 采样率修改 | 修改后实际帧率变化，前端显示更新 |
| 多客户端 | 4 个客户端同时连接，均能收到数据 |
| 客户端断开 | 断开后服务器正确移除，不影响其他客户端 |
| 长时间运行 | 24 小时运行，堆内存无泄漏 |
| 触发功能 | 上升沿/下降沿触发，波形稳定 |
| 演示模式 | 断开 WiFi，浏览器显示演示波形 |

### 9.3 性能测试

| 测试项 | 目标 |
|--------|------|
| 最大采样率 | ≥ 100 kSPS（双通道） |
| WebSocket 帧率 | ≥ 30 fps（单客户端） |
| 堆内存 | 运行 1 小时后剩余 ≥ 50 KB |
| WiFi 连接延迟 | < 5 秒 |

---

## 10. Open Questions / TODO（待确认项）

| 编号 | 问题 | 优先级 |
|------|------|--------|
| Q1 | `http_server_start()` 失败时 `app_main` 应如何处理？当前无错误处理 | 高 |
| Q2 | `ws_fds[]` 并发访问是否在所有 ESP-IDF 版本上安全？需确认 httpd 任务调度行为 | 中 |
| Q3 | ADC 校准方案（curve fitting vs line fitting）在 ESP32-S3 上哪个更准确？ | 中 |
| Q4 | 两通道采样时序偏差有多大？是否影响相位测量？ | 中 |
| Q5 | `esp_rom_delay_us` 在高负载下精度如何？是否需要改用 `esp_timer` 回调？ | 低 |
| Q6 | Web 文件是否需要 gzip 压缩以减小 Flash 占用？ | 低 |
| Q7 | 是否需要支持 HTTPS/WSS？（当前为明文） | 低 |
| Q8 | `adc_sampler_task` 栈大小 4096B 是否足够？需实测栈水位 | 中 |