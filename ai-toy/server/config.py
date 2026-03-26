"""
AI玩具后端服务器配置文件

复制此文件为 config.py 并填入你的 API Keys
"""

# ============================================================
# 服务器配置
# ============================================================
SERVER_HOST = "0.0.0.0"   # 监听所有网卡
SERVER_PORT = 8765          # WebSocket端口

# ============================================================
# ASR 语音识别配置
# 选择一个提供商: "baidu" | "aliyun" | "xunfei" | "whisper"
# ============================================================
ASR_PROVIDER = "baidu"

# 百度智能云 ASR
BAIDU_ASR_APP_ID = "your_baidu_app_id"
BAIDU_ASR_API_KEY = "your_baidu_api_key"
BAIDU_ASR_SECRET_KEY = "your_baidu_secret_key"

# 阿里云 ASR (NLS)
ALIYUN_ASR_APP_KEY = "your_aliyun_app_key"
ALIYUN_ASR_ACCESS_KEY_ID = "your_aliyun_access_key_id"
ALIYUN_ASR_ACCESS_KEY_SECRET = "your_aliyun_access_key_secret"

# 讯飞 ASR
XUNFEI_ASR_APP_ID = "your_xunfei_app_id"
XUNFEI_ASR_API_KEY = "your_xunfei_api_key"
XUNFEI_ASR_API_SECRET = "your_xunfei_api_secret"

# OpenAI Whisper (本地或API)
WHISPER_MODEL = "base"          # tiny/base/small/medium/large
WHISPER_LANGUAGE = "zh"         # 语言代码
OPENAI_API_KEY = "your_openai_api_key"  # 使用OpenAI API时需要

# ============================================================
# LLM 大语言模型配置
# 选择一个提供商: "openai" | "qianwen" | "wenxin" | "zhipu" | "ollama"
# ============================================================
LLM_PROVIDER = "openai"

# OpenAI / 兼容API
OPENAI_API_KEY = "your_openai_api_key"
OPENAI_BASE_URL = "https://api.openai.com/v1"  # 可替换为国内代理
OPENAI_MODEL = "gpt-3.5-turbo"

# 通义千问 (阿里云)
QIANWEN_API_KEY = "your_qianwen_api_key"
QIANWEN_MODEL = "qwen-turbo"

# 文心一言 (百度)
WENXIN_API_KEY = "your_wenxin_api_key"
WENXIN_SECRET_KEY = "your_wenxin_secret_key"
WENXIN_MODEL = "ernie-bot-turbo"

# 智谱AI (GLM)
ZHIPU_API_KEY = "your_zhipu_api_key"
ZHIPU_MODEL = "glm-4-flash"

# Ollama (本地部署)
OLLAMA_BASE_URL = "http://localhost:11434"
OLLAMA_MODEL = "qwen2.5:7b"

# ============================================================
# LLM 系统提示词
# ============================================================
LLM_SYSTEM_PROMPT = """你是一个可爱的AI玩具助手，名叫"小智"。
你的特点：
- 声音活泼可爱，语气亲切友好
- 回答简洁明了，适合语音播报（不超过100字）
- 擅长陪伴聊天、讲故事、回答问题
- 对小朋友友好，语言简单易懂
- 不回答不适合儿童的内容

请用简短的语言回答，因为你的回复会被转换成语音播放。"""

LLM_MAX_TOKENS = 200           # 最大回复token数
LLM_TEMPERATURE = 0.8          # 创造性 (0.0-1.0)
LLM_HISTORY_MAX = 10           # 保留的对话历史轮数

# ============================================================
# TTS 语音合成配置
# 选择一个提供商: "baidu" | "aliyun" | "xunfei" | "edge" | "openai"
# ============================================================
TTS_PROVIDER = "edge"

# 百度智能云 TTS
BAIDU_TTS_APP_ID = "your_baidu_app_id"
BAIDU_TTS_API_KEY = "your_baidu_api_key"
BAIDU_TTS_SECRET_KEY = "your_baidu_secret_key"
BAIDU_TTS_VOICE = 0             # 0=度小美, 1=度小宇, 3=度逍遥, 4=度丫丫
BAIDU_TTS_SPEED = 5             # 语速 (0-15)
BAIDU_TTS_PITCH = 5             # 音调 (0-15)
BAIDU_TTS_VOLUME = 5            # 音量 (0-15)

# 阿里云 TTS
ALIYUN_TTS_APP_KEY = "your_aliyun_app_key"
ALIYUN_TTS_ACCESS_KEY_ID = "your_aliyun_access_key_id"
ALIYUN_TTS_ACCESS_KEY_SECRET = "your_aliyun_access_key_secret"
ALIYUN_TTS_VOICE = "xiaoyun"    # 发音人

# 讯飞 TTS
XUNFEI_TTS_APP_ID = "your_xunfei_app_id"
XUNFEI_TTS_API_KEY = "your_xunfei_api_key"
XUNFEI_TTS_API_SECRET = "your_xunfei_api_secret"
XUNFEI_TTS_VOICE = "xiaoyan"    # 发音人

# Edge TTS (微软免费TTS，无需API Key)
EDGE_TTS_VOICE = "zh-CN-XiaoxiaoNeural"   # 晓晓 (女声，推荐)
# 其他可选: zh-CN-YunxiNeural (云希男声), zh-CN-XiaoyiNeural (晓伊女声)

# OpenAI TTS
OPENAI_TTS_MODEL = "tts-1"
OPENAI_TTS_VOICE = "nova"       # alloy/echo/fable/onyx/nova/shimmer

# ============================================================
# 音频配置
# ============================================================
AUDIO_SAMPLE_RATE = 16000       # 采样率 (Hz)
AUDIO_CHANNELS = 1              # 声道数
AUDIO_SAMPLE_WIDTH = 2          # 采样位宽 (字节, 2=16bit)

# TTS输出格式 (发送给ESP32的格式)
TTS_OUTPUT_SAMPLE_RATE = 16000  # 输出采样率
TTS_CHUNK_SIZE = 4096           # 每次发送的音频块大小 (字节)

# ============================================================
# 日志配置
# ============================================================
LOG_LEVEL = "INFO"              # DEBUG/INFO/WARNING/ERROR
LOG_FILE = None                 # None=只输出到控制台, 或指定文件路径