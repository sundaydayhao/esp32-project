/**
 * AI 睡前故事机 - Web 管理界面
 * 与后端 FastAPI 服务器通信
 */

// ==================== 配置 ====================
const API_BASE = window.location.origin;
const WS_BASE = API_BASE.replace(/^http/, 'ws');

// ==================== 状态 ====================
let keywords = [];
let currentStory = '';
let isGenerating = false;
let eventSource = null;
let audioChunks = [];
let connectedDevices = new Map();

// ==================== 初始化 ====================
document.addEventListener('DOMContentLoaded', () => {
    checkServerStatus();
    loadThemes();
    setupKeywordInput();
    setInterval(checkServerStatus, 10000);
    setInterval(updateDeviceList, 5000);

    document.getElementById('serverAddr').textContent = API_BASE;
});

// ==================== 服务器状态 ====================
async function checkServerStatus() {
    try {
        const res = await fetch(`${API_BASE}/health`, { signal: AbortSignal.timeout(3000) });
        const data = await res.json();

        document.getElementById('serverStatus').className = 'status-dot connected';
        document.getElementById('serverStatusText').textContent = '已连接';
        document.getElementById('serverState').textContent = data.status;
        document.getElementById('connectionCount').textContent = data.connections;
    } catch (e) {
        document.getElementById('serverStatus').className = 'status-dot error';
        document.getElementById('serverStatusText').textContent = '未连接';
        document.getElementById('serverState').textContent = '离线';
    }
}

// ==================== 关键词管理 ====================
function setupKeywordInput() {
    const input = document.getElementById('keywordInput');
    input.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') addKeyword();
    });
}

function addKeyword() {
    const input = document.getElementById('keywordInput');
    const value = input.value.trim();
    if (!value) return;
    if (keywords.includes(value)) {
        input.value = '';
        return;
    }
    if (keywords.length >= 5) {
        setStatus('最多添加 5 个关键词');
        return;
    }
    keywords.push(value);
    input.value = '';
    renderKeywordTags();
}

function quickKeyword(kw) {
    if (!keywords.includes(kw) && keywords.length < 5) {
        keywords.push(kw);
        renderKeywordTags();
    }
}

function removeKeyword(kw) {
    keywords = keywords.filter(k => k !== kw);
    renderKeywordTags();
}

function renderKeywordTags() {
    const container = document.getElementById('keywordTags');
    container.innerHTML = keywords.map(kw => `
        <span class="keyword-tag" onclick="removeKeyword('${kw}')">
            ${kw}
            <span class="remove">×</span>
        </span>
    `).join('');
}

function clearAll() {
    keywords = [];
    currentStory = '';
    renderKeywordTags();
    document.getElementById('storyText').innerHTML = '<p class="story-placeholder">在这里，AI 将为孩子创作专属的睡前故事...</p>';
    document.getElementById('storyIllustration').style.display = 'none';
    document.getElementById('illustrationContainer').querySelector('.illustration-placeholder').style.display = 'flex';
    document.getElementById('audioPlayer').style.display = 'none';
    document.getElementById('progressContainer').style.display = 'none';
    document.getElementById('storyText').parentElement.parentElement.classList.remove('story-generating');
    setStatus('已清除');
}

// ==================== 故事生成 ====================
async function generateStory() {
    if (keywords.length === 0) {
        setStatus('请先添加故事关键词');
        return;
    }
    if (isGenerating) return;

    isGenerating = true;
    currentStory = '';
    audioChunks = [];

    document.getElementById('generateBtn').disabled = true;
    document.getElementById('stopBtn').disabled = false;
    document.getElementById('progressContainer').style.display = 'flex';
    document.getElementById('progressFill').style.width = '10%';
    document.getElementById('progressText').textContent = '正在生成故事...';
    document.getElementById('storyText').innerHTML = '';
    document.getElementById('storyText').parentElement.parentElement.classList.add('story-generating');
    document.getElementById('audioPlayer').style.display = 'none';

    setStatus(`正在为关键词「${keywords.join('、')}」生成故事...`);

    // 先获取插图
    const theme = await getThemeForKeywords(keywords);
    showIllustration(theme);

    // 流式生成故事
    try {
        const kwParam = encodeURIComponent(keywords.join(','));
        eventSource = new EventSource(`${API_BASE}/api/story/stream?keywords=${kwParam}`);

        let charCount = 0;
        eventSource.onmessage = (e) => {
            if (e.data === '[DONE]') {
                finishGeneration();
                return;
            }
            try {
                const chunk = JSON.parse(e.data);
                if (chunk.text) {
                    currentStory += chunk.text;
                    charCount += chunk.text.length;
                    appendStoryText(chunk.text);

                    // 更新进度
                    const progress = Math.min(10 + (charCount / 5), 90);
                    document.getElementById('progressFill').style.width = `${progress}%`;
                }
            } catch (err) { /* ignore */ }
        };

        eventSource.onerror = () => {
            if (isGenerating) {
                finishGeneration();
            }
        };
    } catch (e) {
        setStatus(`生成失败: ${e.message}`);
        resetGenerating();
    }
}

function appendStoryText(text) {
    const container = document.getElementById('storyText');
    const span = document.createElement('span');
    span.className = 'story-chunk';
    span.textContent = text;
    container.appendChild(span);

    // 自动滚动到底部
    const scrollContainer = container.parentElement;
    scrollContainer.scrollTop = scrollContainer.scrollHeight;
}

async function finishGeneration() {
    if (eventSource) {
        eventSource.close();
        eventSource = null;
    }

    document.getElementById('progressFill').style.width = '100%';
    document.getElementById('progressText').textContent = `故事生成完成，共 ${currentStory.length} 字`;
    document.getElementById('storyText').parentElement.parentElement.classList.remove('story-generating');

    setStatus(`故事生成完成！共 ${currentStory.length} 字`);

    // 自动生成 TTS
    if (currentStory.length > 0) {
        await generateTTS();
    }

    resetGenerating();
}

function stopStory() {
    if (eventSource) {
        eventSource.close();
        eventSource = null;
    }
    resetGenerating();
    setStatus('已停止生成');
}

function resetGenerating() {
    isGenerating = false;
    document.getElementById('generateBtn').disabled = false;
    document.getElementById('stopBtn').disabled = true;
}

// ==================== TTS 语音合成 ====================
async function generateTTS() {
    if (!currentStory) return;

    setStatus('正在合成语音...');
    document.getElementById('progressText').textContent = '正在合成语音...';

    try {
        const response = await fetch(`${API_BASE}/api/tts`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ text: currentStory })
        });

        if (!response.ok) throw new Error('TTS 合成失败');

        const audioBlob = await response.blob();
        const audioUrl = URL.createObjectURL(audioBlob);

        const audioEl = document.getElementById('audioElement');
        audioEl.src = audioUrl;
        document.getElementById('audioPlayer').style.display = 'block';
        document.getElementById('progressText').textContent = '语音合成完成，点击播放';

        setStatus('语音合成完成！');
    } catch (e) {
        setStatus(`语音合成失败: ${e.message}`);
    }
}

async function playTTS() {
    if (!currentStory) {
        setStatus('请先生成故事');
        return;
    }
    await generateTTS();
    document.getElementById('audioElement').play();
}

// ==================== 插图 ====================
async function getThemeForKeywords(kws) {
    try {
        const res = await fetch(`${API_BASE}/api/keywords`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ text: kws.join(' ') })
        });
        const data = await res.json();
        return data.theme || 'moonlit_forest';
    } catch (e) {
        return 'moonlit_forest';
    }
}

function showIllustration(theme) {
    const img = document.getElementById('storyIllustration');
    const placeholder = document.getElementById('illustrationContainer').querySelector('.illustration-placeholder');

    img.src = `${API_BASE}/api/image/${theme}`;
    img.onload = () => {
        img.style.display = 'block';
        placeholder.style.display = 'none';
    };
    img.onerror = () => {
        img.style.display = 'none';
        placeholder.style.display = 'flex';
    };
}

// ==================== 主题预览 ====================
async function loadThemes() {
    try {
        const res = await fetch(`${API_BASE}/api/themes`);
        const data = await res.json();
        renderThemeGrid(data.themes.slice(0, 9));
    } catch (e) {
        // 使用默认主题列表
        renderThemeGrid(['dinosaur', 'space', 'princess', 'forest', 'ocean', 'moon', 'unicorn', 'robot', 'castle']);
    }
}

function renderThemeGrid(themes) {
    const grid = document.getElementById('themeGrid');
    grid.innerHTML = themes.map(theme => `
        <div class="theme-item" onclick="previewTheme('${theme}')" title="${theme}">
            <img src="${API_BASE}/api/image/${theme}" alt="${theme}" loading="lazy"
                 onerror="this.style.display='none'">
            <span class="theme-label">${theme}</span>
        </div>
    `).join('');
}

function previewTheme(theme) {
    showIllustration(theme);
    setStatus(`预览主题: ${theme}`);
}

// ==================== 设备管理 ====================
async function updateDeviceList() {
    try {
        const res = await fetch(`${API_BASE}/health`);
        const data = await res.json();
        document.getElementById('connectionCount').textContent = data.connections;
    } catch (e) { /* ignore */ }
}

// ==================== 工具函数 ====================
function copyStory() {
    if (!currentStory) {
        setStatus('没有可复制的故事');
        return;
    }
    navigator.clipboard.writeText(currentStory).then(() => {
        setStatus('故事已复制到剪贴板');
    }).catch(() => {
        // 降级方案
        const ta = document.createElement('textarea');
        ta.value = currentStory;
        document.body.appendChild(ta);
        ta.select();
        document.execCommand('copy');
        document.body.removeChild(ta);
        setStatus('故事已复制');
    });
}

function downloadStory() {
    if (!currentStory) {
        setStatus('没有可下载的故事');
        return;
    }
    const kw = keywords.join('_') || '故事';
    const filename = `睡前故事_${kw}_${new Date().toLocaleDateString('zh-CN').replace(/\//g, '')}.txt`;
    const blob = new Blob([currentStory], { type: 'text/plain;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.click();
    URL.revokeObjectURL(url);
    setStatus(`故事已下载: ${filename}`);
}

function setStatus(msg) {
    document.getElementById('statusMessage').textContent = msg;
}