# A型桌面屏 · 餐厅菜单显示控制器

> 目标用户：小餐馆、奶茶店、快餐店  
> 硬件：ESP32-S3 + 7寸 800×480 SPI 屏  
> 控制方式：手机连同一 WiFi，浏览器打开控制页面

---

## 功能特性

- 📋 **菜单管理**：最多 16 个菜品，支持名称 + 价格
- 📢 **促销横幅**：底部滚动显示促销文字
- 🕐 **实时时钟**：右上角显示当前时间
- 🎨 **主题切换**：4 种配色方案（红/蓝/绿/橙）
- 📱 **手机控制**：浏览器访问，无需安装 App
- 🔌 **断网可用**：内容存储在设备本地，断网不黑屏
- 🌐 **mDNS 发现**：访问 `http://menu-display.local` 无需记 IP

---

## 硬件接线

### ESP32-S3 引脚定义

| 功能 | ESP32-S3 引脚 | 屏幕引脚 |
|------|--------------|---------|
| SPI MOSI | GPIO 11 | SDI/MOSI |
| SPI SCLK | GPIO 12 | SCK/CLK |
| SPI CS | GPIO 10 | CS |
| DC/RS | GPIO 13 | DC/RS |
| RST | GPIO 14 | RST |
| BL（背光） | GPIO 15 | BL |
| 电源 | 3.3V | VCC |
| 地 | GND | GND |

> 屏幕驱动芯片：ILI9488（7寸 800×480 SPI）

---

## 快速开始

### 1. 编译烧录

```bash
# 安装 ESP-IDF v5.1+
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### 2. 首次配置 WiFi

设备首次启动时，自动开启 AP 热点：
- **热点名称**：`MenuDisplay-XXXX`（XXXX 为设备 MAC 后4位）
- **热点密码**：`12345678`

手机连接热点后，访问 `http://192.168.4.1`，输入餐厅 WiFi 的账号密码。

### 3. 正常使用

设备连上餐厅 WiFi 后：
- 手机连同一 WiFi
- 浏览器访问 `http://menu-display.local`
- 或访问设备 IP（屏幕右下角显示）

### 4. 管理菜单

在控制页面：
1. 输入餐厅名称
2. 添加菜品（名称 + 价格）
3. 输入促销文字
4. 选择主题颜色
5. 点击「保存并更新」

屏幕 **立即刷新**，无需等待。

---

## 显示布局

```
┌──────────────────────────────────────────────┐
│  🍜 小王快餐                        14:30    │  ← 标题栏
├──────────────────────────────────────────────┤
│  红烧肉饭    ¥18  │  鱼香肉丝饭   ¥16       │
│  番茄鸡蛋饭  ¥12  │  宫保鸡丁饭   ¥18       │  ← 菜单区
│  麻婆豆腐饭  ¥14  │  回锅肉饭     ¥20       │
│  蛋炒饭      ¥10  │  扬州炒饭     ¥15       │
├──────────────────────────────────────────────┤
│  🎉 今日特价：红烧肉饭 + 汤 只要 ¥20！      │  ← 促销栏
└──────────────────────────────────────────────┘
```

---

## 项目结构

```
display_controller_a/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── README.md
└── main/
    ├── CMakeLists.txt
    ├── config.h              # 引脚定义、参数配置
    ├── main.c                # 程序入口
    ├── display/
    │   ├── lcd_driver.c/h    # ILI9488 SPI 驱动
    │   └── display_manager.c/h  # 显示任务、布局渲染
    ├── web/
    │   ├── web_server.c/h    # HTTP 控制服务器
    │   └── web_ui.h          # 内嵌 HTML 控制页面
    ├── wifi/
    │   └── wifi_manager.c/h  # WiFi STA + AP 配置
    └── storage/
        └── content_store.c/h # NVS 菜单内容存储
```

---

## API 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` | 控制页面 HTML |
| GET | `/api/content` | 获取当前菜单内容（JSON） |
| POST | `/api/content` | 更新菜单内容（JSON） |
| GET | `/api/status` | 设备状态（IP、版本、运行时间） |
| POST | `/api/wifi` | 配置 WiFi（首次设置） |
| POST | `/api/reset` | 恢复出厂设置 |

### 内容 JSON 格式

```json
{
  "restaurant_name": "小王快餐",
  "theme": 0,
  "promo_text": "今日特价：红烧肉饭+汤 ¥20",
  "items": [
    {"name": "红烧肉饭", "price": "18"},
    {"name": "鱼香肉丝饭", "price": "16"}
  ]
}
```

---

*硬件成本：ESP32-S3 ¥18 + 7寸屏 ¥35 + 外壳 ¥15 + 其他 ¥12 = **¥80***