@echo off
REM AI玩具 ESP32-S3 烧录脚本 (Windows)
REM 使用方法: flash.bat [串口号]
REM 示例: flash.bat COM3

setlocal

set PORT=%1
if "%PORT%"=="" (
    echo 用法: flash.bat [串口号]
    echo 示例: flash.bat COM3
    echo.
    echo 可用串口:
    mode | findstr "COM"
    exit /b 1
)

echo ========================================
echo  AI玩具 ESP32-S3 烧录工具
echo  串口: %PORT%
echo ========================================

cd /d "%~dp0..\firmware"

echo [1/3] 编译固件...
idf.py build
if errorlevel 1 (
    echo 编译失败！
    exit /b 1
)

echo [2/3] 烧录固件到 %PORT%...
idf.py -p %PORT% flash
if errorlevel 1 (
    echo 烧录失败！请检查串口连接。
    exit /b 1
)

echo [3/3] 打开串口监视器...
echo 按 Ctrl+] 退出监视器
idf.py -p %PORT% monitor

endlocal