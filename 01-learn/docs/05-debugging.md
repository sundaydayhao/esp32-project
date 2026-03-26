# 调试技巧

## 串口日志调试

### 日志级别

```c
#include "esp_log.h"

static const char *TAG = "MyApp";

// 按严重程度从高到低
ESP_LOGE(TAG, "错误: %s", error_msg);    // Error   红色
ESP_LOGW(TAG, "警告: %d", warn_code);    // Warning 黄色
ESP_LOGI(TAG, "信息: %s", info);         // Info    绿色
ESP_LOGD(TAG, "调试: %d", value);        // Debug   白色
ESP_LOGV(TAG, "详细: %f", data);         // Verbose 白色
```

### 动态设置日志级别

```c
#include "esp_log.h"

// 设置特定 TAG 的日志级别
esp_log_level_set("MyApp", ESP_LOG_DEBUG);

// 设置所有 TAG 的日志级别
esp_log_level_set("*", ESP_LOG_INFO);
```

### 在 menuconfig 中设置日志级别

```
Component config → Log output → Default log verbosity
```

## 堆内存调试

```c
#include "esp_heap_caps.h"

// 查看可用堆内存
size_t free_heap = esp_get_free_heap_size();
size_t min_free_heap = esp_get_minimum_free_heap_size();

ESP_LOGI(TAG, "Free heap: %d bytes", free_heap);
ESP_LOGI(TAG, "Min free heap: %d bytes", min_free_heap);

// 查看内部 RAM
size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
ESP_LOGI(TAG, "Free internal RAM: %d bytes", free_internal);

// 内存泄漏检测
heap_caps_check_integrity_all(true);
```

## 任务调试

```c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 打印所有任务状态
void print_task_list(void)
{
    char *task_list = malloc(1024);
    if (task_list) {
        vTaskList(task_list);
        ESP_LOGI(TAG, "Task List:\n%s", task_list);
        free(task_list);
    }
}

// 查看任务堆栈使用情况
UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(NULL);
ESP_LOGI(TAG, "Stack high water mark: %d", stack_high_water);
```

## 崩溃分析

### 常见崩溃原因

1. **栈溢出** (Stack Overflow)
   ```
   Guru Meditation Error: Core 0 panic'ed (Unhandled debug exception)
   Debug exception reason: Stack canary watchpoint triggered (task_name)
   ```
   解决：增加任务栈大小

2. **空指针访问** (Null Pointer)
   ```
   Guru Meditation Error: Core 0 panic'ed (LoadProhibited)
   ```
   解决：检查指针是否为 NULL

3. **看门狗超时** (Watchdog Timeout)
   ```
   Guru Meditation Error: Core 0 panic'ed (Task watchdog got triggered)
   ```
   解决：在长时间操作中添加 `vTaskDelay()` 或 `taskYIELD()`

### 回溯分析

崩溃时会输出回溯信息：
```
Backtrace: 0x400d1234:0x3ffb1234 0x400d5678:0x3ffb1238
```

使用工具解析：
```bash
# 使用 addr2line 解析地址
xtensa-esp32-elf-addr2line -pfiaC -e build/my-project.elf 0x400d1234
```

## JTAG 调试

### 使用 OpenOCD + GDB

```bash
# 启动 OpenOCD
openocd -f board/esp32-wrover-kit.cfg

# 在另一个终端启动 GDB
xtensa-esp32-elf-gdb -x gdbinit build/my-project.elf

# GDB 常用命令
(gdb) target remote :3333    # 连接 OpenOCD
(gdb) monitor reset halt     # 复位并暂停
(gdb) load                   # 加载固件
(gdb) break app_main         # 设置断点
(gdb) continue               # 继续运行
(gdb) next                   # 单步执行
(gdb) print variable         # 打印变量
(gdb) backtrace              # 查看调用栈
```

## 性能分析

```c
#include "esp_timer.h"

// 测量代码执行时间
int64_t start = esp_timer_get_time();  // 微秒

// ... 要测量的代码 ...

int64_t end = esp_timer_get_time();
ESP_LOGI(TAG, "执行时间: %lld 微秒", end - start);
```

## 常用调试宏

```c
// 断言（调试模式下有效）
assert(ptr != NULL);
ESP_ERROR_CHECK(esp_wifi_init(&cfg));  // 检查返回值

// 条件断言
configASSERT(xHandle != NULL);

// 自定义错误检查
#define CHECK_ERROR(x) do {                                     \
    esp_err_t err = (x);                                        \
    if (err != ESP_OK) {                                        \
        ESP_LOGE(TAG, "Error: %s at %s:%d",                    \
                 esp_err_to_name(err), __FILE__, __LINE__);     \
        return err;                                             \
    }                                                           \
} while(0)
```

## 串口监视技巧

```bash
# 基本监视
idf.py -p COM3 monitor

# 过滤特定 TAG 的日志（Linux/macOS）
idf.py -p /dev/ttyUSB0 monitor | grep "MyApp"

# 保存日志到文件
idf.py -p COM3 monitor | tee debug.log
```

### monitor 快捷键

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+]` | 退出监视 |
| `Ctrl+T Ctrl+R` | 重启设备 |
| `Ctrl+T Ctrl+F` | 烧录固件 |
| `Ctrl+T Ctrl+A` | 重置到 APP 分区 |
| `Ctrl+T Ctrl+H` | 显示帮助 |