#!/bin/bash
# AI玩具 ESP32-S3 烧录脚本 (Linux/macOS)
# 使用方法: ./flash.sh [串口]
# 示例: ./flash.sh /dev/ttyUSB0

set -e

PORT=${1:-/dev/ttyUSB0}

echo "========================================"
echo "  AI玩具 ESP32-S3 烧录工具"
echo "  串口: $PORT"
echo "========================================"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/../firmware"

echo "[1/3] 编译固件..."
idf.py build

echo "[2/3] 烧录固件到 $PORT..."
idf.py -p "$PORT" flash

echo "[3/3] 打开串口监视器..."
echo "按 Ctrl+] 退出监视器"
idf.py -p "$PORT" monitor