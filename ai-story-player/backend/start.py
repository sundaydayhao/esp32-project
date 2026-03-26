#!/usr/bin/env python3
"""
AI 睡前故事机 - 快速启动脚本
自动检查依赖并启动服务器
"""

import os
import sys
import subprocess


def check_env():
    """检查环境变量配置"""
    env_file = os.path.join(os.path.dirname(__file__), '.env')
    if not os.path.exists(env_file):
        print("⚠️  未找到 .env 文件，正在从模板创建...")
        example_file = os.path.join(os.path.dirname(__file__), '.env.example')
        if os.path.exists(example_file):
            import shutil
            shutil.copy(example_file, env_file)
            print("✅ 已创建 .env 文件，请编辑并填入 OpenAI API Key")
            print(f"   文件位置: {env_file}")
        return False

    # 检查 API Key
    from dotenv import load_dotenv
    load_dotenv(env_file)
    api_key = os.getenv('OPENAI_API_KEY', '')
    if not api_key or api_key == 'your_openai_api_key_here':
        print("⚠️  请在 .env 文件中设置有效的 OPENAI_API_KEY")
        print(f"   文件位置: {env_file}")
        return False

    return True


def check_dependencies():
    """检查 Python 依赖"""
    try:
        import fastapi
        import uvicorn
        import openai
        import edge_tts
        from PIL import Image
        print("✅ 所有依赖已安装")
        return True
    except ImportError as e:
        print(f"❌ 缺少依赖: {e}")
        print("正在安装依赖...")
        req_file = os.path.join(os.path.dirname(__file__), 'requirements.txt')
        subprocess.run([sys.executable, '-m', 'pip', 'install', '-r', req_file], check=True)
        return True


def main():
    print("=" * 50)
    print("  🌙 AI 睡前故事机 - 后端服务器")
    print("=" * 50)

    # 检查依赖
    if not check_dependencies():
        sys.exit(1)

    # 检查环境变量
    env_ok = check_env()
    if not env_ok:
        print("\n⚠️  环境变量未配置完整，服务器将以演示模式启动")
        print("   （使用备用故事，不需要 API Key）\n")

    # 启动服务器
    import uvicorn
    from dotenv import load_dotenv
    load_dotenv()

    host = os.getenv('SERVER_HOST', '0.0.0.0')
    port = int(os.getenv('SERVER_PORT', '8000'))

    print(f"\n🚀 启动服务器...")
    print(f"   本地访问: http://localhost:{port}")
    print(f"   Web 界面: http://localhost:{port}/web")
    print(f"   API 文档: http://localhost:{port}/docs")
    print(f"   WebSocket: ws://localhost:{port}/ws/{{device_id}}")
    print(f"\n   按 Ctrl+C 停止服务器\n")

    uvicorn.run(
        "app:app",
        host=host,
        port=port,
        reload=False,
        log_level="info"
    )


if __name__ == '__main__':
    # 切换到脚本所在目录
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    main()