# ESP-IDF 开发环境搭建

## Windows 环境搭建

### 方法一：使用官方安装器（推荐）

1. 下载 [ESP-IDF Tools Installer](https://dl.espressif.com/dl/esp-idf/)
2. 运行安装程序，选择 ESP-IDF 版本（推荐 v5.2+）
3. 安装完成后，桌面会出现 "ESP-IDF Command Prompt" 快捷方式
4. 打开该命令提示符，即可使用 `idf.py` 命令

### 方法二：手动安装

```powershell
# 1. 安装 Git
# 下载: https://git-scm.com/download/win

# 2. 安装 Python 3.8+
# 下载: https://www.python.org/downloads/

# 3. 克隆 ESP-IDF
git clone --recursive https://github.com/espressif/esp-idf.git C:\esp\esp-idf
cd C:\esp\esp-idf

# 4. 安装工具链
install.bat esp32

# 5. 设置环境变量（每次使用前执行）
export.bat
```

## Linux 环境搭建

```bash
# 1. 安装依赖
sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

# 2. 克隆 ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git

# 3. 安装工具链
cd ~/esp/esp-idf
./install.sh esp32

# 4. 设置环境变量（每次使用前执行，或添加到 .bashrc）
. ~/esp/esp-idf/export.sh

# 添加到 .bashrc（可选，自动加载）
echo '. ~/esp/esp-idf/export.sh' >> ~/.bashrc
```

## macOS 环境搭建

```bash
# 1. 安装 Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 2. 安装依赖
brew install cmake ninja dfu-util python3

# 3. 克隆 ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git

# 4. 安装工具链
cd ~/esp/esp-idf
./install.sh esp32

# 5. 设置环境变量
. ~/esp/esp-idf/export.sh
```

## VS Code 插件配置

推荐安装 **ESP-IDF Extension for VS Code**：

1. 打开 VS Code
2. 按 `Ctrl+Shift+X` 打开扩展市场
3. 搜索 "ESP-IDF"
4. 安装 "Espressif IDF" 插件
5. 按 `Ctrl+Shift+P`，输入 "ESP-IDF: Configure ESP-IDF Extension"
6. 选择 ESP-IDF 安装路径

### VS Code 常用命令

| 命令 | 功能 |
|------|------|
| `ESP-IDF: Build your project` | 编译项目 |
| `ESP-IDF: Flash your project` | 烧录固件 |
| `ESP-IDF: Monitor your device` | 串口监视 |
| `ESP-IDF: Build, Flash and start a monitor` | 一键编译烧录监视 |
| `ESP-IDF: SDK Configuration editor` | 打开 menuconfig |

## 验证安装

```bash
# 检查 idf.py 版本
idf.py --version

# 应输出类似：
# ESP-IDF v5.2.1

# 检查工具链
xtensa-esp32-elf-gcc --version
```

## 创建第一个项目

```bash
# 方法一：从模板创建
idf.py create-project my-first-project
cd my-first-project

# 方法二：复制示例
cp -r $IDF_PATH/examples/get-started/hello_world my-hello-world
cd my-hello-world

# 设置目标
idf.py set-target esp32

# 编译
idf.py build

# 烧录（替换端口号）
idf.py -p COM3 flash monitor    # Windows
idf.py -p /dev/ttyUSB0 flash monitor  # Linux
```

## 常见问题

### 问题1：找不到串口设备

**Windows**: 安装 CP210x 或 CH340 驱动
- CP210x: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
- CH340: http://www.wch.cn/downloads/CH341SER_EXE.html

**Linux**: 添加用户到 dialout 组
```bash
sudo usermod -a -G dialout $USER
# 重新登录后生效
```

### 问题2：编译失败 - Python 版本问题

```bash
# 确认 Python 版本 >= 3.8
python3 --version

# 重新安装 Python 依赖
pip install -r $IDF_PATH/requirements.txt
```

### 问题3：Flash 失败

```bash
# 检查串口权限（Linux）
ls -la /dev/ttyUSB0

# 降低波特率
idf.py -p /dev/ttyUSB0 -b 115200 flash

# 手动进入下载模式
# 按住 BOOT 键，再按 EN 键，松开 EN，再松开 BOOT
```

### 问题4：menuconfig 中文乱码

```bash
# 设置终端编码
export LANG=zh_CN.UTF-8
# 或使用英文界面
export LANG=en_US.UTF-8
```

## 项目配置 (menuconfig)

```bash
idf.py menuconfig
```

常用配置项：
- `Component config → ESP32-specific → CPU frequency` - CPU 频率
- `Component config → FreeRTOS → Tick rate` - FreeRTOS 时钟频率
- `Component config → Log output → Default log verbosity` - 日志级别
- `Serial flasher config → Flash size` - Flash 大小