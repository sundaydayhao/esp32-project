#!/usr/bin/env python3
"""
ESP32-S3 跌倒检测系统 - 数据接收脚本
通过串口接收 CSI 数据和跌倒事件
"""

import serial
import json
import time
import argparse
from datetime import datetime
from pathlib import Path

class FallDetectionReceiver:
    def __init__(self, port, baudrate=115200, log_file=None):
        """
        初始化接收器
        
        Args:
            port: 串口端口 (例如: COM3 或 /dev/ttyUSB0)
            baudrate: 波特率
            log_file: 日志文件路径 (可选)
        """
        self.port = port
        self.baudrate = baudrate
        self.log_file = log_file
        self.ser = None
        
        # 统计信息
        self.stats = {
            'total_packets': 0,
            'csi_packets': 0,
            'event_packets': 0,
            'message_packets': 0,
            'fall_events': 0,
            'suspect_events': 0,
        }
        
    def connect(self):
        """连接串口"""
        try:
            self.ser = serial.Serial(
                self.port,
                self.baudrate,
                timeout=1,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE
            )
            print(f"✓ 已连接到 {self.port} (波特率: {self.baudrate})")
            return True
        except serial.SerialException as e:
            print(f"✗ 连接失败: {e}")
            return False
    
    def disconnect(self):
        """断开串口"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("✓ 已断开连接")
    
    def log_to_file(self, data):
        """记录数据到文件"""
        if self.log_file:
            try:
                with open(self.log_file, 'a', encoding='utf-8') as f:
                    timestamp = datetime.now().isoformat()
                    f.write(f"{timestamp} | {json.dumps(data, ensure_ascii=False)}\n")
            except Exception as e:
                print(f"✗ 写入日志失败: {e}")
    
    def handle_csi_data(self, data):
        """处理 CSI 数据"""
        self.stats['csi_packets'] += 1
        
        # 简化输出，避免刷屏
        if self.stats['csi_packets'] % 100 == 0:
            print(f"[CSI] 已接收 {self.stats['csi_packets']} 个数据包 "
                  f"(RSSI: {data.get('rssi', 'N/A')} dBm)")
    
    def handle_event(self, data):
        """处理跌倒事件"""
        self.stats['event_packets'] += 1
        
        state = data.get('state', 'UNKNOWN')
        confidence = data.get('confidence', 0.0)
        motion_before = data.get('motion_before', 'UNKNOWN')
        motion_after = data.get('motion_after', 'UNKNOWN')
        
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        if state == 'SUSPECT_FALL':
            self.stats['suspect_events'] += 1
            print(f"\n{'='*60}")
            print(f"[{timestamp}] ⚠️  疑似跌倒事件")
            print(f"  置信度: {confidence:.2%}")
            print(f"  运动前: {motion_before}")
            print(f"  运动后: {motion_after}")
            print(f"{'='*60}\n")
            
        elif state == 'CONFIRMED_FALL':
            self.stats['fall_events'] += 1
            print(f"\n{'='*60}")
            print(f"[{timestamp}] 🚨 确认跌倒事件!")
            print(f"  置信度: {confidence:.2%}")
            print(f"  运动前: {motion_before}")
            print(f"  运动后: {motion_after}")
            print(f"{'='*60}\n")
            
            # 这里可以添加告警逻辑
            # 例如: 发送短信、推送通知、触发蜂鸣器等
            self.trigger_alarm(data)
    
    def handle_message(self, data):
        """处理文本消息"""
        self.stats['message_packets'] += 1
        
        msg = data.get('msg', '')
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        print(f"[{timestamp}] 💬 {msg}")
    
    def trigger_alarm(self, event_data):
        """触发告警 (可自定义)"""
        # 示例: 打印告警信息
        print("🔔 告警已触发!")
        
        # TODO: 在这里添加实际的告警逻辑
        # 例如:
        # - 发送邮件
        # - 发送短信
        # - 推送通知到手机
        # - 触发本地蜂鸣器
        # - 调用第三方 API
        pass
    
    def process_line(self, line):
        """处理接收到的一行数据"""
        line = line.strip()
        
        # 跳过空行
        if not line:
            return
        
        # 尝试解析 JSON
        if line.startswith('{'):
            try:
                data = json.loads(line)
                self.stats['total_packets'] += 1
                
                # 记录到文件
                self.log_to_file(data)
                
                # 根据类型分发处理
                data_type = data.get('type', 'unknown')
                
                if data_type == 'csi':
                    self.handle_csi_data(data)
                elif data_type == 'event':
                    self.handle_event(data)
                elif data_type == 'message':
                    self.handle_message(data)
                else:
                    print(f"⚠️  未知数据类型: {data_type}")
                    
            except json.JSONDecodeError as e:
                print(f"✗ JSON 解析错误: {e}")
                print(f"  原始数据: {line}")
        else:
            # 非 JSON 数据，直接输出 (可能是日志)
            print(line)
    
    def print_stats(self):
        """打印统计信息"""
        print(f"\n{'='*60}")
        print("统计信息:")
        print(f"  总数据包: {self.stats['total_packets']}")
        print(f"  CSI 数据包: {self.stats['csi_packets']}")
        print(f"  事件数据包: {self.stats['event_packets']}")
        print(f"  消息数据包: {self.stats['message_packets']}")
        print(f"  疑似跌倒: {self.stats['suspect_events']}")
        print(f"  确认跌倒: {self.stats['fall_events']}")
        print(f"{'='*60}\n")
    
    def run(self):
        """主循环"""
        if not self.connect():
            return
        
        print("开始接收数据... (按 Ctrl+C 停止)\n")
        
        last_stats_time = time.time()
        
        try:
            while True:
                if self.ser.in_waiting > 0:
                    try:
                        line = self.ser.readline().decode('utf-8', errors='ignore')
                        self.process_line(line)
                    except UnicodeDecodeError:
                        pass
                
                # 每 30 秒打印一次统计信息
                if time.time() - last_stats_time > 30:
                    self.print_stats()
                    last_stats_time = time.time()
                    
        except KeyboardInterrupt:
            print("\n\n收到停止信号...")
        except Exception as e:
            print(f"\n✗ 错误: {e}")
        finally:
            self.print_stats()
            self.disconnect()

def main():
    parser = argparse.ArgumentParser(
        description='ESP32-S3 跌倒检测系统数据接收器',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python receive_data.py -p COM3
  python receive_data.py -p /dev/ttyUSB0 -b 115200
  python receive_data.py -p COM3 -l fall_detection.log
        """
    )
    
    parser.add_argument('-p', '--port', required=True,
                        help='串口端口 (例如: COM3 或 /dev/ttyUSB0)')
    parser.add_argument('-b', '--baudrate', type=int, default=115200,
                        help='波特率 (默认: 115200)')
    parser.add_argument('-l', '--log', dest='log_file',
                        help='日志文件路径 (可选)')
    
    args = parser.parse_args()
    
    # 创建日志目录
    if args.log_file:
        log_path = Path(args.log_file)
        log_path.parent.mkdir(parents=True, exist_ok=True)
    
    # 创建接收器并运行
    receiver = FallDetectionReceiver(
        port=args.port,
        baudrate=args.baudrate,
        log_file=args.log_file
    )
    
    receiver.run()

if __name__ == '__main__':
    main()