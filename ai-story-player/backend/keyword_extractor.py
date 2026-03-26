"""
关键词提取模块
从用户语音文本中提取故事关键词
"""

import re
from typing import List, Dict

# 预定义关键词分类
KEYWORD_CATEGORIES = {
    "动物": [
        "恐龙", "龙", "狮子", "老虎", "大象", "熊猫", "兔子", "小猫", "小狗",
        "企鹅", "海豚", "鲸鱼", "独角兽", "凤凰", "龙猫", "小熊", "狐狸",
        "小鸟", "蝴蝶", "小鱼", "乌龟", "小马", "小猪", "小羊"
    ],
    "场景": [
        "太空", "宇宙", "星球", "月亮", "星星", "森林", "海洋", "大海",
        "山谷", "城堡", "王国", "魔法森林", "冰雪", "沙漠", "彩虹",
        "云朵", "天空", "地下", "深海", "火山"
    ],
    "角色": [
        "公主", "王子", "国王", "女王", "骑士", "魔法师", "巫师", "仙女",
        "精灵", "小矮人", "巨人", "超级英雄", "机器人", "小孩", "老爷爷",
        "老奶奶", "小朋友"
    ],
    "主题": [
        "友谊", "勇气", "冒险", "魔法", "梦想", "爱心", "分享", "帮助",
        "成长", "快乐", "神奇", "奇幻", "探险", "发现", "学习"
    ],
    "物品": [
        "宝剑", "魔法棒", "宝藏", "地图", "飞船", "火箭", "魔法书",
        "神灯", "水晶球", "彩虹糖", "魔法药水"
    ]
}

# 扁平化关键词列表用于快速查找
ALL_KEYWORDS = {}
for category, keywords in KEYWORD_CATEGORIES.items():
    for kw in keywords:
        ALL_KEYWORDS[kw] = category


def extract_keywords(text: str) -> Dict:
    """
    从文本中提取故事关键词
    
    Args:
        text: 用户输入的文本
        
    Returns:
        包含关键词和分类的字典
    """
    found_keywords = []
    found_categories = set()
    
    # 直接匹配预定义关键词
    for keyword, category in ALL_KEYWORDS.items():
        if keyword in text:
            found_keywords.append(keyword)
            found_categories.add(category)
    
    # 去重
    found_keywords = list(dict.fromkeys(found_keywords))
    
    # 如果没有找到关键词，尝试提取名词
    if not found_keywords:
        # 简单的中文名词提取（基于常见模式）
        patterns = [
            r'[\u4e00-\u9fa5]{2,4}(?:的故事|冒险|王国)',
            r'(?:关于|讲|说|听)[\u4e00-\u9fa5]{2,6}',
        ]
        for pattern in patterns:
            matches = re.findall(pattern, text)
            for match in matches:
                # 清理提取的词
                clean = re.sub(r'关于|讲|说|听|的故事|冒险|王国', '', match)
                if clean and len(clean) >= 2:
                    found_keywords.append(clean)
    
    return {
        "keywords": found_keywords,
        "categories": list(found_categories),
        "original_text": text,
        "has_keywords": len(found_keywords) > 0
    }


def build_story_prompt(keywords: List[str], categories: List[str]) -> str:
    """
    根据关键词构建故事生成提示词
    
    Args:
        keywords: 关键词列表
        categories: 关键词分类列表
        
    Returns:
        故事生成提示词
    """
    if not keywords:
        keywords = ["小动物", "森林"]
    
    keywords_str = "、".join(keywords[:5])  # 最多使用5个关键词
    
    prompt = f"""你是一位温柔的儿童故事讲述者。请根据以下关键词创作一个适合3-8岁儿童的睡前故事。

关键词：{keywords_str}

要求：
1. 故事长度：300-500字，适合睡前朗读（约3-5分钟）
2. 语言：简单温柔，充满想象力，适合儿童理解
3. 结构：有开头、发展、结尾，结尾要温馨美好
4. 主题：积极向上，传递友爱、勇气、善良等正面价值观
5. 风格：充满童趣，可以有拟人化的动物或物品
6. 结尾：故事结束时，主角要准备睡觉或进入梦乡，帮助孩子入睡

请直接开始讲故事，不需要标题，用温柔的语气娓娓道来。"""
    
    return prompt


def get_illustration_prompt(keywords: List[str]) -> str:
    """
    根据关键词生成插图描述（用于屏幕显示）
    
    Args:
        keywords: 关键词列表
        
    Returns:
        插图场景描述
    """
    if not keywords:
        return "moonlit_forest"
    
    # 映射关键词到预置插图
    illustration_map = {
        "恐龙": "dinosaur",
        "龙": "dragon",
        "太空": "space",
        "宇宙": "space",
        "星球": "space",
        "公主": "princess",
        "王子": "prince",
        "城堡": "castle",
        "森林": "forest",
        "海洋": "ocean",
        "大海": "ocean",
        "月亮": "moon",
        "星星": "stars",
        "独角兽": "unicorn",
        "机器人": "robot",
        "火箭": "rocket",
        "魔法": "magic",
        "企鹅": "penguin",
        "熊猫": "panda",
        "小熊": "bear",
        "兔子": "rabbit",
    }
    
    for keyword in keywords:
        if keyword in illustration_map:
            return illustration_map[keyword]
    
    return "moonlit_forest"  # 默认插图


if __name__ == "__main__":
    # 测试
    test_texts = [
        "我想听一个关于恐龙的故事",
        "讲一个太空冒险的故事",
        "公主和魔法师的故事",
        "小兔子在森林里的冒险",
    ]
    
    for text in test_texts:
        result = extract_keywords(text)
        print(f"输入: {text}")
        print(f"关键词: {result['keywords']}")
        print(f"分类: {result['categories']}")
        print(f"插图: {get_illustration_prompt(result['keywords'])}")
        print("-" * 50)