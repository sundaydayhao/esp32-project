"""
TTS (文字转语音) 处理模块

支持多种TTS提供商:
- Edge TTS (微软免费，推荐)
- 百度智能云
- 阿里云
- OpenAI TTS
"""

import asyncio
import logging
import io
import struct
from typing import AsyncGenerator, Optional

logger = logging.getLogger(__name__)


async def mp3_to_pcm(mp3_data: bytes, target_sample_rate: int = 16000) -> bytes:
    """将MP3数据转换为PCM格式 (需要pydub或ffmpeg)"""
    try:
        from pydub import AudioSegment
        import io

        audio = AudioSegment.from_mp3(io.BytesIO(mp3_data))
        audio = audio.set_frame_rate(target_sample_rate)
        audio = audio.set_channels(1)
        audio = audio.set_sample_width(2)  # 16bit
        return audio.raw_data
    except ImportError:
        logger.warning("pydub未安装，尝试使用ffmpeg直接转换")
        return await _ffmpeg_to_pcm(mp3_data, target_sample_rate)


async def _ffmpeg_to_pcm(audio_data: bytes, target_sample_rate: int = 16000) -> bytes:
    """使用ffmpeg转换音频格式"""
    import subprocess
    import tempfile
    import os

    with tempfile.NamedTemporaryFile(suffix=".mp3", delete=False) as f:
        f.write(audio_data)
        input_file = f.name

    output_file = input_file.replace(".mp3", ".pcm")

    try:
        proc = await asyncio.create_subprocess_exec(
            "ffmpeg", "-y", "-i", input_file,
            "-f", "s16le", "-ar", str(target_sample_rate), "-ac", "1",
            output_file,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.DEVNULL,
        )
        await proc.wait()

        with open(output_file, "rb") as f:
            return f.read()
    finally:
        os.unlink(input_file)
        if os.path.exists(output_file):
            os.unlink(output_file)


class EdgeTTS:
    """微软 Edge TTS (免费，无需API Key)"""

    def __init__(self, voice: str = "zh-CN-XiaoxiaoNeural"):
        self.voice = voice

    async def synthesize_stream(self, text: str,
                                 target_sample_rate: int = 16000) -> AsyncGenerator[bytes, None]:
        """流式合成语音，直接输出PCM数据"""
        try:
            import edge_tts
        except ImportError:
            raise ImportError("请安装 edge-tts: pip install edge-tts")

        # Edge TTS 生成MP3
        communicate = edge_tts.Communicate(text, self.voice)

        mp3_chunks = []
        async for chunk in communicate.stream():
            if chunk["type"] == "audio":
                mp3_chunks.append(chunk["data"])

        if not mp3_chunks:
            logger.warning("Edge TTS 未生成音频数据")
            return

        # 合并MP3数据并转换为PCM
        mp3_data = b"".join(mp3_chunks)
        pcm_data = await mp3_to_pcm(mp3_data, target_sample_rate)

        # 分块输出
        chunk_size = 4096
        for i in range(0, len(pcm_data), chunk_size):
            yield pcm_data[i:i + chunk_size]

    async def synthesize(self, text: str, target_sample_rate: int = 16000) -> bytes:
        """合成完整语音，返回PCM数据"""
        chunks = []
        async for chunk in self.synthesize_stream(text, target_sample_rate):
            chunks.append(chunk)
        return b"".join(chunks)


class BaiduTTS:
    """百度智能云 TTS"""

    def __init__(self, app_id: str, api_key: str, secret_key: str,
                 voice: int = 0, speed: int = 5, pitch: int = 5, volume: int = 5):
        self.app_id = app_id
        self.api_key = api_key
        self.secret_key = secret_key
        self.voice = voice
        self.speed = speed
        self.pitch = pitch
        self.volume = volume
        self._token = None

    async def _get_token(self) -> str:
        if self._token:
            return self._token
        import aiohttp
        url = "https://aip.baidubce.com/oauth/2.0/token"
        params = {
            "grant_type": "client_credentials",
            "client_id": self.api_key,
            "client_secret": self.secret_key,
        }
        async with aiohttp.ClientSession() as session:
            async with session.post(url, params=params) as resp:
                data = await resp.json()
                self._token = data.get("access_token", "")
                return self._token

    async def synthesize_stream(self, text: str,
                                 target_sample_rate: int = 16000) -> AsyncGenerator[bytes, None]:
        """合成语音"""
        import aiohttp
        import urllib.parse

        token = await self._get_token()
        params = {
            "tex": urllib.parse.quote(text),
            "tok": token,
            "cuid": "ai_toy_esp32s3",
            "ctp": 1,
            "lan": "zh",
            "spd": self.speed,
            "pit": self.pitch,
            "vol": self.volume,
            "per": self.voice,
            "aue": 3,  # MP3格式
        }

        url = "https://tsn.baidu.com/text2audio?" + "&".join(f"{k}={v}" for k, v in params.items())

        async with aiohttp.ClientSession() as session:
            async with session.get(url) as resp:
                if resp.content_type == "audio/mp3":
                    mp3_data = await resp.read()
                    pcm_data = await mp3_to_pcm(mp3_data, target_sample_rate)
                    chunk_size = 4096
                    for i in range(0, len(pcm_data), chunk_size):
                        yield pcm_data[i:i + chunk_size]
                else:
                    error = await resp.json()
                    logger.error(f"百度TTS错误: {error}")


class OpenAITTS:
    """OpenAI TTS"""

    def __init__(self, api_key: str, model: str = "tts-1", voice: str = "nova"):
        self.api_key = api_key
        self.model = model
        self.voice = voice

    async def synthesize_stream(self, text: str,
                                 target_sample_rate: int = 16000) -> AsyncGenerator[bytes, None]:
        """合成语音"""
        import aiohttp

        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json",
        }
        payload = {
            "model": self.model,
            "input": text,
            "voice": self.voice,
            "response_format": "mp3",
        }

        async with aiohttp.ClientSession() as session:
            async with session.post(
                "https://api.openai.com/v1/audio/speech",
                headers=headers,
                json=payload,
            ) as resp:
                if resp.status == 200:
                    mp3_data = await resp.read()
                    pcm_data = await mp3_to_pcm(mp3_data, target_sample_rate)
                    chunk_size = 4096
                    for i in range(0, len(pcm_data), chunk_size):
                        yield pcm_data[i:i + chunk_size]
                else:
                    error = await resp.text()
                    logger.error(f"OpenAI TTS错误: {error}")


class TTSHandler:
    """TTS处理器工厂"""

    def __init__(self, config):
        self.config = config
        self._provider = self._create_provider()

    def _create_provider(self):
        provider = self.config.TTS_PROVIDER

        if provider == "edge":
            return EdgeTTS(voice=self.config.EDGE_TTS_VOICE)
        elif provider == "baidu":
            return BaiduTTS(
                app_id=self.config.BAIDU_TTS_APP_ID,
                api_key=self.config.BAIDU_TTS_API_KEY,
                secret_key=self.config.BAIDU_TTS_SECRET_KEY,
                voice=self.config.BAIDU_TTS_VOICE,
                speed=self.config.BAIDU_TTS_SPEED,
                pitch=self.config.BAIDU_TTS_PITCH,
                volume=self.config.BAIDU_TTS_VOLUME,
            )
        elif provider == "openai":
            return OpenAITTS(
                api_key=self.config.OPENAI_API_KEY,
                model=self.config.OPENAI_TTS_MODEL,
                voice=self.config.OPENAI_TTS_VOICE,
            )
        else:
            logger.warning(f"未知TTS提供商: {provider}，使用Edge TTS")
            return EdgeTTS()

    async def synthesize_stream(self, text: str) -> AsyncGenerator[bytes, None]:
        """
        流式合成语音

        Args:
            text: 要合成的文字

        Yields:
            bytes: PCM音频数据块
        """
        if not text or not text.strip():
            return

        logger.info(f"TTS合成: {text[:50]}{'...' if len(text) > 50 else ''}")

        try:
            async for chunk in self._provider.synthesize_stream(
                text, self.config.TTS_OUTPUT_SAMPLE_RATE
            ):
                yield chunk
        except Exception as e:
            logger.error(f"TTS合成失败: {e}", exc_info=True)