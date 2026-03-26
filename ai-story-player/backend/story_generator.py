"""
AI 故事生成模块
使用 OpenAI GPT 实时生成儿童睡前故事
"""

import asyncio
import logging
from typing import AsyncGenerator, List, Optional
from openai import AsyncOpenAI
from keyword_extractor import build_story_prompt, extract_keywords

logger = logging.getLogger(__name__)


class StoryGenerator:
    """AI 故事生成器"""
    
    def __init__(self, api_key: str, base_url: str = None, model: str = "gpt-4o"):
        self.client = AsyncOpenAI(
            api_key=api_key,
            base_url=base_url
        )
        self.model = model
        
    async def generate_story_stream(
        self, 
        keywords: List[str],
        categories: List[str] = None,
        max_tokens: int = 800
    ) -> AsyncGenerator[str, None]:
        """
        流式生成故事
        
        Args:
            keywords: 故事关键词列表
            categories: 关键词分类
            max_tokens: 最大生成 token 数
            
        Yields:
            故事文本片段
        """
        prompt = build_story_prompt(keywords, categories or [])
        
        logger.info(f"开始生成故事，关键词: {keywords}")
        
        try:
            stream = await self.client.chat.completions.create(
                model=self.model,
                messages=[
                    {
                        "role": "system",
                        "content": "你是一位专业的儿童故事作家，擅长创作温馨、有趣、富有教育意义的睡前故事。"
                    },
                    {
                        "role": "user",
                        "content": prompt
                    }
                ],
                max_tokens=max_tokens,
                temperature=0.8,
                stream=True
            )
            
            async for chunk in stream:
                if chunk.choices[0].delta.content:
                    yield chunk.choices[0].delta.content
                    
        except Exception as e:
            logger.error(f"故事生成失败: {e}")
            # 返回备用故事
            async for chunk in self._fallback_story(keywords):
                yield chunk
    
    async def generate_story_full(
        self,
        keywords: List[str],
        categories: List[str] = None,
        max_tokens: int = 800
    ) -> str:
        """
        生成完整故事（非流式）
        
        Args:
            keywords: 故事关键词列表
            categories: 关键词分类
            max_tokens: 最大生成 token 数
            
        Returns:
            完整故事文本
        """
        full_story = ""
        async for chunk in self.generate_story_stream(keywords, categories, max_tokens):
            full_story += chunk
        return full_story
    
    async def _fallback_story(self, keywords: List[str]) -> AsyncGenerator[str, None]:
        """备用故事（当 API 不可用时）"""
        keyword = keywords[0] if keywords else "小动物"
        
        fallback_stories = {
            "恐龙": """从前，在一片茂密的森林里，住着一只叫小雷的小恐龙。
小雷虽然长得很大，但他有一颗温柔的心。
每天晚上，他都会轻轻地走到河边，看着天上的星星。
"星星啊星星，你们好漂亮！"小雷轻声说。
一颗星星眨了眨眼睛，好像在回应他。
小雷开心地笑了，慢慢地闭上眼睛，进入了甜甜的梦乡。
在梦里，他和星星们一起跳舞，快乐极了。""",
            
            "太空": """在遥远的宇宙里，有一艘小小的飞船。
飞船里住着一个叫小星的小宇航员。
他驾驶着飞船，穿越了一个又一个美丽的星球。
红色的火星、蓝色的海王星、金色的土星……
每个星球都有不同的颜色，美丽极了。
小星累了，把飞船停在了月亮旁边。
月亮妈妈轻轻地唱起了摇篮曲，小星慢慢地睡着了。
他梦见自己变成了一颗小星星，在宇宙中闪闪发光。""",
            
            "公主": """在一个美丽的王国里，住着一位善良的小公主。
她最喜欢在花园里和小动物们玩耍。
有一天，她发现了一只受伤的小鸟。
"小鸟，你怎么了？"公主轻声问道。
她小心地把小鸟带回宫殿，细心地照顾它。
几天后，小鸟的翅膀好了，飞向了蓝天。
"谢谢你，善良的公主！"小鸟唱着歌飞走了。
公主看着小鸟飞翔，心里充满了快乐。
她回到自己的小床上，带着微笑进入了梦乡。"""
        }
        
        story = fallback_stories.get(keyword, fallback_stories["恐龙"])
        
        # 模拟流式输出
        for char in story:
            yield char
            await asyncio.sleep(0.02)
    
    async def extract_keywords_with_ai(self, text: str) -> List[str]:
        """
        使用 AI 从文本中提取关键词（更准确）
        
        Args:
            text: 用户输入文本
            
        Returns:
            关键词列表
        """
        try:
            response = await self.client.chat.completions.create(
                model=self.model,
                messages=[
                    {
                        "role": "system",
                        "content": "你是一个关键词提取助手。从用户的话中提取故事主题关键词，返回JSON格式。"
                    },
                    {
                        "role": "user",
                        "content": f"""从以下文本中提取故事关键词，返回JSON格式：{{"keywords": ["关键词1", "关键词2"]}}
                        
文本：{text}

只返回JSON，不要其他内容。"""
                    }
                ],
                max_tokens=100,
                temperature=0.3
            )
            
            import json
            content = response.choices[0].message.content.strip()
            # 清理可能的 markdown 代码块
            content = content.replace("```json", "").replace("```", "").strip()
            result = json.loads(content)
            return result.get("keywords", [])
            
        except Exception as e:
            logger.warning(f"AI 关键词提取失败，使用规则提取: {e}")
            result = extract_keywords(text)
            return result["keywords"]


class StorySession:
    """故事会话管理"""
    
    def __init__(self):
        self.current_story = ""
        self.keywords = []
        self.is_playing = False
        self.session_id = None
        
    def reset(self):
        """重置会话"""
        self.current_story = ""
        self.keywords = []
        self.is_playing = False
        
    def append_story(self, text: str):
        """追加故事文本"""
        self.current_story += text
        
    def get_story(self) -> str:
        """获取当前故事"""
        return self.current_story


if __name__ == "__main__":
    import os
    from dotenv import load_dotenv
    
    load_dotenv()
    
    async def test():
        generator = StoryGenerator(
            api_key=os.getenv("OPENAI_API_KEY"),
            base_url=os.getenv("OPENAI_BASE_URL"),
            model=os.getenv("OPENAI_MODEL", "gpt-4o")
        )
        
        print("生成恐龙故事...")
        async for chunk in generator.generate_story_stream(["恐龙", "森林"]):
            print(chunk, end="", flush=True)
        print("\n\n故事生成完成！")
    
    asyncio.run(test())