"""
AI 睡前故事机 - 后端主服务器
FastAPI + WebSocket 实时通信
"""

import asyncio
import base64
import json
import logging
import os
import uuid
from contextlib import asynccontextmanager
from typing import Dict, Optional

import aiofiles
from dotenv import load_dotenv
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException, UploadFile, File
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, JSONResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from image_generator import ImageGenerator, DisplayMessage, THEME_COLORS
from keyword_extractor import extract_keywords, get_illustration_prompt
from story_generator import StoryGenerator, StorySession
from stt_engine import STTEngine
from tts_engine import TTSEngine

# 加载环境变量
load_dotenv()

# 配置日志
logging.basicConfig(
    level=getattr(logging, os.getenv("LOG_LEVEL", "INFO")),
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s"
)
logger = logging.getLogger(__name__)

# 全局组件
story_generator: Optional[StoryGenerator] = None
tts_engine: Optional[TTSEngine] = None
stt_engine: Optional[STTEngine] = None
image_generator: Optional[ImageGenerator] = None

# WebSocket 连接管理
active_connections: Dict[str, WebSocket] = {}
device_sessions: Dict[str, StorySession] = {}


@asynccontextmanager
async def lifespan(app: FastAPI):
    """应用生命周期管理"""
    global story_generator, tts_engine, stt_engine, image_generator

    logger.info("🌙 AI 睡前故事机服务器启动中...")

    # 初始化组件
    api_key = os.getenv("OPENAI_API_KEY", "")
    base_url = os.getenv("OPENAI_BASE_URL", "https://api.openai.com/v1")
    model = os.getenv("OPENAI_MODEL", "gpt-4o")

    story_generator = StoryGenerator(
        api_key=api_key,
        base_url=base_url,
        model=model
    )

    tts_engine = TTSEngine(
        voice=os.getenv("TTS_VOICE", "zh-CN-XiaoxiaoNeural"),
        rate=os.getenv("TTS_RATE", "-10%"),
        volume=os.getenv("TTS_VOLUME", "+0%"),
    )

    stt_engine = STTEngine(
        api_key=api_key,
        base_url=base_url
    )

    image_generator = ImageGenerator()

    # 预生成插图
    logger.info("预生成主题插图...")
    for theme in list(THEME_COLORS.keys()):
        output_path = os.path.join(
            os.path.dirname(__file__), "..", "assets", "illustrations", f"{theme}.png"
        )
        os.makedirs(os.path.dirname(output_path), exist_ok=True)
        if not os.path.exists(output_path):
            image_generator.save_image(theme, output_path)

    logger.info("✅ 服务器启动完成！")

    yield

    logger.info("服务器关闭中...")


# 创建 FastAPI 应用
app = FastAPI(
    title="AI 睡前故事机",
    description="基于 ESP32-S3 的智能睡前故事机后端服务",
    version="1.0.0",
    lifespan=lifespan
)

# CORS 配置
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# 静态文件服务
assets_dir = os.path.join(os.path.dirname(__file__), "..", "assets")
os.makedirs(assets_dir, exist_ok=True)
app.mount("/assets", StaticFiles(directory=assets_dir), name="assets")

web_dir = os.path.join(os.path.dirname(__file__), "..", "web")
if os.path.exists(web_dir):
    app.mount("/web", StaticFiles(directory=web_dir, html=True), name="web")


# ==================== REST API ====================

class StoryRequest(BaseModel):
    keywords: list[str]
    voice: Optional[str] = None


class TextRequest(BaseModel):
    text: str


@app.get("/")
async def root():
    """根路径"""
    return {
        "name": "AI 睡前故事机",
        "version": "1.0.0",
        "status": "running",
        "endpoints": {
            "websocket": "/ws/{device_id}",
            "story": "/api/story",
            "tts": "/api/tts",
            "stt": "/api/stt",
            "image": "/api/image/{theme}",
            "web": "/web"
        }
    }


@app.get("/health")
async def health_check():
    """健康检查"""
    return {"status": "ok", "connections": len(active_connections)}


@app.post("/api/story")
async def generate_story(request: StoryRequest):
    """
    生成故事（非流式）
    
    Body: {"keywords": ["恐龙", "森林"]}
    """
    if not request.keywords:
        raise HTTPException(status_code=400, detail="关键词不能为空")

    story = await story_generator.generate_story_full(request.keywords)
    theme = get_illustration_prompt(request.keywords)

    return {
        "story": story,
        "keywords": request.keywords,
        "theme": theme,
        "length": len(story)
    }


@app.get("/api/story/stream")
async def stream_story(keywords: str):
    """
    流式生成故事
    
    Query: ?keywords=恐龙,森林
    """
    kw_list = [k.strip() for k in keywords.split(",") if k.strip()]
    if not kw_list:
        raise HTTPException(status_code=400, detail="关键词不能为空")

    async def generate():
        async for chunk in story_generator.generate_story_stream(kw_list):
            yield f"data: {json.dumps({'text': chunk}, ensure_ascii=False)}\n\n"
        yield "data: [DONE]\n\n"

    return StreamingResponse(
        generate(),
        media_type="text/event-stream",
        headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"}
    )


@app.post("/api/tts")
async def text_to_speech(request: TextRequest):
    """
    文字转语音
    
    Body: {"text": "故事内容..."}
    Returns: MP3 音频文件
    """
    if not request.text.strip():
        raise HTTPException(status_code=400, detail="文本不能为空")

    audio_data = await tts_engine.synthesize_to_bytes(request.text)

    if not audio_data:
        raise HTTPException(status_code=500, detail="语音合成失败")

    return StreamingResponse(
        iter([audio_data]),
        media_type="audio/mpeg",
        headers={"Content-Disposition": "attachment; filename=story.mp3"}
    )


@app.post("/api/stt")
async def speech_to_text(audio: UploadFile = File(...)):
    """
    语音转文字
    
    Form: audio file (WAV/MP3)
    """
    audio_data = await audio.read()

    # 检测文件类型
    if audio.filename and audio.filename.endswith(".wav"):
        # WAV 文件，提取 PCM 数据
        import wave
        import io
        with wave.open(io.BytesIO(audio_data)) as wav:
            pcm_data = wav.readframes(wav.getnframes())
            sample_rate = wav.getframerate()
        text = await stt_engine.transcribe_audio(pcm_data, sample_rate)
    else:
        # 直接保存并识别
        import tempfile
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
            tmp.write(audio_data)
            tmp_path = tmp.name

        try:
            text = await stt_engine.transcribe_file(tmp_path)
        finally:
            os.unlink(tmp_path)

    # 提取关键词
    keywords_result = extract_keywords(text)

    return {
        "text": text,
        "keywords": keywords_result["keywords"],
        "categories": keywords_result["categories"]
    }


@app.get("/api/image/{theme}")
async def get_illustration(theme: str, format: str = "png"):
    """
    获取主题插图
    
    Path: /api/image/dinosaur
    Query: ?format=png|rgb565|base64
    """
    if theme not in THEME_COLORS:
        theme = "moonlit_forest"

    if format == "rgb565":
        rgb565_data = image_generator.generate_rgb565_image(theme)
        return StreamingResponse(
            iter([rgb565_data]),
            media_type="application/octet-stream",
            headers={"Content-Disposition": f"attachment; filename={theme}.rgb565"}
        )
    elif format == "base64":
        b64_data = image_generator.get_image_base64(theme)
        return {"theme": theme, "format": "base64", "data": b64_data}
    else:
        # 检查预生成的文件
        img_path = os.path.join(assets_dir, "illustrations", f"{theme}.png")
        if os.path.exists(img_path):
            return FileResponse(img_path, media_type="image/png")
        else:
            png_data = image_generator.generate_theme_image(theme)
            return StreamingResponse(
                iter([png_data]),
                media_type="image/png"
            )


@app.get("/api/themes")
async def list_themes():
    """列出所有可用主题"""
    return {
        "themes": list(THEME_COLORS.keys()),
        "count": len(THEME_COLORS)
    }


@app.get("/api/voices")
async def list_voices():
    """列出可用的 TTS 声音"""
    from tts_engine import CHINESE_VOICES
    return {"voices": CHINESE_VOICES}


@app.post("/api/keywords")
async def extract_story_keywords(request: TextRequest):
    """从文本中提取故事关键词"""
    result = extract_keywords(request.text)
    theme = get_illustration_prompt(result["keywords"])
    return {
        **result,
        "theme": theme
    }


# ==================== WebSocket ====================

@app.websocket("/ws/{device_id}")
async def websocket_endpoint(websocket: WebSocket, device_id: str):
    """
    ESP32-S3 设备 WebSocket 连接端点
    
    消息协议:
    - 设备 -> 服务器: {"type": "audio", "data": "<base64 PCM>", "sample_rate": 16000}
    - 设备 -> 服务器: {"type": "keywords", "keywords": ["恐龙", "太空"]}
    - 设备 -> 服务器: {"type": "ping"}
    
    - 服务器 -> 设备: {"type": "display", "screen": "...", ...}
    - 服务器 -> 设备: {"type": "story_text", "text": "...", "is_last": false}
    - 服务器 -> 设备: {"type": "audio_chunk", "data": "<base64 MP3>", "index": 0}
    - 服务器 -> 设备: {"type": "story_complete"}
    - 服务器 -> 设备: {"type": "pong"}
    - 服务器 -> 设备: {"type": "error", "message": "..."}
    """
    await websocket.accept()
    active_connections[device_id] = websocket
    device_sessions[device_id] = StorySession()
    device_sessions[device_id].session_id = device_id

    logger.info(f"设备连接: {device_id}，当前连接数: {len(active_connections)}")

    # 发送欢迎消息
    await send_message(websocket, DisplayMessage.welcome())

    try:
        while True:
            # 接收消息
            raw_data = await websocket.receive_text()

            try:
                message = json.loads(raw_data)
            except json.JSONDecodeError:
                await send_error(websocket, "无效的 JSON 格式")
                continue

            msg_type = message.get("type", "")

            if msg_type == "ping":
                await send_message(websocket, {"type": "pong"})

            elif msg_type == "audio":
                # 处理音频数据（语音识别）
                await handle_audio_message(websocket, device_id, message)

            elif msg_type == "keywords":
                # 直接接收关键词（文本模式）
                keywords = message.get("keywords", [])
                if keywords:
                    await handle_story_request(websocket, device_id, keywords)
                else:
                    await send_error(websocket, "关键词不能为空")

            elif msg_type == "text":
                # 接收文本，提取关键词
                text = message.get("text", "")
                if text:
                    result = extract_keywords(text)
                    keywords = result["keywords"]
                    if not keywords:
                        # 使用 AI 提取
                        keywords = await story_generator.extract_keywords_with_ai(text)
                    if keywords:
                        await handle_story_request(websocket, device_id, keywords)
                    else:
                        await send_error(websocket, "未能识别故事关键词，请重试")

            elif msg_type == "stop":
                # 停止当前故事
                session = device_sessions.get(device_id)
                if session:
                    session.is_playing = False
                await send_message(websocket, {"type": "stopped"})

            elif msg_type == "status":
                # 查询状态
                session = device_sessions.get(device_id)
                await send_message(websocket, {
                    "type": "status",
                    "is_playing": session.is_playing if session else False,
                    "keywords": session.keywords if session else []
                })

            else:
                logger.warning(f"未知消息类型: {msg_type}")

    except WebSocketDisconnect:
        logger.info(f"设备断开连接: {device_id}")
    except Exception as e:
        logger.error(f"WebSocket 错误 [{device_id}]: {e}")
    finally:
        active_connections.pop(device_id, None)
        device_sessions.pop(device_id, None)
        logger.info(f"清理连接: {device_id}，剩余连接数: {len(active_connections)}")


async def handle_audio_message(websocket: WebSocket, device_id: str, message: dict):
    """处理音频消息（语音识别 -> 关键词提取 -> 故事生成）"""
    try:
        # 解码音频数据
        audio_b64 = message.get("data", "")
        sample_rate = message.get("sample_rate", 16000)

        if not audio_b64:
            await send_error(websocket, "音频数据为空")
            return

        audio_data = base64.b64decode(audio_b64)

        # 检测语音活动
        if not stt_engine.detect_voice_activity(audio_data):
            await send_message(websocket, {
                "type": "vad_result",
                "has_voice": False
            })
            return

        # 显示"正在识别"
        await send_message(websocket, {
            "type": "display",
            "screen": "recognizing",
            "text": "正在识别...",
        })

        # 语音识别
        text = await stt_engine.transcribe_audio(audio_data, sample_rate)

        if not text:
            await send_error(websocket, "语音识别失败，请重试")
            return

        logger.info(f"[{device_id}] 识别结果: {text}")

        # 提取关键词
        result = extract_keywords(text)
        keywords = result["keywords"]

        if not keywords:
            keywords = await story_generator.extract_keywords_with_ai(text)

        if not keywords:
            await send_message(websocket, {
                "type": "no_keywords",
                "text": text,
                "message": "未识别到故事关键词"
            })
            return

        # 开始生成故事
        await handle_story_request(websocket, device_id, keywords)

    except Exception as e:
        logger.error(f"处理音频消息失败: {e}")
        await send_error(websocket, f"处理失败: {str(e)}")


async def handle_story_request(
    websocket: WebSocket,
    device_id: str,
    keywords: list
):
    """处理故事生成请求"""
    session = device_sessions.get(device_id)
    if not session:
        return

    session.reset()
    session.keywords = keywords
    session.is_playing = True

    logger.info(f"[{device_id}] 开始生成故事，关键词: {keywords}")

    # 获取主题和插图
    theme = get_illustration_prompt(keywords)

    # 发送"正在生成"显示
    await send_message(websocket, DisplayMessage.generating(keywords))

    # 发送插图
    try:
        image_b64 = image_generator.get_image_base64(theme)
        await send_message(websocket, {
            "type": "illustration",
            "theme": theme,
            "image": image_b64,
            "keywords": keywords
        })
    except Exception as e:
        logger.warning(f"发送插图失败: {e}")

    # 流式生成故事文本
    full_story = ""
    story_buffer = ""
    sentence_index = 0

    try:
        async for text_chunk in story_generator.generate_story_stream(keywords):
            if not session.is_playing:
                break

            full_story += text_chunk
            story_buffer += text_chunk
            session.append_story(text_chunk)

            # 发送文本片段给设备显示
            await send_message(websocket, {
                "type": "story_text",
                "text": text_chunk,
                "is_last": False
            })

            # 当积累了足够的文本（一句话），合成语音
            if any(c in story_buffer for c in ['。', '！', '？', '\n']):
                # 找到句子边界
                for delimiter in ['。', '！', '？']:
                    if delimiter in story_buffer:
                        parts = story_buffer.split(delimiter, 1)
                        sentence = parts[0] + delimiter
                        story_buffer = parts[1] if len(parts) > 1 else ""

                        if sentence.strip():
                            # 合成这句话的语音
                            audio_data = await tts_engine.synthesize_to_bytes(sentence)
                            if audio_data:
                                audio_b64 = base64.b64encode(audio_data).decode("utf-8")
                                await send_message(websocket, {
                                    "type": "audio_chunk",
                                    "data": audio_b64,
                                    "index": sentence_index,
                                    "text": sentence,
                                    "is_last": False
                                })
                                sentence_index += 1
                        break

        # 处理剩余文本
        if story_buffer.strip() and session.is_playing:
            audio_data = await tts_engine.synthesize_to_bytes(story_buffer)
            if audio_data:
                audio_b64 = base64.b64encode(audio_data).decode("utf-8")
                await send_message(websocket, {
                    "type": "audio_chunk",
                    "data": audio_b64,
                    "index": sentence_index,
                    "text": story_buffer,
                    "is_last": True
                })

        # 故事完成
        await send_message(websocket, {
            "type": "story_text",
            "text": "",
            "is_last": True
        })

        await send_message(websocket, {
            "type": "story_complete",
            "keywords": keywords,
            "theme": theme,
            "story_length": len(full_story)
        })

        # 发送睡眠界面
        await asyncio.sleep(2)
        await send_message(websocket, DisplayMessage.sleeping())

        logger.info(f"[{device_id}] 故事生成完成，共 {len(full_story)} 字")

    except Exception as e:
        logger.error(f"[{device_id}] 故事生成失败: {e}")
        await send_error(websocket, f"故事生成失败: {str(e)}")
    finally:
        session.is_playing = False


async def send_message(websocket: WebSocket, data: dict):
    """发送 JSON 消息"""
    try:
        await websocket.send_text(json.dumps(data, ensure_ascii=False))
    except Exception as e:
        logger.error(f"发送消息失败: {e}")


async def send_error(websocket: WebSocket, message: str):
    """发送错误消息"""
    await send_message(websocket, {
        "type": "error",
        "message": message
    })


# ==================== 广播功能 ====================

async def broadcast_to_all(data: dict):
    """广播消息给所有连接的设备"""
    disconnected = []
    for device_id, ws in active_connections.items():
        try:
            await ws.send_text(json.dumps(data, ensure_ascii=False))
        except Exception:
            disconnected.append(device_id)

    for device_id in disconnected:
        active_connections.pop(device_id, None)


# ==================== 启动入口 ====================

if __name__ == "__main__":
    import uvicorn

    host = os.getenv("SERVER_HOST", "0.0.0.0")
    port = int(os.getenv("SERVER_PORT", "8000"))

    logger.info(f"启动服务器: http://{host}:{port}")
    logger.info(f"WebSocket: ws://{host}:{port}/ws/{{device_id}}")
    logger.info(f"Web 界面: http://{host}:{port}/web")

    uvicorn.run(
        "app:app",
        host=host,
        port=port,
        reload=False,
        log_level=os.getenv("LOG_LEVEL", "info").lower()
    )