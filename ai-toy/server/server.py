"""
AI玩具 WebSocket 后端服务器

处理来自ESP32-S3的WebSocket连接，协调ASR/LLM/TTS流程

协议说明:
  客户端 → 服务器 (JSON):
    {"type": "hello", "device_id": "xxx", "version": "1.0"}
    {"type": "listen_start"}
    {"type": "listen_stop"}
    {"type": "abort"}
  
  客户端 → 服务器 (Binary):
    PCM音频数据 (16bit, 16kHz, 单声道)
  
  服务器 → 客户端 (JSON):
    {"type": "hello", "session_id": "xxx"}
    {"type": "asr_result", "text": "识别文字"}
    {"type": "llm_response", "text": "AI回复", "done": false}
    {"type": "tts_start", "sample_rate": 16000}
    {"type": "tts_end"}
    {"type": "error", "message": "错误信息"}
  
  服务器 → 客户端 (Binary):
    TTS PCM音频数据
"""

import asyncio
import json
import logging
import uuid
import time
from typing import Dict, Optional

import websockets
from websockets.server import WebSocketServerProtocol

import config
from asr_handler import ASRHandler
from llm_handler import LLMHandler
from tts_handler import TTSHandler

# 配置日志
logging.basicConfig(
    level=getattr(logging, config.LOG_LEVEL, logging.INFO),
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger("SERVER")


class DeviceSession:
    """设备会话，管理单个ESP32设备的连接状态"""

    def __init__(self, websocket: WebSocketServerProtocol, device_id: str):
        self.websocket = websocket
        self.device_id = device_id
        self.session_id = str(uuid.uuid4())[:8]
        self.connected_at = time.time()

        # 状态
        self.is_listening = False
        self.is_aborted = False

        # 音频缓冲区 (录音时积累PCM数据)
        self.audio_buffer = bytearray()

        logger.info(f"[{self.device_id}] 新会话: {self.session_id}")

    async def send_json(self, data: dict):
        """发送JSON消息"""
        try:
            msg = json.dumps(data, ensure_ascii=False)
            await self.websocket.send(msg)
            logger.debug(f"[{self.device_id}] → {msg[:100]}")
        except Exception as e:
            logger.error(f"[{self.device_id}] 发送JSON失败: {e}")

    async def send_binary(self, data: bytes):
        """发送二进制数据"""
        try:
            await self.websocket.send(data)
        except Exception as e:
            logger.error(f"[{self.device_id}] 发送二进制失败: {e}")

    def reset(self):
        """重置会话状态"""
        self.is_listening = False
        self.is_aborted = False
        self.audio_buffer.clear()


class AIToyServer:
    """AI玩具WebSocket服务器"""

    def __init__(self):
        self.asr = ASRHandler(config)
        self.llm = LLMHandler(config)
        self.tts = TTSHandler(config)
        self.sessions: Dict[str, DeviceSession] = {}

    async def handle_connection(self, websocket: WebSocketServerProtocol):
        """处理新的WebSocket连接"""
        remote = websocket.remote_address
        logger.info(f"新连接: {remote}")

        session: Optional[DeviceSession] = None

        try:
            async for message in websocket:
                if isinstance(message, str):
                    # JSON文本消息
                    await self._handle_text(websocket, session, message)
                    # 更新session引用 (hello消息后创建)
                    if session is None:
                        session = self._get_session_by_ws(websocket)
                elif isinstance(message, bytes):
                    # 二进制音频数据
                    if session and session.is_listening:
                        session.audio_buffer.extend(message)

        except websockets.exceptions.ConnectionClosed as e:
            logger.info(f"连接关闭: {remote}, 原因: {e}")
        except Exception as e:
            logger.error(f"连接处理异常: {e}", exc_info=True)
        finally:
            # 清理会话
            if session:
                self.sessions.pop(session.device_id, None)
                logger.info(f"[{session.device_id}] 会话结束")

    def _get_session_by_ws(self, websocket) -> Optional[DeviceSession]:
        """通过WebSocket对象查找会话"""
        for session in self.sessions.values():
            if session.websocket is websocket:
                return session
        return None

    async def _handle_text(self, websocket: WebSocketServerProtocol,
                            session: Optional[DeviceSession], message: str):
        """处理JSON文本消息"""
        try:
            data = json.loads(message)
        except json.JSONDecodeError:
            logger.warning(f"无效JSON: {message[:100]}")
            return

        msg_type = data.get("type", "")
        logger.debug(f"收到消息: type={msg_type}")

        if msg_type == "hello":
            # 握手
            device_id = data.get("device_id", f"unknown_{id(websocket)}")
            version = data.get("version", "unknown")

            session = DeviceSession(websocket, device_id)
            self.sessions[device_id] = session

            logger.info(f"[{device_id}] 握手成功 (版本: {version})")

            await session.send_json({
                "type": "hello",
                "session_id": session.session_id,
                "server_version": "1.0",
            })

        elif msg_type == "listen_start":
            if session:
                logger.info(f"[{session.device_id}] 开始录音")
                session.is_listening = True
                session.is_aborted = False
                session.audio_buffer.clear()

        elif msg_type == "listen_stop":
            if session and session.is_listening:
                logger.info(f"[{session.device_id}] 停止录音，开始处理...")
                session.is_listening = False

                # 异步处理音频 (不阻塞消息接收)
                audio_data = bytes(session.audio_buffer)
                session.audio_buffer.clear()

                asyncio.create_task(
                    self._process_audio(session, audio_data)
                )

        elif msg_type == "abort":
            if session:
                logger.info(f"[{session.device_id}] 收到打断指令")
                session.is_aborted = True
                session.is_listening = False
                session.audio_buffer.clear()

        else:
            logger.warning(f"未知消息类型: {msg_type}")

    async def _process_audio(self, session: DeviceSession, audio_data: bytes):
        """
        处理音频数据的完整流程:
        PCM音频 → ASR识别 → LLM对话 → TTS合成 → 发送音频
        """
        device_id = session.device_id

        if not audio_data:
            logger.warning(f"[{device_id}] 音频数据为空")
            return

        logger.info(f"[{device_id}] 开始处理音频 ({len(audio_data)} 字节, "
                    f"{len(audio_data) / config.AUDIO_SAMPLE_RATE / 2:.1f}s)")

        try:
            # ---- Step 1: ASR 语音识别 ----
            if session.is_aborted:
                return

            text = await self.asr.recognize(audio_data)

            if not text:
                logger.warning(f"[{device_id}] ASR识别结果为空")
                await session.send_json({
                    "type": "error",
                    "message": "没有识别到语音，请重试",
                })
                return

            # 发送ASR结果
            await session.send_json({
                "type": "asr_result",
                "text": text,
            })

            if session.is_aborted:
                return

            # ---- Step 2: LLM 对话 ----
            logger.info(f"[{device_id}] 开始LLM对话: {text}")

            full_llm_response = ""
            async for chunk in self.llm.chat_stream(device_id, text):
                if session.is_aborted:
                    break
                full_llm_response += chunk
                # 发送流式LLM回复
                await session.send_json({
                    "type": "llm_response",
                    "text": chunk,
                    "done": False,
                })

            if session.is_aborted:
                return

            # 发送LLM完成信号
            await session.send_json({
                "type": "llm_response",
                "text": "",
                "done": True,
            })

            if not full_llm_response:
                return

            # ---- Step 3: TTS 语音合成 ----
            logger.info(f"[{device_id}] 开始TTS合成: {full_llm_response[:50]}...")

            # 发送TTS开始信号
            await session.send_json({
                "type": "tts_start",
                "sample_rate": config.TTS_OUTPUT_SAMPLE_RATE,
            })

            # 流式发送TTS音频
            total_bytes = 0
            async for audio_chunk in self.tts.synthesize_stream(full_llm_response):
                if session.is_aborted:
                    break
                await session.send_binary(audio_chunk)
                total_bytes += len(audio_chunk)

            if not session.is_aborted:
                # 发送TTS结束信号
                await session.send_json({"type": "tts_end"})
                logger.info(f"[{device_id}] TTS完成，发送 {total_bytes} 字节音频")

        except Exception as e:
            logger.error(f"[{device_id}] 处理音频失败: {e}", exc_info=True)
            try:
                await session.send_json({
                    "type": "error",
                    "message": f"处理失败: {str(e)[:100]}",
                })
            except Exception:
                pass

    async def start(self):
        """启动WebSocket服务器"""
        logger.info(f"启动AI玩具服务器: ws://{config.SERVER_HOST}:{config.SERVER_PORT}")
        logger.info(f"  ASR提供商: {config.ASR_PROVIDER}")
        logger.info(f"  LLM提供商: {config.LLM_PROVIDER} ({config.OPENAI_MODEL if config.LLM_PROVIDER == 'openai' else ''})")
        logger.info(f"  TTS提供商: {config.TTS_PROVIDER}")

        async with websockets.serve(
            self.handle_connection,
            config.SERVER_HOST,
            config.SERVER_PORT,
            max_size=10 * 1024 * 1024,  # 最大消息10MB
            ping_interval=30,
            ping_timeout=10,
        ):
            logger.info("服务器已启动，等待设备连接...")
            await asyncio.Future()  # 永久运行


def main():
    """主入口"""
    server = AIToyServer()
    try:
        asyncio.run(server.start())
    except KeyboardInterrupt:
        logger.info("服务器已停止")


if __name__ == "__main__":
    main()