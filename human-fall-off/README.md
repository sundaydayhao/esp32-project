# ESP32-S3 人体跌倒检测系统

基于 WiFi CSI (Channel State Information) 实现的非接触式老人跌倒监测系统。

## 项目概述

本项目使用 ESP32-S3 的 WiFi CSI 功能，通过分析无线信道状态信息的变化来检测人体跌倒事件。这是一个 V1 原型系统，专注于单房间、单人场景下的跌倒检测验证。

### 主要特性

- ✅ 非接触式检测，无需穿戴设备
- ✅ 基于 WiFi CSI 的人体活动感知
- ✅ 运动门控机制，过滤空闲场景
- ✅ 两阶段跌倒确认（疑似 → 确认）
- ✅ 实时数据上报（UART/UDP）
- ✅ 模块化设计，易于扩展

## 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                     ESP32-S3 主控                        │
├─────────────────────────────────────────────────────────┤
│  WiFi CSI 采集层                                         │
│  ├─ CSI 数据采集                                         │
│  ├─ 数据队列管理                                         │
│  └─ 统计信息                                             │
├─────────────────────────────────────────────────────────┤
│  运动门控层                                              │
│  ├─ 幅度特征提取                                         │
│  ├─ 方差计算                                             │
│  └─ 活动/空闲判断                                        │
├─────────────────────────────────────────────────────────┤
│  事件检测层                                              │
│  ├─ 状态转换分析                                         │
│  ├─ 跌倒候选识别                                         │
│  └─ 事后确认机制                                         │
├─────────────────────────────────────────────────────────┤
│  数据上报层                                              │
│  ├─ UART 串口输出                                        │
│  ├─ UDP 网络上报                                         │
│  └─ JSON 格式化                                          │
└─────────────────────────────────────────────────────────┘
```

## 硬件要求

### 必需硬件

- **ESP32-S3 开发板** x 2（推荐带 PSRAM 和外置天线接口）
- **2.4GHz WiFi 路由器** x 1
- **外置天线** x 2（可选，但推荐）
- **USB 数据线** x 2
- **PC** x 1（用于数据接收和分析）

### 推荐配置

- ESP32-S3-DevKitC-1 或类似开发板
- 支持 IPEX 天线接口
- 至少 2MB PSRAM
- 稳定的 5V 电源供电

## 软件依赖

- **ESP-IDF**: v5.0 或更高版本
- **Python**: 3.7+ (用于数据接收脚本)
- **Git**: 用于克隆项目

## 快速开始

### 1. 环境准备

```bash
# 安装 ESP-IDF (如果尚未安装)
# 参考: https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/get-started/

# 设置 ESP-IDF 环境变量
. $HOME/esp/esp-idf/export.sh  # Linux/macOS
# 或
%userprofile%\esp\esp-idf\export.bat  # Windows
```

### 2. 配置项目

```bash
cd human-fall-off

# 配置 WiFi 参数
idf.py menuconfig
# 导航到: Component config -> WiFi Configuration
# 设置 SSID 和密码
```

或直接编辑 `sdkconfig.defaults`:

```ini
CONFIG_WIFI_SSID="YourWiFiSSID"
CONFIG_WIFI_PASSWORD="YourWiFiPassword"
CONFIG_WIFI_CHANNEL=6
```

### 3. 编译和烧录

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 编译项目
idf.py build

# 烧录到设备 (两块板子都需要烧录)
idf.py -p COM3 flash monitor  # Windows
# 或
idf.py -p /dev/ttyUSB0 flash monitor  # Linux
```

### 4. 部署建议

```
房间布局示例:

    墙壁
    ┌─────────────────────────────────┐
    │                                 │
    │  [ESP32-S3 #1]                  │
    │   (RX 接收端)                   │
    │        ↓                        │
    │        ↓  WiFi CSI 链路         │
    │        ↓                        │
    │   [监测区域]                    │
    │   (床边/沙发)                   │
    │        ↓                        │
    │        ↓                        │
    │  [ESP32-S3 #2]                  │
    │   (TX 发送端)                   │
    │                                 │
    └─────────────────────────────────┘

部署参数:
- 节点间距: 3-6 米
- 安装高度: 0.8-1.2 米
- 天线方向: 垂直向上
- 避免金属遮挡
```

## 配置参数

### WiFi CSI 配置

```c
wifi_csi_config_t csi_config = {
    .ssid = "YourSSID",
    .password = "YourPassword",
    .channel = 6,              // WiFi 信道 (1-13)
    .enable_csi = true,
};
```

### 运动门控配置

```c
motion_gate_config_t motion_config = {
    .threshold = 100.0f,       // 运动检测阈值
    .window_size = 50,         // 滑动窗口大小
};
```

### 事件检测配置

```c
event_detector_config_t event_config = {
    .fall_threshold = 0.7f,    // 跌倒检测阈值 (0.0-1.0)
    .confirm_duration_ms = 5000, // 确认持续时间 (毫秒)
};
```

## 数据格式

### CSI 数据 (JSON)

```json
{
  "type": "csi",
  "ts": 1234567890,
  "rssi": -45,
  "len": 128,
  "data": [
    {"i": 10, "q": -5},
    {"i": 8, "q": -3},
    ...
  ]
}
```

### 事件数据 (JSON)

```json
{
  "type": "event",
  "ts": 1234567890,
  "state": "CONFIRMED_FALL",
  "confidence": 0.85,
  "motion_before": "ACTIVE",
  "motion_after": "IDLE"
}
```

## Python 数据接收脚本

创建 `receive_data.py`:

```python
#!/usr/bin/env python3
import serial
import json
import time
from datetime import datetime

# 配置串口
SERIAL_PORT = 'COM3'  # 根据实际情况修改
BAUD_RATE = 115200

def main():
    print(f"连接到 {SERIAL_PORT}...")
    
    with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1) as ser:
        print("开始接收数据...")
        
        while True:
            try:
                line = ser.readline().decode('utf-8').strip()
                
                if line.startswith('{'):
                    data = json.loads(line)
                    
                    if data['type'] == 'event':
                        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
                        print(f"\n[{timestamp}] 🚨 跌倒事件!")
                        print(f"  状态: {data['state']}")
                        print(f"  置信度: {data['confidence']:.2f}")
                        print(f"  运动前: {data['motion_before']}")
                        print(f"  运动后: {data['motion_after']}")
                        
                        # 这里可以添加告警逻辑
                        # 例如: 发送短信、推送通知等
                        
            except json.JSONDecodeError:
                pass
            except KeyboardInterrupt:
                print("\n停止接收")
                break
            except Exception as e:
                print(f"错误: {e}")

if __name__ == '__main__':
    main()
```

运行脚本:

```bash
python receive_data.py
```

## 调试和测试

### 查看日志

```bash
idf.py monitor
```

### 常见日志输出

```
I (1234) FALL_DETECT: WiFi 已连接
I (1235) WIFI_CSI: CSI 数据采集已启动
I (1236) MOTION_GATE: 基线幅度初始化: 45.23
I (5678) MOTION_GATE: 运动状态变化: 空闲 -> 活动 (强度: 156.78)
W (6789) EVENT_DETECT: ⚠️  检测到疑似跌倒事件 (置信度: 0.60)
E (11789) EVENT_DETECT: 🚨 确认跌倒事件! (置信度: 0.90)
```

### 性能监控

系统每 10 秒输出一次状态信息:

```
I (10000) FALL_DETECT: === 系统状态 ===
I (10000) FALL_DETECT: 空闲堆内存: 245678 bytes
I (10000) FALL_DETECT: 最小空闲堆: 198765 bytes
I (10000) FALL_DETECT: 运动状态: 活动中
I (10000) FALL_DETECT: 事件状态: 正常
```

## 项目结构

```
human-fall-off/
├── CMakeLists.txt              # 项目构建配置
├── sdkconfig.defaults          # 默认配置
├── README.md                   # 本文档
├── ESP32-S3-跌倒检测原型方案.md # 详细技术方案
└── main/
    ├── CMakeLists.txt          # 主组件配置
    ├── main.c                  # 主程序入口
    ├── wifi_csi.h/c            # WiFi CSI 采集模块
    ├── motion_gate.h/c         # 运动门控模块
    ├── event_detector.h/c      # 事件检测模块
    └── data_uplink.h/c         # 数据上报模块
```

## 已知限制

### V1 版本限制

- ✗ 仅支持单房间、单人场景
- ✗ 需要固定部署位置
- ✗ 对环境变化敏感（家具移动、门窗开关）
- ✗ 可能误报（快速坐下、躺下）
- ✗ 需要初始化基线（启动后约 10 秒）

### 环境要求

- 需要稳定的 WiFi 环境
- 避免金属物体遮挡
- 避免强电磁干扰
- 固定信道，避免信道切换

## 后续改进方向

### 算法优化

- [ ] 引入机器学习模型（XGBoost/RandomForest）
- [ ] 添加相位信息分析
- [ ] 多特征融合
- [ ] 自适应阈值调整

### 功能扩展

- [ ] 多人场景支持
- [ ] 全屋漫游检测
- [ ] 移动端 App
- [ ] 云端数据分析
- [ ] 历史记录查询

### 硬件优化

- [ ] 添加蜂鸣器告警
- [ ] LED 状态指示
- [ ] 按钮手动复位
- [ ] 电池供电支持

## 故障排除

### WiFi 连接失败

```
检查项:
1. SSID 和密码是否正确
2. 路由器是否支持 2.4GHz
3. 信号强度是否足够
4. 信道设置是否正确
```

### CSI 数据异常

```
检查项:
1. 天线是否正确连接
2. 两个节点是否都在运行
3. 节点间距是否合适
4. 是否有金属遮挡
```

### 误报率高

```
调整建议:
1. 增大 fall_threshold 值
2. 增加 confirm_duration_ms
3. 调整节点位置和方向
4. 重新采集基线数据
```

## 参考资料

- [ESP-IDF 编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/)
- [ESP32-S3 WiFi CSI 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/wifi.html#wi-fi-channel-state-information)
- [项目技术方案](./ESP32-S3-跌倒检测原型方案.md)

## 许可证

本项目仅供学习和研究使用。

## 贡献

欢迎提交 Issue 和 Pull Request。

## 联系方式

如有问题，请通过 Issue 反馈。

---

**注意**: 本系统为原型验证版本，不应用于医疗级应用。实际部署前需要进行充分的测试和验证。