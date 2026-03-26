# ESP32-S3 智能快递柜 (Smart Parcel Locker)

基于 ESP32-S3 和 ESP-IDF 开发的智能快递柜系统，支持键盘操作、OLED 显示、WiFi 远程管理。

---

## 功能特性

- **4个独立格口**，每个格口配备舵机锁、状态LED、门磁传感器
- **4x4 矩阵键盘**，支持取件码输入
- **SSD1306 OLED 显示屏** (128x64)，实时显示格口状态和操作界面
- **WiFi 双模式**：AP 热点模式 + STA 连接模式
- **HTTP REST API**，支持远程管理
- **内嵌 Web 管理界面**，浏览器直接访问
- **NVS 持久化存储**，断电后状态恢复
- **蜂鸣器音效**反馈
- **安全机制**：多次错误自动锁定

---

## 硬件连接

| 功能         | 引脚                              |
|------------|-----------------------------------|
| 舵机 (锁)   | GPIO 1, 2, 4, 5                   |
| 键盘行       | GPIO 6, 7, 8, 9                   |
| 键盘列       | GPIO 10, 11, 12, 13               |
| OLED SDA   | GPIO 21                           |
| OLED SCL   | GPIO 22                           |
| 状态 LED    | GPIO 14, 15, 16, 17               |
| 蜂鸣器      | GPIO 18                           |
| 门磁传感器  | GPIO 38, 39, 40, 41               |

> 所有引脚可在 `main/config.h` 中修改

---

## 键盘布局

```
[1][2][3][A]
[4][5][6][B]
[7][8][9][C]
[*][0][#][D]
```

| 按键 | 功能                    |
|------|------------------------|
| `#`  | 确认 / 进入取件模式      |
| `*`  | 删除 / 取消             |
| `A`  | 存件模式 (快递员)        |
| `D`  | 管理员模式 (显示WiFi信息) |
| `B`  | 返回上一级              |

---

## 操作流程

### 取件流程
1. 待机界面按 `#` 键
2. 输入 6 位取件码
3. 再次按 `#` 确认
4. 验证成功 → 对应格口自动解锁
5. 取出包裹，关门
6. 系统自动锁定，返回待机

### 存件流程 (快递员)
1. 待机界面按 `A` 键
2. 系统自动选择空闲格口并解锁
3. 屏幕显示 6 位取件码
4. 放入包裹，关门
5. 系统自动锁定
6. 将取件码告知收件人

### 管理员操作
1. 待机界面按 `D` 键，查看 WiFi 信息
2. 连接到显示的 WiFi，访问 Web 管理界面

---

## HTTP REST API

### 基础 URL
- **AP 模式**: `http://192.168.4.1` (连接 WiFi: `SmartLocker_AP`, 密码: `admin1234`)
- **STA 模式**: `http://<设备IP>`

### 端点列表

| 方法   | 路径                          | 说明                    |
|--------|-------------------------------|------------------------|
| GET    | `/`                           | Web 管理界面            |
| GET    | `/api/status`                 | 系统状态                |
| GET    | `/api/lockers`                | 所有格口状态            |
| GET    | `/api/lockers/{id}`           | 指定格口状态            |
| POST   | `/api/lockers/{id}/deposit`   | 存件 (分配格口+取件码)  |
| POST   | `/api/lockers/*/pickup`       | 取件 (验证取件码)       |
| POST   | `/api/lockers/{id}/unlock`    | 强制解锁 (管理员)       |
| POST   | `/api/lockers/{id}/lock`      | 强制锁定 (管理员)       |
| POST   | `/api/lockers/{id}/reset`     | 重置格口 (管理员)       |
| POST   | `/api/wifi`                   | 配置 WiFi               |

### API 示例

**存件 (自动选择格口)**
```bash
curl -X POST http://192.168.4.1/api/lockers/deposit \
  -H "Content-Type: application/json" \
  -d '{}'
# 响应: {"status":"ok","locker_id":0,"code":"123456","message":"..."}
```

**取件**
```bash
curl -X POST http://192.168.4.1/api/lockers/0/pickup \
  -H "Content-Type: application/json" \
  -d '{"code":"123456"}'
# 响应: {"status":"ok","locker_id":0,"message":"Pickup successful..."}
```

**配置 WiFi**
```bash
curl -X POST http://192.168.4.1/api/wifi \
  -H "Content-Type: application/json" \
  -d '{"ssid":"MyWiFi","password":"mypassword"}'
```

---

## 编译与烧录

### 环境要求
- ESP-IDF v5.x
- Python 3.8+

### 编译步骤

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 配置 (可选)
idf.py menuconfig

# 编译
idf.py build

# 烧录
idf.py -p COM3 flash

# 监视串口
idf.py -p COM3 monitor

# 一键编译+烧录+监视
idf.py -p COM3 flash monitor
```

### VS Code 使用
安装 **ESP-IDF** 扩展后，可直接在 VS Code 中编译和烧录。

---

## 项目结构

```
esp32-delivery/
├── CMakeLists.txt          # 顶层 CMake
├── sdkconfig.defaults      # 默认 SDK 配置
├── partitions.csv          # 分区表
└── main/
    ├── CMakeLists.txt      # 组件 CMake
    ├── config.h            # 硬件引脚与参数配置
    ├── main.c              # 主程序入口 + UI 状态机
    ├── storage.h/c         # NVS 持久化存储
    ├── buzzer.h/c          # 蜂鸣器驱动
    ├── locker.h/c          # 格口管理 (舵机+门磁+状态机)
    ├── keypad.h/c          # 4x4 矩阵键盘驱动
    ├── ssd1306.h/c         # SSD1306 OLED 驱动
    ├── display.h/c         # UI 界面管理
    ├── wifi_manager.h/c    # WiFi 管理 (AP+STA)
    └── http_server.h/c     # HTTP REST API 服务器
```

---

## 格口状态说明

| 状态        | 说明                          | LED  |
|-------------|-------------------------------|------|
| `empty`     | 空闲，可存件                  | 灭   |
| `occupied`  | 已存件，等待取件              | 亮   |
| `unlocked`  | 已解锁，门已打开              | 灭   |
| `locked_out`| 因多次错误被临时锁定          | 闪烁 |

---

## 安全特性

- 连续输错 **3次** 取件码后，格口锁定 **60秒**
- 解锁后 **30秒** 无操作自动关锁
- 取件码存储在 NVS Flash，断电不丢失
- WiFi 配置加密存储

---

## 许可证

MIT License