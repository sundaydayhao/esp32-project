"""
STT (语音转文字) 引擎模块
处理来自 ESP32-S3 的音频数据，识别关键词
"""

import asyncio
import io
import logging
import wave
from typing import Optional
import numpy as np

logger = logging.getLogger(__name__)


class STTEngine:
    """语音识别引擎"""
    
    def __init__(self, api_key: str = None, base_url: str = None, model: str = "whisper-1"):
        self.api_key = api_key
        self.base_url = base_url
        self.model = model
        self._client = None
        
    def _get_client(self):
        """懒加载 OpenAI 客户端"""
        if self._client is None:
            from openai import AsyncOpenAI
            self._client = AsyncOpenAI(
                api_key=self.api_key,
                base_url=self.base_url
            )
        return self._client
    
    async def transcribe_audio(self, audio_data: bytes, sample_rate: int = 16000) -> str:
        """
        将音频数据转换为文字
        
        Args:
            audio_data: PCM 音频数据 (16-bit, mono)
            sample_rate: 采样率 (默认 16000 Hz)
            
        Returns:
            识别的文字
        """
        try:
            # 将 PCM 数据转换为 WAV 格式
            wav_data = self._pcm_to_wav(audio_data, sample_rate)
            
            # 使用 OpenAI Whisper API
            client = self._get_client()
            
            audio_file = io.BytesIO(wav_data)
            audio_file.name = "audio.wav"
            
            response = await client.audio.transcriptions.create(
                model=self.model,
                file=audio_file,
                language="zh",
                response_format="text"
            )
            
            text = response.strip() if isinstance(response, str) else response.text.strip()
            logger.info(f"语音识别结果: {text}")
            return text
            
        except Exception as e:
            logger.error(f"语音识别失败: {e}")
            return ""
    
    async def transcribe_file(self, file_path: str) -> str:
        """
        识别音频文件
        
        Args:
            file_path: 音频文件路径
            
        Returns:
            识别的文字
        """
        try:
            client = self._get_client()
            
            with open(file_path, "rb") as audio_file:
                response = await client.audio.transcriptions.create(
                    model=self.model,
                    file=audio_file,
                    language="zh",
                    response_format="text"
                )
            
            text = response.strip() if isinstance(response, str) else response.text.strip()
            logger.info(f"文件识别结果: {text}")
            return text
            
        except Exception as e:
            logger.error(f"文件识别失败: {e}")
            return ""
    
    def _pcm_to_wav(self, pcm_data: bytes, sample_rate: int = 16000) -> bytes:
        """
        将 PCM 数据转换为 WAV 格式
        
        Args:
            pcm_data: PCM 音频数据
            sample_rate: 采样率
            
        Returns:
            WAV 格式数据
        """
        buffer = io.BytesIO()
        with wave.open(buffer, 'wb') as wav_file:
            wav_file.setnchannels(1)      # 单声道
            wav_file.setsampwidth(2)       # 16-bit
            wav_file.setframerate(sample_rate)
            wav_file.writeframes(pcm_data)
        
        return buffer.getvalue()
    
    def detect_voice_activity(
        self, 
        audio_data: bytes, 
        threshold: float = 500.0,
        min_duration_ms: int = 500
    ) -> bool:
        """
        检测音频中是否有语音活动 (VAD)
        
        Args:
            audio_data: PCM 音频数据
            threshold: 能量阈值
            min_duration_ms: 最小语音持续时间（毫秒）
            
        Returns:
            是否检测到语音
        """
        try:
            # 将字节数据转换为 numpy 数组
            audio_array = np.frombuffer(audio_data, dtype=np.int16).astype(np.float32)
            
            if len(audio_array) == 0:
                return False
            
            # 计算 RMS 能量
            rms = np.sqrt(np.mean(audio_array ** 2))
            
            logger.debug(f"音频 RMS 能量: {rms:.2f}")
            
            return rms > threshold
            
        except Exception as e:
            logger.error(f"VAD 检测失败: {e}")
            return False
    
    def preprocess_audio(
        self, 
        audio_data: bytes,
        sample_rate: int = 16000,
        normalize: bool = True
    ) -> bytes:
        """
        预处理音频数据
        
        Args:
            audio_data: 原始 PCM 数据
            sample_rate: 采样率
            normalize: 是否归一化
            
        Returns:
            处理后的 PCM 数据
        """
        try:
            audio_array = np.frombuffer(audio_data, dtype=np.int16).astype(np.float32)
            
            if normalize and len(audio_array) > 0:
                max_val = np.max(np.abs(audio_array))
                if max_val > 0:
                    audio_array = audio_array / max_val * 32767
            
            return audio_array.astype(np.int16).tobytes()
            
        except Exception as e:
            logger.error(f"音频预处理失败: {e}")
            return audio_data


class AudioRecorder:
    """音频录制缓冲区管理"""
    
    def __init__(
        self,
        sample_rate: int = 16000,
        max_duration_s: int = 10,
        silence_threshold: float = 300.0,
        silence_duration_ms: int = 1500
    ):
        self.sample_rate = sample_rate
        self.max_duration_s = max_duration_s
        self.silence_threshold = silence_threshold
        self.silence_duration_ms = silence_duration_ms
        
        self.buffer = b""
        self.is_recording = False
        self.silence_frames = 0
        
        # 每帧大小（20ms）
        self.frame_size = int(sample_rate * 0.02) * 2  # 16-bit = 2 bytes
        self.silence_frames_threshold = int(silence_duration_ms / 20)
        
    def start_recording(self):
        """开始录音"""
        self.buffer = b""
        self.is_recording = True
        self.silence_frames = 0
        logger.info("开始录音")
        
    def add_frame(self, frame_data: bytes) -> bool:
        """
        添加音频帧
        
        Returns:
            是否应该停止录音（检测到足够长的静音）
        """
        if not self.is_recording:
            return False
            
        self.buffer += frame_data
        
        # 检查是否超过最大时长
        max_bytes = self.sample_rate * self.max_duration_s * 2
        if len(self.buffer) >= max_bytes:
            logger.info("达到最大录音时长，停止录音")
            return True
        
        # 检测静音
        frame_array = np.frombuffer(frame_data, dtype=np.int16).astype(np.float32)
        rms = np.sqrt(np.mean(frame_array ** 2)) if len(frame_array) > 0 else 0
        
        if rms < self.silence_threshold:
            self.silence_frames += 1
        else:
            self.silence_frames = 0
        
        # 如果静音超过阈值，停止录音
        if self.silence_frames >= self.silence_frames_threshold:
            # 确保至少录了一些有效音频
            min_bytes = self.sample_rate * 0.5 * 2  # 至少 0.5 秒
            if len(self.buffer) >= min_bytes:
                logger.info(f"检测到静音 {self.silence_duration_ms}ms，停止录音")
                return True
        
        return False
    
    def stop_recording(self) -> bytes:
        """停止录音并返回音频数据"""
        self.is_recording = False
        data = self.buffer
        self.buffer = b""
        logger.info(f"录音结束，数据大小: {len(data)} bytes")
        return data
    
    def get_duration_ms(self) -> int:
        """获取当前录音时长（毫秒）"""
        return int(len(self.buffer) / (self.sample_rate * 2) * 1000)


if __name__ == "__main__":
    import os
    from dotenv import load_dotenv
    
    load_dotenv()
    
    async def test():
        stt = STTEngine(
            api_key=os.getenv("OPENAI_API_KEY"),
            base_url=os.getenv("OPENAI_BASE_URL")
        )
        
        # 测试 VAD
        # 生成一段测试音频（正弦波）
        sample_rate = 16000
        duration = 1.0
        t = np.linspace(0, duration, int(sample_rate * duration))
        audio = (np.sin(2 * np.pi * 440 * t) * 10000).astype(np.int16)
        
        has_voice = stt.detect_voice_activity(audio.tobytes())
        print(f"VAD 检测结果: {has_voice}")
    
    asyncio.run(test())