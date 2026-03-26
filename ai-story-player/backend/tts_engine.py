"""
TTS (文字转语音) 引擎模块
使用 edge-tts 将故事文本转换为语音
"""

import asyncio
import io
import logging
import os
import tempfile
from typing import AsyncGenerator, Optional
import edge_tts

logger = logging.getLogger(__name__)

# 可用的中文声音列表
CHINESE_VOICES = {
    "xiaoxiao": "zh-CN-XiaoxiaoNeural",    # 晓晓 - 温柔女声（推荐睡前故事）
    "yunxi": "zh-CN-YunxiNeural",           # 云希 - 男声
    "xiaoyi": "zh-CN-XiaoyiNeural",         # 晓伊 - 活泼女声
    "yunjian": "zh-CN-YunjianNeural",       # 云健 - 男声
    "xiaomo": "zh-CN-XiaomoNeural",         # 晓墨 - 温柔女声
    "xiaoxuan": "zh-CN-XiaoxuanNeural",     # 晓萱 - 女声
    "xiaohan": "zh-CN-XiaohanNeural",       # 晓涵 - 女声
    "xiaorui": "zh-CN-XiaoruiNeural",       # 晓睿 - 女声
}


class TTSEngine:
    """TTS 语音合成引擎"""
    
    def __init__(
        self,
        voice: str = "zh-CN-XiaoxiaoNeural",
        rate: str = "-10%",      # 稍慢一点，适合睡前
        volume: str = "+0%",
        pitch: str = "-5Hz"      # 稍低音调，更温柔
    ):
        self.voice = voice
        self.rate = rate
        self.volume = volume
        self.pitch = pitch
        
    async def synthesize_to_bytes(self, text: str) -> bytes:
        """
        将文本合成为音频字节数据
        
        Args:
            text: 要合成的文本
            
        Returns:
            MP3 格式的音频字节数据
        """
        if not text.strip():
            return b""
            
        try:
            communicate = edge_tts.Communicate(
                text=text,
                voice=self.voice,
                rate=self.rate,
                volume=self.volume,
                pitch=self.pitch
            )
            
            audio_data = b""
            async for chunk in communicate.stream():
                if chunk["type"] == "audio":
                    audio_data += chunk["data"]
            
            logger.debug(f"TTS 合成完成，文本长度: {len(text)}，音频大小: {len(audio_data)} bytes")
            return audio_data
            
        except Exception as e:
            logger.error(f"TTS 合成失败: {e}")
            return b""
    
    async def synthesize_to_file(self, text: str, output_path: str) -> bool:
        """
        将文本合成为音频文件
        
        Args:
            text: 要合成的文本
            output_path: 输出文件路径 (.mp3)
            
        Returns:
            是否成功
        """
        try:
            communicate = edge_tts.Communicate(
                text=text,
                voice=self.voice,
                rate=self.rate,
                volume=self.volume,
                pitch=self.pitch
            )
            await communicate.save(output_path)
            logger.info(f"TTS 音频已保存到: {output_path}")
            return True
        except Exception as e:
            logger.error(f"TTS 保存失败: {e}")
            return False
    
    async def synthesize_stream(self, text: str) -> AsyncGenerator[bytes, None]:
        """
        流式合成音频（逐块返回）
        
        Args:
            text: 要合成的文本
            
        Yields:
            音频数据块
        """
        if not text.strip():
            return
            
        try:
            communicate = edge_tts.Communicate(
                text=text,
                voice=self.voice,
                rate=self.rate,
                volume=self.volume,
                pitch=self.pitch
            )
            
            async for chunk in communicate.stream():
                if chunk["type"] == "audio":
                    yield chunk["data"]
                    
        except Exception as e:
            logger.error(f"TTS 流式合成失败: {e}")
    
    async def synthesize_sentences_stream(
        self, 
        text: str,
        sentence_callback=None
    ) -> AsyncGenerator[dict, None]:
        """
        按句子流式合成音频，同时返回文本和音频
        
        Args:
            text: 完整故事文本
            sentence_callback: 每句话合成完成的回调
            
        Yields:
            包含文本和音频数据的字典
        """
        # 按句子分割
        sentences = self._split_sentences(text)
        
        for i, sentence in enumerate(sentences):
            if not sentence.strip():
                continue
                
            audio_data = await self.synthesize_to_bytes(sentence)
            
            yield {
                "type": "sentence",
                "index": i,
                "text": sentence,
                "audio": audio_data,
                "is_last": i == len(sentences) - 1
            }
            
            if sentence_callback:
                await sentence_callback(sentence, audio_data)
    
    def _split_sentences(self, text: str) -> list:
        """
        将文本按句子分割
        
        Args:
            text: 输入文本
            
        Returns:
            句子列表
        """
        import re
        # 按中文标点分割
        sentences = re.split(r'([。！？…\n])', text)
        
        result = []
        current = ""
        for part in sentences:
            current += part
            if part in ['。', '！', '？', '…', '\n'] and current.strip():
                result.append(current.strip())
                current = ""
        
        if current.strip():
            result.append(current.strip())
        
        return result
    
    @staticmethod
    async def list_voices() -> list:
        """列出所有可用的中文声音"""
        voices = await edge_tts.list_voices()
        chinese_voices = [v for v in voices if v["Locale"].startswith("zh-")]
        return chinese_voices


class AudioChunkBuffer:
    """音频块缓冲区，用于流式传输"""
    
    def __init__(self, chunk_size: int = 4096):
        self.chunk_size = chunk_size
        self.buffer = b""
        
    def add_data(self, data: bytes):
        """添加音频数据"""
        self.buffer += data
        
    def get_chunks(self) -> list:
        """获取完整的音频块"""
        chunks = []
        while len(self.buffer) >= self.chunk_size:
            chunks.append(self.buffer[:self.chunk_size])
            self.buffer = self.buffer[self.chunk_size:]
        return chunks
    
    def flush(self) -> bytes:
        """获取剩余数据"""
        remaining = self.buffer
        self.buffer = b""
        return remaining


if __name__ == "__main__":
    async def test():
        tts = TTSEngine(
            voice="zh-CN-XiaoxiaoNeural",
            rate="-10%"
        )
        
        test_text = "从前，在一片茂密的森林里，住着一只叫小雷的小恐龙。小雷虽然长得很大，但他有一颗温柔的心。"
        
        print("开始合成语音...")
        audio_data = await tts.synthesize_to_bytes(test_text)
        
        with open("test_output.mp3", "wb") as f:
            f.write(audio_data)
        
        print(f"语音合成完成！文件大小: {len(audio_data)} bytes")
        print("已保存到 test_output.mp3")
    
    asyncio.run(test())