"""
ASR (自动语音识别) 处理模块

支持多种ASR提供商:
- 百度智能云
- 阿里云 NLS
- 讯飞开放平台
- OpenAI Whisper
"""

import asyncio
import logging
import io
import wave
import struct
from typing import Optional

logger = logging.getLogger(__name__)


def pcm_to_wav(pcm_data: bytes, sample_rate: int = 16000,
               channels: int = 1, sample_width: int = 2) -> bytes:
    """将PCM原始数据转换为WAV格式"""
    buf = io.BytesIO()
    with wave.open(buf, 'wb') as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(sample_width)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_data)
    return buf.getvalue()


class BaiduASR:
    """百度智能云语音识别"""

    def __init__(self, app_id: str, api_key: str, secret_key: str):
        self.app_id = app_id
        self.api_key = api_key
        self.secret_key = secret_key
        self._token = None

    async def _get_token(self) -> str:
        """获取百度Access Token"""
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

    async def recognize(self, pcm_data: bytes, sample_rate: int = 16000) -> Optional[str]:
        """识别语音"""
        import aiohttp
        import base64

        token = await self._get_token()
        wav_data = pcm_to_wav(pcm_data, sample_rate)

        url = "https://vop.baidu.com/server_api"
        payload = {
            "format": "wav",
            "rate": sample_rate,
            "channel": 1,
            "cuid": "ai_toy_esp32s3",
            "token": token,
            "speech": base64.b64encode(wav_data).decode(),
            "len": len(wav_data),
        }

        async with aiohttp.ClientSession() as session:
            async with session.post(url, json=payload) as resp:
                result = await resp.json()
                if result.get("err_no") == 0:
                    return result.get("result", [""])[0]
                else:
                    logger.error(f"百度ASR错误: {result.get('err_msg')}")
                    return None


class WhisperASR:
    """OpenAI Whisper 语音识别 (本地或API)"""

    def __init__(self, model: str = "base", language: str = "zh",
                 api_key: Optional[str] = None, use_api: bool = False):
        self.model_name = model
        self.language = language
        self.api_key = api_key
        self.use_api = use_api
        self._model = None

    def _load_model(self):
        """加载本地Whisper模型"""
        if self._model is None:
            import whisper
            logger.info(f"加载Whisper模型: {self.model_name}")
            self._model = whisper.load_model(self.model_name)
        return self._model

    async def recognize(self, pcm_data: bytes, sample_rate: int = 16000) -> Optional[str]:
        """识别语音"""
        if self.use_api and self.api_key:
            return await self._recognize_api(pcm_data, sample_rate)
        else:
            return await self._recognize_local(pcm_data, sample_rate)

    async def _recognize_local(self, pcm_data: bytes, sample_rate: int) -> Optional[str]:
        """使用本地Whisper模型识别"""
        import numpy as np
        import tempfile
        import os

        # 转换为numpy数组
        samples = np.frombuffer(pcm_data, dtype=np.int16).astype(np.float32) / 32768.0

        # 在线程池中运行 (避免阻塞事件循环)
        loop = asyncio.get_event_loop()
        model = self._load_model()

        def _transcribe():
            result = model.transcribe(samples, language=self.language, fp16=False)
            return result.get("text", "").strip()

        text = await loop.run_in_executor(None, _transcribe)
        logger.info(f"Whisper识别结果: {text}")
        return text if text else None

    async def _recognize_api(self, pcm_data: bytes, sample_rate: int) -> Optional[str]:
        """使用OpenAI Whisper API识别"""
        import aiohttp

        wav_data = pcm_to_wav(pcm_data, sample_rate)

        headers = {"Authorization": f"Bearer {self.api_key}"}
        data = aiohttp.FormData()
        data.add_field("file", wav_data, filename="audio.wav", content_type="audio/wav")
        data.add_field("model", "whisper-1")
        data.add_field("language", self.language)

        async with aiohttp.ClientSession() as session:
            async with session.post(
                "https://api.openai.com/v1/audio/transcriptions",
                headers=headers, data=data
            ) as resp:
                result = await resp.json()
                return result.get("text", "").strip() or None


class ASRHandler:
    """ASR处理器工厂"""

    def __init__(self, config):
        self.config = config
        self._provider = self._create_provider()

    def _create_provider(self):
        provider = self.config.ASR_PROVIDER

        if provider == "baidu":
            return BaiduASR(
                self.config.BAIDU_ASR_APP_ID,
                self.config.BAIDU_ASR_API_KEY,
                self.config.BAIDU_ASR_SECRET_KEY,
            )
        elif provider == "whisper":
            use_api = bool(self.config.OPENAI_API_KEY and
                           self.config.OPENAI_API_KEY != "your_openai_api_key")
            return WhisperASR(
                model=self.config.WHISPER_MODEL,
                language=self.config.WHISPER_LANGUAGE,
                api_key=self.config.OPENAI_API_KEY,
                use_api=use_api,
            )
        else:
            logger.warning(f"未知ASR提供商: {provider}，使用Whisper本地模式")
            return WhisperASR()

    async def recognize(self, pcm_data: bytes) -> Optional[str]:
        """识别语音，返回文字"""
        if not pcm_data:
            return None

        logger.info(f"开始ASR识别，音频大小: {len(pcm_data)} 字节 "
                    f"({len(pcm_data) / self.config.AUDIO_SAMPLE_RATE / 2:.1f}s)")

        try:
            text = await self._provider.recognize(pcm_data, self.config.AUDIO_SAMPLE_RATE)
            if text:
                logger.info(f"ASR识别结果: {text}")
            else:
                logger.warning("ASR识别结果为空")
            return text
        except Exception as e:
            logger.error(f"ASR识别失败: {e}", exc_info=True)
            return None