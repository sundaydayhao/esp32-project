"""
插图生成模块
为故事生成或选择合适的插图，发送给 ESP32-S3 屏幕显示
"""

import asyncio
import base64
import io
import logging
import os
from pathlib import Path
from typing import Optional, Tuple
from PIL import Image, ImageDraw, ImageFont

logger = logging.getLogger(__name__)

# 屏幕分辨率 (ST7789 240x240)
SCREEN_WIDTH = 240
SCREEN_HEIGHT = 240

# 预置插图主题颜色方案
THEME_COLORS = {
    "dinosaur": {
        "bg": (34, 139, 34),
        "accent": (144, 238, 144),
        "text": (255, 255, 255),
        "label": "恐龙"
    },
    "space": {
        "bg": (10, 10, 50),
        "accent": (255, 215, 0),
        "text": (255, 255, 255),
        "label": "太空"
    },
    "princess": {
        "bg": (255, 182, 193),
        "accent": (255, 215, 0),
        "text": (139, 0, 139),
        "label": "公主"
    },
    "dragon": {
        "bg": (139, 0, 0),
        "accent": (255, 165, 0),
        "text": (255, 255, 255),
        "label": "龙"
    },
    "ocean": {
        "bg": (0, 105, 148),
        "accent": (135, 206, 235),
        "text": (255, 255, 255),
        "label": "海洋"
    },
    "forest": {
        "bg": (34, 85, 34),
        "accent": (255, 215, 0),
        "text": (255, 255, 255),
        "label": "森林"
    },
    "castle": {
        "bg": (105, 105, 105),
        "accent": (255, 215, 0),
        "text": (255, 255, 255),
        "label": "城堡"
    },
    "moon": {
        "bg": (25, 25, 112),
        "accent": (255, 255, 200),
        "text": (255, 255, 255),
        "label": "月亮"
    },
    "unicorn": {
        "bg": (230, 190, 255),
        "accent": (255, 105, 180),
        "text": (75, 0, 130),
        "label": "独角兽"
    },
    "robot": {
        "bg": (50, 50, 80),
        "accent": (0, 255, 255),
        "text": (255, 255, 255),
        "label": "机器人"
    },
    "rocket": {
        "bg": (10, 10, 50),
        "accent": (255, 100, 0),
        "text": (255, 255, 255),
        "label": "火箭"
    },
    "magic": {
        "bg": (75, 0, 130),
        "accent": (255, 215, 0),
        "text": (255, 255, 255),
        "label": "魔法"
    },
    "panda": {
        "bg": (240, 240, 240),
        "accent": (50, 50, 50),
        "text": (50, 50, 50),
        "label": "熊猫"
    },
    "rabbit": {
        "bg": (255, 240, 245),
        "accent": (255, 182, 193),
        "text": (139, 0, 139),
        "label": "兔子"
    },
    "bear": {
        "bg": (139, 90, 43),
        "accent": (255, 215, 0),
        "text": (255, 255, 255),
        "label": "小熊"
    },
    "penguin": {
        "bg": (135, 206, 235),
        "accent": (255, 255, 255),
        "text": (0, 0, 0),
        "label": "企鹅"
    },
    "moonlit_forest": {
        "bg": (15, 25, 50),
        "accent": (200, 200, 255),
        "text": (255, 255, 255),
        "label": "月夜森林"
    },
    "prince": {
        "bg": (70, 130, 180),
        "accent": (255, 215, 0),
        "text": (255, 255, 255),
        "label": "王子"
    },
    "stars": {
        "bg": (10, 10, 40),
        "accent": (255, 255, 200),
        "text": (255, 255, 255),
        "label": "星星"
    },
}


class ImageGenerator:
    """插图生成器"""

    def __init__(self, assets_dir: str = None):
        self.assets_dir = assets_dir or os.path.join(
            os.path.dirname(__file__), "..", "assets", "illustrations"
        )
        os.makedirs(self.assets_dir, exist_ok=True)

    def generate_theme_image(self, theme: str) -> bytes:
        """
        根据主题生成插图（程序化绘制）

        Args:
            theme: 主题名称

        Returns:
            PNG 格式图片字节数据
        """
        colors = THEME_COLORS.get(theme, THEME_COLORS["moonlit_forest"])

        img = Image.new("RGB", (SCREEN_WIDTH, SCREEN_HEIGHT), colors["bg"])
        draw = ImageDraw.Draw(img)

        # 根据主题绘制不同的图案
        if theme == "space" or theme == "rocket" or theme == "stars":
            self._draw_space_scene(draw, colors)
        elif theme == "forest" or theme == "moonlit_forest":
            self._draw_forest_scene(draw, colors)
        elif theme == "ocean":
            self._draw_ocean_scene(draw, colors)
        elif theme == "castle" or theme == "princess" or theme == "prince":
            self._draw_castle_scene(draw, colors)
        elif theme == "moon":
            self._draw_moon_scene(draw, colors)
        elif theme == "dinosaur":
            self._draw_dinosaur_scene(draw, colors)
        else:
            self._draw_default_scene(draw, colors)

        # 添加主题标签
        self._draw_label(draw, colors["label"], colors["text"])

        # 转换为字节
        buffer = io.BytesIO()
        img.save(buffer, format="PNG")
        return buffer.getvalue()

    def generate_rgb565_image(self, theme: str) -> bytes:
        """
        生成 RGB565 格式图片（ESP32-S3 TFT 屏幕直接使用）

        Args:
            theme: 主题名称

        Returns:
            RGB565 格式字节数据
        """
        png_data = self.generate_theme_image(theme)
        img = Image.open(io.BytesIO(png_data)).convert("RGB")
        img = img.resize((SCREEN_WIDTH, SCREEN_HEIGHT))

        rgb565_data = bytearray()
        pixels = img.load()

        for y in range(SCREEN_HEIGHT):
            for x in range(SCREEN_WIDTH):
                r, g, b = pixels[x, y]
                # 转换为 RGB565
                r5 = (r >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                b5 = (b >> 3) & 0x1F
                rgb565 = (r5 << 11) | (g6 << 5) | b5
                # 大端字节序
                rgb565_data.append((rgb565 >> 8) & 0xFF)
                rgb565_data.append(rgb565 & 0xFF)

        return bytes(rgb565_data)

    def get_image_base64(self, theme: str) -> str:
        """获取 Base64 编码的图片"""
        png_data = self.generate_theme_image(theme)
        return base64.b64encode(png_data).decode("utf-8")

    def _draw_space_scene(self, draw: ImageDraw.Draw, colors: dict):
        """绘制太空场景"""
        import random
        random.seed(42)

        # 绘制星星
        for _ in range(60):
            x = random.randint(0, SCREEN_WIDTH)
            y = random.randint(0, SCREEN_HEIGHT - 40)
            size = random.randint(1, 3)
            brightness = random.randint(180, 255)
            draw.ellipse(
                [x - size, y - size, x + size, y + size],
                fill=(brightness, brightness, brightness)
            )

        # 绘制月亮
        moon_x, moon_y = 180, 50
        draw.ellipse([moon_x - 30, moon_y - 30, moon_x + 30, moon_y + 30],
                     fill=(255, 255, 200))
        # 月亮阴影
        draw.ellipse([moon_x - 20, moon_y - 35, moon_x + 25, moon_y + 25],
                     fill=colors["bg"])

        # 绘制火箭
        rocket_x, rocket_y = 100, 120
        # 火箭主体
        draw.polygon([
            (rocket_x, rocket_y - 40),
            (rocket_x - 15, rocket_y + 20),
            (rocket_x + 15, rocket_y + 20)
        ], fill=(200, 200, 220))
        # 火箭窗口
        draw.ellipse([rocket_x - 8, rocket_y - 10, rocket_x + 8, rocket_y + 6],
                     fill=(100, 200, 255))
        # 火焰
        draw.polygon([
            (rocket_x - 10, rocket_y + 20),
            (rocket_x, rocket_y + 50),
            (rocket_x + 10, rocket_y + 20)
        ], fill=(255, 100, 0))
        draw.polygon([
            (rocket_x - 6, rocket_y + 20),
            (rocket_x, rocket_y + 40),
            (rocket_x + 6, rocket_y + 20)
        ], fill=(255, 220, 0))

        # 绘制星球
        draw.ellipse([20, 60, 80, 120], fill=(180, 100, 50))
        draw.ellipse([10, 85, 90, 95], fill=(220, 150, 80), outline=(180, 100, 50))

    def _draw_forest_scene(self, draw: ImageDraw.Draw, colors: dict):
        """绘制森林场景"""
        # 天空渐变（简化）
        for y in range(SCREEN_HEIGHT // 2):
            ratio = y / (SCREEN_HEIGHT // 2)
            r = int(15 + ratio * 20)
            g = int(25 + ratio * 60)
            b = int(50 + ratio * 30)
            draw.line([(0, y), (SCREEN_WIDTH, y)], fill=(r, g, b))

        # 地面
        draw.rectangle([0, SCREEN_HEIGHT - 40, SCREEN_WIDTH, SCREEN_HEIGHT],
                        fill=(34, 85, 34))

        # 绘制树木
        tree_positions = [20, 60, 100, 150, 190, 220]
        for tx in tree_positions:
            height = 80 + (tx % 30)
            # 树干
            draw.rectangle([tx - 5, SCREEN_HEIGHT - 40 - 20, tx + 5, SCREEN_HEIGHT - 40],
                            fill=(101, 67, 33))
            # 树冠（三角形）
            draw.polygon([
                (tx, SCREEN_HEIGHT - 40 - height),
                (tx - 25, SCREEN_HEIGHT - 40 - 20),
                (tx + 25, SCREEN_HEIGHT - 40 - 20)
            ], fill=(0, 100, 0))
            draw.polygon([
                (tx, SCREEN_HEIGHT - 40 - height - 20),
                (tx - 20, SCREEN_HEIGHT - 40 - height + 20),
                (tx + 20, SCREEN_HEIGHT - 40 - height + 20)
            ], fill=(0, 128, 0))

        # 月亮
        draw.ellipse([170, 20, 210, 60], fill=(255, 255, 200))

        # 星星
        for sx, sy in [(30, 30), (80, 15), (130, 25), (50, 50)]:
            draw.polygon([
                (sx, sy - 8), (sx + 3, sy - 3), (sx + 8, sy - 3),
                (sx + 4, sy + 2), (sx + 6, sy + 8),
                (sx, sy + 4), (sx - 6, sy + 8),
                (sx - 4, sy + 2), (sx - 8, sy - 3), (sx - 3, sy - 3)
            ], fill=(255, 255, 150))

    def _draw_ocean_scene(self, draw: ImageDraw.Draw, colors: dict):
        """绘制海洋场景"""
        # 天空
        draw.rectangle([0, 0, SCREEN_WIDTH, SCREEN_HEIGHT // 2],
                        fill=(135, 206, 235))

        # 海浪
        wave_y = SCREEN_HEIGHT // 2
        for y in range(wave_y, SCREEN_HEIGHT):
            ratio = (y - wave_y) / (SCREEN_HEIGHT - wave_y)
            b = int(148 - ratio * 80)
            draw.line([(0, y), (SCREEN_WIDTH, y)], fill=(0, int(105 + ratio * 30), b))

        # 波浪线
        import math
        for wave_offset in range(0, SCREEN_HEIGHT - wave_y, 20):
            points = []
            for x in range(0, SCREEN_WIDTH + 10, 5):
                y = wave_y + wave_offset + int(8 * math.sin(x * 0.1))
                points.append((x, y))
            if len(points) > 1:
                draw.line(points, fill=(100, 180, 220), width=2)

        # 太阳
        draw.ellipse([170, 20, 220, 70], fill=(255, 220, 0))

        # 海豚
        dolphin_x, dolphin_y = 100, 140
        draw.ellipse([dolphin_x - 40, dolphin_y - 15, dolphin_x + 40, dolphin_y + 15],
                     fill=(70, 130, 180))
        # 背鳍
        draw.polygon([
            (dolphin_x, dolphin_y - 15),
            (dolphin_x + 10, dolphin_y - 35),
            (dolphin_x + 20, dolphin_y - 15)
        ], fill=(70, 130, 180))
        # 尾巴
        draw.polygon([
            (dolphin_x - 40, dolphin_y),
            (dolphin_x - 55, dolphin_y - 15),
            (dolphin_x - 55, dolphin_y + 15)
        ], fill=(70, 130, 180))
        # 眼睛
        draw.ellipse([dolphin_x + 25, dolphin_y - 8, dolphin_x + 32, dolphin_y - 1],
                     fill=(255, 255, 255))
        draw.ellipse([dolphin_x + 27, dolphin_y - 6, dolphin_x + 30, dolphin_y - 3],
                     fill=(0, 0, 0))

    def _draw_castle_scene(self, draw: ImageDraw.Draw, colors: dict):
        """绘制城堡场景"""
        # 夜空
        draw.rectangle([0, 0, SCREEN_WIDTH, SCREEN_HEIGHT], fill=(25, 25, 80))

        # 星星
        for sx, sy in [(20, 20), (60, 10), (100, 30), (160, 15), (200, 25), (40, 50)]:
            draw.ellipse([sx - 2, sy - 2, sx + 2, sy + 2], fill=(255, 255, 200))

        # 月亮
        draw.ellipse([180, 20, 220, 60], fill=(255, 255, 180))

        # 城堡主体
        castle_base_y = SCREEN_HEIGHT - 60
        # 主塔
        draw.rectangle([80, castle_base_y - 80, 160, castle_base_y],
                        fill=(120, 120, 140))
        # 左塔
        draw.rectangle([50, castle_base_y - 60, 85, castle_base_y],
                        fill=(100, 100, 120))
        # 右塔
        draw.rectangle([155, castle_base_y - 60, 190, castle_base_y],
                        fill=(100, 100, 120))

        # 城垛
        for cx in range(80, 160, 15):
            draw.rectangle([cx, castle_base_y - 90, cx + 10, castle_base_y - 80],
                            fill=(120, 120, 140))
        for cx in range(50, 85, 12):
            draw.rectangle([cx, castle_base_y - 70, cx + 8, castle_base_y - 60],
                            fill=(100, 100, 120))
        for cx in range(155, 190, 12):
            draw.rectangle([cx, castle_base_y - 70, cx + 8, castle_base_y - 60],
                            fill=(100, 100, 120))

        # 城门
        draw.rectangle([105, castle_base_y - 40, 135, castle_base_y],
                        fill=(50, 30, 10))
        draw.ellipse([105, castle_base_y - 55, 135, castle_base_y - 25],
                     fill=(50, 30, 10))

        # 窗户（发光）
        for wx, wy in [(95, castle_base_y - 65), (145, castle_base_y - 65),
                       (60, castle_base_y - 45), (165, castle_base_y - 45)]:
            draw.rectangle([wx, wy, wx + 12, wy + 15], fill=(255, 220, 100))

        # 地面
        draw.rectangle([0, castle_base_y, SCREEN_WIDTH, SCREEN_HEIGHT],
                        fill=(30, 60, 30))

    def _draw_moon_scene(self, draw: ImageDraw.Draw, colors: dict):
        """绘制月夜场景"""
        # 渐变夜空
        for y in range(SCREEN_HEIGHT):
            ratio = y / SCREEN_HEIGHT
            r = int(10 + ratio * 15)
            g = int(10 + ratio * 20)
            b = int(50 + ratio * 30)
            draw.line([(0, y), (SCREEN_WIDTH, y)], fill=(r, g, b))

        # 大月亮
        draw.ellipse([70, 30, 170, 130], fill=(255, 255, 200))
        # 月亮纹理
        draw.ellipse([90, 50, 120, 80], fill=(240, 240, 180))
        draw.ellipse([130, 70, 155, 95], fill=(240, 240, 180))
        draw.ellipse([80, 90, 105, 115], fill=(240, 240, 180))

        # 星星
        import random
        random.seed(123)
        for _ in range(30):
            sx = random.randint(0, SCREEN_WIDTH)
            sy = random.randint(0, SCREEN_HEIGHT)
            if not (60 < sx < 180 and 20 < sy < 140):  # 避开月亮
                size = random.randint(1, 2)
                draw.ellipse([sx - size, sy - size, sx + size, sy + size],
                             fill=(255, 255, 200))

        # 地面和草地
        draw.rectangle([0, SCREEN_HEIGHT - 50, SCREEN_WIDTH, SCREEN_HEIGHT],
                        fill=(20, 50, 20))
        # 草
        for gx in range(0, SCREEN_WIDTH, 15):
            draw.line([(gx, SCREEN_HEIGHT - 50), (gx - 5, SCREEN_HEIGHT - 65)],
                      fill=(30, 80, 30), width=2)
            draw.line([(gx + 5, SCREEN_HEIGHT - 50), (gx + 10, SCREEN_HEIGHT - 68)],
                      fill=(30, 80, 30), width=2)

        # 小屋
        draw.rectangle([20, SCREEN_HEIGHT - 90, 80, SCREEN_HEIGHT - 50],
                        fill=(80, 50, 30))
        draw.polygon([
            (10, SCREEN_HEIGHT - 90),
            (50, SCREEN_HEIGHT - 120),
            (90, SCREEN_HEIGHT - 90)
        ], fill=(120, 60, 40))
        draw.rectangle([35, SCREEN_HEIGHT - 75, 55, SCREEN_HEIGHT - 50],
                        fill=(50, 30, 10))
        draw.rectangle([22, SCREEN_HEIGHT - 80, 38, SCREEN_HEIGHT - 65],
                        fill=(255, 220, 100))

    def _draw_dinosaur_scene(self, draw: ImageDraw.Draw, colors: dict):
        """绘制恐龙场景"""
        # 天空
        draw.rectangle([0, 0, SCREEN_WIDTH, SCREEN_HEIGHT * 2 // 3],
                        fill=(100, 180, 100))

        # 太阳
        draw.ellipse([170, 20, 210, 60], fill=(255, 220, 0))

        # 地面
        draw.rectangle([0, SCREEN_HEIGHT * 2 // 3, SCREEN_WIDTH, SCREEN_HEIGHT],
                        fill=(34, 139, 34))

        # 树木（史前）
        for tx in [30, 180, 210]:
            draw.rectangle([tx - 8, 80, tx + 8, SCREEN_HEIGHT * 2 // 3],
                            fill=(101, 67, 33))
            draw.ellipse([tx - 30, 40, tx + 30, 100], fill=(0, 120, 0))

        # 恐龙（简化卡通）
        dino_x, dino_y = 110, 130
        # 身体
        draw.ellipse([dino_x - 35, dino_y - 20, dino_x + 35, dino_y + 30],
                     fill=(50, 160, 50))
        # 头
        draw.ellipse([dino_x + 15, dino_y - 45, dino_x + 55, dino_y - 10],
                     fill=(50, 160, 50))
        # 眼睛
        draw.ellipse([dino_x + 35, dino_y - 40, dino_x + 45, dino_y - 30],
                     fill=(255, 255, 255))
        draw.ellipse([dino_x + 38, dino_y - 37, dino_x + 43, dino_y - 32],
                     fill=(0, 0, 0))
        # 嘴巴（微笑）
        draw.arc([dino_x + 25, dino_y - 30, dino_x + 50, dino_y - 15],
                 start=0, end=180, fill=(0, 100, 0), width=2)
        # 尾巴
        draw.polygon([
            (dino_x - 35, dino_y + 10),
            (dino_x - 70, dino_y + 30),
            (dino_x - 35, dino_y + 30)
        ], fill=(50, 160, 50))
        # 腿
        draw.rectangle([dino_x - 20, dino_y + 30, dino_x - 5, dino_y + 55],
                        fill=(40, 140, 40))
        draw.rectangle([dino_x + 5, dino_y + 30, dino_x + 20, dino_y + 55],
                        fill=(40, 140, 40))
        # 背刺
        for i, (sx, sy) in enumerate([(dino_x - 20, dino_y - 20),
                                       (dino_x, dino_y - 30),
                                       (dino_x + 15, dino_y - 20)]):
            draw.polygon([
                (sx, sy - 15 - i * 3),
                (sx - 8, sy),
                (sx + 8, sy)
            ], fill=(30, 130, 30))

    def _draw_default_scene(self, draw: ImageDraw.Draw, colors: dict):
        """绘制默认场景（月夜）"""
        self._draw_moon_scene(draw, colors)

    def _draw_label(self, draw: ImageDraw.Draw, label: str, text_color: tuple):
        """在图片底部绘制标签"""
        # 半透明背景条
        draw.rectangle(
            [0, SCREEN_HEIGHT - 30, SCREEN_WIDTH, SCREEN_HEIGHT],
            fill=(0, 0, 0)
        )

        # 尝试加载字体，失败则使用默认字体
        try:
            # 尝试使用系统中文字体
            font_paths = [
                "C:/Windows/Fonts/msyh.ttc",
                "C:/Windows/Fonts/simhei.ttf",
                "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
                "/System/Library/Fonts/PingFang.ttc",
            ]
            font = None
            for fp in font_paths:
                if os.path.exists(fp):
                    font = ImageFont.truetype(fp, 16)
                    break
            if font is None:
                font = ImageFont.load_default()
        except Exception:
            font = ImageFont.load_default()

        # 绘制文字
        try:
            bbox = draw.textbbox((0, 0), label, font=font)
            text_width = bbox[2] - bbox[0]
        except Exception:
            text_width = len(label) * 8

        x = (SCREEN_WIDTH - text_width) // 2
        draw.text((x, SCREEN_HEIGHT - 25), label, fill=text_color, font=font)

    def save_image(self, theme: str, output_path: str) -> bool:
        """保存插图到文件"""
        try:
            png_data = self.generate_theme_image(theme)
            with open(output_path, "wb") as f:
                f.write(png_data)
            logger.info(f"插图已保存: {output_path}")
            return True
        except Exception as e:
            logger.error(f"保存插图失败: {e}")
            return False

    def pregenerate_all(self):
        """预生成所有主题插图"""
        for theme in THEME_COLORS:
            output_path = os.path.join(self.assets_dir, f"{theme}.png")
            self.save_image(theme, output_path)
            logger.info(f"预生成插图: {theme}")


class DisplayMessage:
    """屏幕显示消息格式"""

    @staticmethod
    def welcome() -> dict:
        return {
            "type": "display",
            "screen": "welcome",
            "text": "晚安故事机",
            "subtext": "说出你想听的故事"
        }

    @staticmethod
    def listening() -> dict:
        return {
            "type": "display",
            "screen": "listening",
            "text": "正在聆听...",
            "subtext": "请说出故事关键词"
        }

    @staticmethod
    def generating(keywords: list) -> dict:
        return {
            "type": "display",
            "screen": "generating",
            "text": "正在创作故事...",
            "subtext": "、".join(keywords[:3])
        }

    @staticmethod
    def playing(theme: str, image_data: str) -> dict:
        return {
            "type": "display",
            "screen": "playing",
            "theme": theme,
            "image": image_data  # base64 encoded
        }

    @staticmethod
    def sleeping() -> dict:
        return {
            "type": "display",
            "screen": "sleeping",
            "text": "晚安~",
            "subtext": "甜甜的梦"
        }


if __name__ == "__main__":
    gen = ImageGenerator()

    print("生成所有主题插图...")
    for theme in list(THEME_COLORS.keys())[:5]:
        output = f"test_{theme}.png"
        gen.save_image(theme, output)
        print(f"已生成: {output}")

    print("完成！")