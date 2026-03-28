# 03 关键流程与逻辑分析（Design Details）

> 依据：全部源文件静态分析  
> 分析时间：2026-03-19

---

## 1. 关键数据结构

### 1.1 `adc_frame_t`（`adc_sampler.h:22-27`）

```c
typedef struct {
    uint16_t ch1[ADC_BUFFER_SIZE];  // CH1 原始 ADC 值 (0-4095), 1024 个
    uint16_t ch2[ADC_BUFFER_SIZE];  // CH2 原始 ADC 值 (0-4095), 1024 个
    uint32_t sample_rate;           // 实际采样率 Hz
    uint32_t timestamp_ms;          // 帧时间戳（毫秒）
} adc_frame_t;
```

- **大小**：`2 × 1024 × 2 + 4 + 4 = 4104 字节`
- **用途**：ADC 采样器与 HTTP 服务器之间的数据交换单元
- **生命周期**：静态分配（`frame_buf[2]`，`adc_sampler.c:21`），程序运行期间常驻

### 1.2 双缓冲机制（`adc_sampler.c:20-26`）

```c
static adc_frame_t frame_buf[2];       // 双缓冲
static volatile int write_idx = 0;    // 写缓冲索引（采样任务使用）
static volatile int read_idx  = 1;    // 读缓冲索引（广播任务使用）
static volatile bool new_frame_ready = false;
static SemaphoreHandle_t frame_mutex = NULL;
```

**设计意图**：
- 采样任务（Core 1）写入 `frame_buf[write_idx]`
- 广播任务（Core 0）读取 `frame_buf[read_idx]`
- 两个索引始终指向不同缓冲区，避免读写冲突
- 缓冲区交换时用 `frame_mutex` 保护（`adc_sampler.c:127-132`）

### 1.3 WebSocket 二进制帧格式（`http_server.c:36-44`）

```
偏移   大小   类型        内容
0      1B    uint8       魔数 0xAD
1      1B    uint8       魔数 0xC1
2      2B    uint16 LE   采样点数（= ADC_BUFFER_SIZE = 1024）
4      4B    uint32 LE   实际采样率 Hz
8      4B    uint32 LE   时间戳 ms
12     N*4B  uint16 LE   交错数据: ch1[0],ch2[0],ch1[1],ch2[1],...
```

- **总大小**：`12 + 1024 × 4 = 4108 字节`（约 4 KB/帧）
- **魔数验证**：浏览器端 `parseFrame()` 检查 `0xAD 0xC1`（`app.js:116-117`）

### 1.4 WebSocket 客户端表（`http_server.c:25-27`）

```c
#define MAX_WS_CLIENTS  4
static int ws_fds[MAX_WS_CLIENTS];    // 客户端 socket fd 数组
static int ws_client_count = 0;       // 当前连接数
```

### 1.5 前端状态对象（`app.js:17-60`）

```javascript
const state = {
    ws, running, singleShot, singleCaptured,
    ch1: { enabled, vdiv, offset, color, glowColor, fillColor },
    ch2: { enabled, vdiv, offset, color, glowColor, fillColor },
    timebase,       // µs/格
    sampleRate,     // 期望采样率
    trigger: { channel, edge, level },
    displayMode,    // 'line' | 'dot' | 'fill'
    persistence,    // 0-10
    frameCount, fps, fpsTimer,
    cursor: { ch1: {x,y,show}, ch2: {x,y,show} },
    ch1Data, ch2Data, actualSampleRate, bufferSize, timestamp
};
```

---

## 2. 关键业务流程

### 2.1 ADC 采样流程

**触发入口**：`adc_sampler_init()` 创建任务后自动运行（`adc_sampler.c:175-183`）

**主路径**（`adc_sampler_task`，`adc_sampler.c:83-135`）：

```
1. 获取写缓冲指针：wbuf = &frame_buf[write_idx]
2. 循环采集 1024 个样本：
   a. adc_oneshot_read(CH0) → raw_ch1
   b. adc_oneshot_read(CH1) → raw_ch2
   c. wbuf->ch1[i] = raw_ch1
   d. wbuf->ch2[i] = raw_ch2
   e. 速率控制：
      - period_us = 1,000,000 / sample_rate_hz
      - 若 period_us > 10µs：
          elapsed = now - last_time
          wait = period_us - elapsed
          若 wait > 100µs：esp_rom_delay_us(wait)
3. 计算实际采样率：
   wbuf->sample_rate = ADC_BUFFER_SIZE * 1e6 / frame_duration_us
4. 记录时间戳：wbuf->timestamp_ms = now_us / 1000
5. 交换缓冲区（加锁）：
   xSemaphoreTake(frame_mutex, portMAX_DELAY)
   swap(write_idx, read_idx)
   new_frame_ready = true
   xSemaphoreGive(frame_mutex)
6. 回到步骤 1（无限循环）
```

**分支条件**：
- `period_us <= 10`（采样率 ≥ 100 kHz）：不延迟，全速采样
- `period_us > 10`（采样率 < 100 kHz）：精确延迟控制

**错误处理**：
- `adc_oneshot_read` 失败：`ESP_ERROR_CHECK` 宏触发 panic，系统重启
- `xSemaphoreTake` 超时：`portMAX_DELAY` 永久等待，不会超时

**资源生命周期**：
- `adc1_handle`：`adc_sampler_init` 创建，程序运行期间不释放
- `frame_mutex`：`adc_sampler_init` 创建，程序运行期间不释放
- `frame_buf[2]`：静态分配，程序运行期间常驻

### 2.2 WebSocket 广播流程

**触发入口**：`http_server_start()` 创建 `ws_broadcast_task`（`http_server.c:316-324`）

**主路径**（`ws_broadcast_task`，`http_server.c:76-115`）：

```
1. adc_sampler_get_frame(&frame)
   - 若 new_frame_ready == false：vTaskDelay(1ms)，继续
   - 若有新帧：memcpy 到本地 frame，清除 new_frame_ready
2. 若 ws_client_count == 0：vTaskDelay(10ms)，继续
3. pack_frame(&frame, tx_buf)：
   - 写入 12 字节头部
   - 交错写入 ch1/ch2 数据（uint16 LE）
4. 遍历 ws_fds[0..ws_client_count-1]：
   - httpd_ws_send_frame_async(server, fd, &ws_pkt)
   - 若失败：移除该客户端（swap with last, count--）
   - 若成功：i++
5. 回到步骤 1（无限循环）
```

**分支条件**：
- 无新帧：1ms 轮询等待
- 无客户端：10ms 等待（节省 CPU）
- 发送失败：移除客户端（使用 swap-with-last 算法，O(1)）

**错误处理**：
- `pack_frame` 返回 0（缓冲区不足）：`ESP_LOGE` 记录，跳过本帧
- WebSocket 发送失败：移除客户端，继续发送给其他客户端

### 2.3 WebSocket 连接管理流程

**触发入口**：浏览器访问 `ws://192.168.4.1/ws`

**连接建立**（`ws_handler`，`http_server.c:183-200`）：

```
1. req->method == HTTP_GET（WebSocket 握手）
2. fd = httpd_req_to_sockfd(req)
3. 若 ws_client_count < MAX_WS_CLIENTS(4)：
   ws_fds[ws_client_count++] = fd
4. 否则：记录警告，拒绝（但 HTTP 握手已完成）
```

**连接断开**（`ws_handler`，`http_server.c:215-224`）：

```
1. 收到 HTTPD_WS_TYPE_CLOSE 帧
2. 在 ws_fds[] 中找到对应 fd
3. 用最后一个元素替换（swap-with-last），ws_client_count--
```

**命令接收**（`ws_handler`，`http_server.c:225-235`）：

```
1. 收到 HTTPD_WS_TYPE_TEXT 帧
2. cJSON_Parse(buf)
3. 若包含 "sample_rate" 字段：adc_sampler_set_rate(rate)
```

### 2.4 HTTP API 流程

#### GET /api/info

**触发入口**：浏览器 `fetch('/api/info')`（`app.js` 中未直接调用，但 API 存在）

```
1. cJSON_CreateObject()
2. 填充字段：device, sample_rate, buffer_size, channels, vref_mv, free_heap
3. cJSON_Print() → JSON 字符串
4. httpd_resp_sendstr()
5. free(json_str); cJSON_Delete(root)
```

#### POST /api/config

**触发入口**：浏览器 `sendConfig()` 或 `sampleRateSelect` 变更（`app.js:530-533`）

```
1. httpd_req_recv(buf, 255)
2. cJSON_Parse(buf)
3. 提取 "sample_rate" 字段
4. adc_sampler_set_rate(rate)
5. 返回 {"status":"ok"}
```

**边界条件**：
- 请求体为空：返回 400 Bad Request
- JSON 解析失败：返回 400 Bad Request
- `sample_rate` 字段缺失：静默忽略，返回 200

### 2.5 前端波形渲染流程

**触发入口**：`ws.onmessage` 收到二进制帧（`app.js:113`）

```
1. parseFrame(buf)：
   a. 验证魔数 0xAD 0xC1
   b. 读取 n, rate, ts
   c. 解析交错数据 → ch1[], ch2[]（Float32Array，单位 mV）
      ch1[i] = (raw / 4095) * 3300 mV
   d. 更新 state.ch1Data, ch2Data, actualSampleRate 等
2. 若 !state.running：丢弃
3. 若 singleShot && singleCaptured：丢弃
4. applyTrigger(ch1, ch2)：
   a. 若 trigger.channel == 0（自动）：直接返回 {ch1, ch2}
   b. 选择触发源（ch1 或 ch2）
   c. 搜索触发点（上升沿/下降沿）：
      for i in 1..n-1:
        if rising: p < level && c >= level → 找到
        if falling: p > level && c <= level → 找到
   d. 返回从触发点开始的切片
   e. 若未找到触发点：返回 null（不渲染）
5. renderCh(canvas, ctx, data, rate, ch, cursor)：
   a. 清屏（或余辉叠加）
   b. drawGrid()：绘制网格 + 坐标标签
   c. drawTrigLine()：绘制触发电平线
   d. drawWave()：绘制波形
      - 计算显示窗口：win_us = timebase × 10格
      - nSamp = min(win_us/1e6 × rate, data.length)
      - 按 displayMode 绘制（line/dot/fill）
   e. drawCursor()：绘制鼠标光标读数
6. calcMeas(data, rate, 'ch1'/'ch2')：
   - 遍历数据：计算 min, max, sum, sum_sq
   - Vpp = max - min
   - Vrms = sqrt(sum_sq / n)
   - 频率：计数上升过零点，freq = rate / (平均周期)
```

### 2.6 采样率设置流程

**触发路径 1**：浏览器 UI → `sampleRateSelect` → `sendConfig()` → WebSocket JSON → `ws_handler` → `adc_sampler_set_rate()`

**触发路径 2**：浏览器 UI → `sampleRateSelect` → `sendConfig()` → POST /api/config → `api_config_handler` → `adc_sampler_set_rate()`

**`adc_sampler_set_rate`**（`adc_sampler.c:207-212`）：

```c
void adc_sampler_set_rate(uint32_t rate_hz) {
    if (rate_hz < 100)    rate_hz = 100;     // 下限 100 Hz
    if (rate_hz > 500000) rate_hz = 500000;  // 上限 500 kHz
    sample_rate_hz = rate_hz;                // volatile 写，立即生效
}
```

> **注意**：`sample_rate_hz` 是 `volatile uint32_t`，写操作在 ARM Cortex-M 上是原子的（32位对齐），但严格来说不是线程安全的（无内存屏障）。在实践中，最坏情况是采样任务读到旧值，下一帧生效，可接受。

---

## 3. 并发与同步分析

### 3.1 双缓冲同步

| 操作 | 执行者 | 保护机制 |
|------|--------|----------|
| 写入 `frame_buf[write_idx]` | `adc_sampler_task` (Core 1) | 无锁（独占写缓冲） |
| 交换 `write_idx`/`read_idx` | `adc_sampler_task` (Core 1) | `frame_mutex` |
| 读取 `frame_buf[read_idx]` | `ws_broadcast_task` (Core 0) | `frame_mutex` |
| 读取 `new_frame_ready` | `ws_broadcast_task` (Core 0) | volatile（非原子） |
| 写入 `new_frame_ready` | `adc_sampler_task` (Core 1) | 在 mutex 内 |

**潜在问题**：`ws_broadcast_task` 在 `xSemaphoreTake` 之前检查 `new_frame_ready`（`adc_sampler.c:193-195`），这是一个无锁检查。在多核环境下，可能存在 TOCTOU（检查时/使用时）竞态，但由于：
1. `new_frame_ready` 是 `volatile bool`
2. 最坏情况是多读一帧或少读一帧
3. 不会导致内存损坏

实际影响：**可接受**（示波器应用对偶发帧丢失不敏感）。

### 3.2 WebSocket 客户端表并发

**问题**：`ws_fds[]` 和 `ws_client_count` 由两个任务并发访问：
- `ws_handler`（httpd 任务，Core 0）：添加/删除客户端
- `ws_broadcast_task`（Core 0）：遍历客户端发送数据

由于两者都在 **Core 0** 运行，且 `httpd` 是事件驱动的（非抢占式处理），在实践中不会同时执行。但这是一个**隐式假设**，没有显式同步保护。

**风险**：若 `httpd` 任务优先级高于 `ws_broadcast_task`（实际上 httpd 默认优先级通常高于 4），可能在广播任务遍历 `ws_fds[]` 时被抢占，导致数组状态不一致。

**建议**：添加 mutex 保护 `ws_fds[]` 访问（见 Section 8 风险）。

### 3.3 `sample_rate_hz` 并发写

- 写：`adc_sampler_set_rate()`（可能从 httpd 任务调用）
- 读：`adc_sampler_task`（Core 1）

`volatile uint32_t` 在 ESP32-S3（Xtensa LX7）上 32 位写是原子的，但无内存屏障。实际影响：**可接受**。

---

## 4. 边界条件与异常路径

### 4.1 ADC 采样边界

| 条件 | 处理 |
|------|------|
| ADC 读取失败 | `ESP_ERROR_CHECK` → panic → 系统重启 |
| 采样率 < 100 Hz | 截断到 100 Hz（`adc_sampler.c:208`） |
| 采样率 > 500,000 Hz | 截断到 500,000 Hz（`adc_sampler.c:209`） |
| ADC 校准不支持 | 记录警告，使用原始值（`adc_sampler.c:73-75`） |
| 输入电压 > 3.3V | 硬件损坏风险，无软件保护 |

### 4.2 HTTP 服务器边界

| 条件 | 处理 |
|------|------|
| HTTP 服务器启动失败 | 返回 NULL，`app_main` 继续（无错误处理！） |
| WebSocket 客户端 > 4 | 记录警告，拒绝新连接（握手已完成） |
| POST body 为空 | 返回 400 Bad Request |
| POST body 非 JSON | 返回 400 Bad Request |
| WebSocket 发送失败 | 移除客户端，继续 |
| `pack_frame` 缓冲区不足 | 记录错误，跳过本帧 |

### 4.3 WiFi 边界

| 条件 | 处理 |
|------|------|
| WiFi 初始化失败 | `ESP_ERROR_CHECK` → panic → 系统重启 |
| 客户端连接 | 记录日志（MAC + AID） |
| 客户端断开 | 记录日志，WebSocket 连接由 httpd 处理 |
| 最大连接数 4 | WiFi 层拒绝第 5 个连接 |

### 4.4 NVS 边界

| 条件 | 处理 |
|------|------|
| NVS 无空闲页 | 擦除 NVS 分区，重新初始化（`main.c:20-23`） |
| NVS 版本不匹配 | 擦除 NVS 分区，重新初始化 |
| NVS 初始化失败 | `ESP_ERROR_CHECK` → panic |

---

## 5. 外部依赖

### 5.1 硬件依赖

| 依赖 | 说明 | 风险 |
|------|------|------|
| ESP32-S3 ADC1 | GPIO1/GPIO2 模拟输入 | ADC 非线性，高频精度下降 |
| Flash 4MB | 固件 + NVS + SPIFFS | 分区表已配置 |
| WiFi 天线 | 内置天线 | 无外部依赖 |

### 5.2 软件依赖

| 依赖 | 版本要求 | 说明 |
|------|----------|------|
| ESP-IDF | v5.x（推荐 v5.2+） | `esp_adc/adc_oneshot.h` API |
| FreeRTOS | ESP-IDF 内置 | 任务、信号量 |
| LwIP | ESP-IDF 内置 | TCP/IP |
| cJSON | ESP-IDF 内置 | JSON 处理 |

### 5.3 配置依赖

| 配置项 | 来源 | 说明 |
|--------|------|------|
| WiFi SSID/密码 | `wifi_ap.c` 硬编码 | ⚠️ 敏感信息 |
| 采样率 | 运行时可调 | 默认 100 kHz |
| Flash 分区 | `partitions.csv` | factory 3MB + spiffs 960KB |
| FreeRTOS tick | `sdkconfig.defaults` | 1000 Hz |

---

## 6. 性能分析

### 6.1 采样性能

- **理论最大采样率**：ESP32-S3 ADC oneshot 模式约 200-400 kSPS（单通道）
- **双通道实际**：两通道交替采样，有效单通道率减半
- **速率控制精度**：`esp_rom_delay_us` 精度约 ±1µs，高频时误差累积
- **帧率**：100 kHz × 1024 点 ≈ 97.7 帧/秒（理论），实际受 WebSocket 发送限制

### 6.2 WebSocket 传输性能

- **帧大小**：4108 字节/帧
- **WiFi 带宽**：802.11n 理论 ~72 Mbps，实际 ~10-20 Mbps
- **理论最大帧率**：~2400 fps（带宽限制），实际受 httpd 处理能力限制
- **客户端数量影响**：每增加一个客户端，广播时间线性增加

### 6.3 内存使用

| 资源 | 大小 | 说明 |
|------|------|------|
| `frame_buf[2]` | 2 × 4104 = 8208 B | 静态分配 |
| `tx_buf` | 12 + 4096 + 16 = 4124 B | `ws_broadcast_task` 栈上 |
| `ws_broadcast_task` 栈 | 8192 B | `http_server.c:317` |
| `adc_sampler` 栈 | 4096 B | `adc_sampler.c:176` |
| Web 文件（Flash） | ~20 KB | index.html + app.js + style.css |

---

## 7. 前端触发算法

**实现**（`app.js:163-178`）：

```javascript
function applyTrigger(ch1, ch2) {
    if (state.trigger.channel === 0) return { ch1, ch2 };  // 自动模式
    const src    = state.trigger.channel === 1 ? ch1 : ch2;
    const lv     = state.trigger.level;   // mV
    const rising = state.trigger.edge === 'rising';
    for (let i = 1; i < src.length - 1; i++) {
        const p = src[i - 1], c = src[i];
        if (rising ? (p < lv && c >= lv) : (p > lv && c <= lv)) {
            return { ch1: ch1.slice(i), ch2: ch2.slice(i) };
        }
    }
    return null;  // 未找到触发点，不渲染
}
```

**特点**：
- 线性搜索，O(n)，n = 1024
- 找到第一个触发点即返回（不是最优触发点）
- 未找到时返回 null，当前帧不渲染（可能导致画面"冻结"）
- 自动模式（channel=0）：直接渲染，不搜索触发点

**边界条件**：
- 信号幅度小于触发电平：永远找不到触发点
- 信号频率极低（周期 > 帧长）：可能找不到触发点
- 触发电平设置在信号范围外：永远找不到触发点

---

## 8. 风险与待确认项

| 编号 | 风险 | 严重度 | 位置 | 建议 |
|------|------|--------|------|------|
| R1 | `ws_fds[]` 无锁并发访问 | 中 | `http_server.c:25-27` | 添加 mutex 保护 |
| R2 | `http_server_start()` 失败无错误处理 | 高 | `main.c:37` | 检查返回值，失败时重试或 panic |
| R3 | WiFi 密码硬编码 | 低（本地 AP） | `wifi_ap.c:15` | 通过 menuconfig 或 NVS 配置 |
| R4 | ADC 输入无过压保护 | 高（硬件） | 硬件设计 | 添加限压电路（TVS 二极管） |
| R5 | `adc_sampler_task` 栈 4096B 可能不足 | 中 | `adc_sampler.c:176` | 监控栈水位，必要时增大 |
| R6 | WebSocket 客户端 > 4 时握手已完成但被忽略 | 低 | `http_server.c:196-199` | 在握手前拒绝 |
| R7 | `new_frame_ready` TOCTOU 竞态 | 低 | `adc_sampler.c:193` | 可接受，或改用队列 |
| R8 | 触发未找到时画面冻结 | 低（功能） | `app.js:163-178` | 添加超时自动刷新 |