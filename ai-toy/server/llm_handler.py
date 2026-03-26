"""
LLM (大语言模型) 处理模块

支持多种LLM提供商:
- OpenAI GPT (及兼容API)
- 通义千问 (阿里云)
- 文心一言 (百度)
- 智谱AI (GLM)
- Ollama (本地部署)
"""

import asyncio
import logging
from typing import AsyncGenerator, List, Dict, Optional

logger = logging.getLogger(__name__)


class OpenAILLM:
    """OpenAI GPT 及兼容API"""

    def __init__(self, api_key: str, base_url: str, model: str):
        self.api_key = api_key
        self.base_url = base_url.rstrip("/")
        self.model = model

    async def chat_stream(self, messages: List[Dict],
                          max_tokens: int = 200,
                          temperature: float = 0.8) -> AsyncGenerator[str, None]:
        """流式对话"""
        import aiohttp
        import json

        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json",
        }
        payload = {
            "model": self.model,
            "messages": messages,
            "max_tokens": max_tokens,
            "temperature": temperature,
            "stream": True,
        }

        async with aiohttp.ClientSession() as session:
            async with session.post(
                f"{self.base_url}/chat/completions",
                headers=headers,
                json=payload,
                timeout=aiohttp.ClientTimeout(total=30),
            ) as resp:
                if resp.status != 200:
                    error = await resp.text()
                    logger.error(f"OpenAI API错误 {resp.status}: {error}")
                    yield "抱歉，AI服务暂时不可用。"
                    return

                async for line in resp.content:
                    line = line.decode("utf-8").strip()
                    if not line or line == "data: [DONE]":
                        continue
                    if line.startswith("data: "):
                        try:
                            data = json.loads(line[6:])
                            delta = data["choices"][0].get("delta", {})
                            content = delta.get("content", "")
                            if content:
                                yield content
                        except (json.JSONDecodeError, KeyError, IndexError):
                            continue


class QianwenLLM:
    """通义千问 (阿里云)"""

    def __init__(self, api_key: str, model: str):
        self.api_key = api_key
        self.model = model

    async def chat_stream(self, messages: List[Dict],
                          max_tokens: int = 200,
                          temperature: float = 0.8) -> AsyncGenerator[str, None]:
        """流式对话"""
        import aiohttp
        import json

        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json",
            "X-DashScope-SSE": "enable",
        }
        payload = {
            "model": self.model,
            "input": {"messages": messages},
            "parameters": {
                "max_tokens": max_tokens,
                "temperature": temperature,
                "incremental_output": True,
            },
        }

        async with aiohttp.ClientSession() as session:
            async with session.post(
                "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation",
                headers=headers,
                json=payload,
            ) as resp:
                async for line in resp.content:
                    line = line.decode("utf-8").strip()
                    if line.startswith("data:"):
                        try:
                            data = json.loads(line[5:])
                            text = data.get("output", {}).get("text", "")
                            if text:
                                yield text
                        except json.JSONDecodeError:
                            continue


class OllamaLLM:
    """Ollama 本地部署LLM"""

    def __init__(self, base_url: str, model: str):
        self.base_url = base_url.rstrip("/")
        self.model = model

    async def chat_stream(self, messages: List[Dict],
                          max_tokens: int = 200,
                          temperature: float = 0.8) -> AsyncGenerator[str, None]:
        """流式对话"""
        import aiohttp
        import json

        payload = {
            "model": self.model,
            "messages": messages,
            "stream": True,
            "options": {
                "num_predict": max_tokens,
                "temperature": temperature,
            },
        }

        async with aiohttp.ClientSession() as session:
            async with session.post(
                f"{self.base_url}/api/chat",
                json=payload,
                timeout=aiohttp.ClientTimeout(total=60),
            ) as resp:
                async for line in resp.content:
                    line = line.decode("utf-8").strip()
                    if not line:
                        continue
                    try:
                        data = json.loads(line)
                        content = data.get("message", {}).get("content", "")
                        if content:
                            yield content
                        if data.get("done"):
                            break
                    except json.JSONDecodeError:
                        continue


class LLMHandler:
    """LLM处理器"""

    def __init__(self, config):
        self.config = config
        self._provider = self._create_provider()
        # 对话历史 (按设备ID存储)
        self._histories: Dict[str, List[Dict]] = {}

    def _create_provider(self):
        provider = self.config.LLM_PROVIDER

        if provider == "openai":
            return OpenAILLM(
                api_key=self.config.OPENAI_API_KEY,
                base_url=self.config.OPENAI_BASE_URL,
                model=self.config.OPENAI_MODEL,
            )
        elif provider == "qianwen":
            return QianwenLLM(
                api_key=self.config.QIANWEN_API_KEY,
                model=self.config.QIANWEN_MODEL,
            )
        elif provider == "ollama":
            return OllamaLLM(
                base_url=self.config.OLLAMA_BASE_URL,
                model=self.config.OLLAMA_MODEL,
            )
        else:
            logger.warning(f"未知LLM提供商: {provider}，使用Ollama")
            return OllamaLLM(
                base_url=self.config.OLLAMA_BASE_URL,
                model=self.config.OLLAMA_MODEL,
            )

    def _get_history(self, device_id: str) -> List[Dict]:
        """获取设备的对话历史"""
        if device_id not in self._histories:
            self._histories[device_id] = []
        return self._histories[device_id]

    def _add_to_history(self, device_id: str, role: str, content: str):
        """添加消息到历史"""
        history = self._get_history(device_id)
        history.append({"role": role, "content": content})

        # 限制历史长度
        max_history = self.config.LLM_HISTORY_MAX * 2  # 每轮包含user+assistant
        if len(history) > max_history:
            self._histories[device_id] = history[-max_history:]

    def clear_history(self, device_id: str):
        """清除对话历史"""
        self._histories.pop(device_id, None)

    async def chat_stream(self, device_id: str, user_text: str) -> AsyncGenerator[str, None]:
        """
        流式对话

        Args:
            device_id: 设备ID
            user_text: 用户输入文字

        Yields:
            str: AI回复文字片段
        """
        logger.info(f"[{device_id}] 用户: {user_text}")

        # 添加用户消息到历史
        self._add_to_history(device_id, "user", user_text)

        # 构建消息列表 (系统提示 + 历史)
        messages = [
            {"role": "system", "content": self.config.LLM_SYSTEM_PROMPT}
        ] + self._get_history(device_id)

        # 收集完整回复
        full_response = ""

        try:
            async for chunk in self._provider.chat_stream(
                messages,
                max_tokens=self.config.LLM_MAX_TOKENS,
                temperature=self.config.LLM_TEMPERATURE,
            ):
                full_response += chunk
                yield chunk

        except Exception as e:
            logger.error(f"LLM对话失败: {e}", exc_info=True)
            yield "抱歉，我现在有点不舒服，请稍后再试。"
            full_response = "抱歉，我现在有点不舒服，请稍后再试。"

        finally:
            # 将完整回复添加到历史
            if full_response:
                self._add_to_history(device_id, "assistant", full_response)
                logger.info(f"[{device_id}] AI回复: {full_response}")