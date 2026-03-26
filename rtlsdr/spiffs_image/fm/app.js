// ESP32-S3 FM 收音机 - Web Audio API 播放器
'use strict';

let audioCtx    = null;
let gainNode    = null;
let reader      = null;
let isPlaying   = false;
let nextPlayTime = 0;
let currentFreqKHz = 100000;

const SAMPLE_RATE   = 44100;
const CHUNK_SAMPLES = 1024;

// ---------------------------------------------------------------------------
// 频率控制
// ---------------------------------------------------------------------------

function onSliderInput(valKHz) {
    currentFreqKHz = parseInt(valKHz);
    updateFreqDisplay(currentFreqKHz);
    updatePresetHighlight(currentFreqKHz);
    if (isPlaying) applyFreq(currentFreqKHz);
}

function setFreq(kHz) {
    currentFreqKHz = kHz;
    document.getElementById('freq-slider').value = kHz;
    updateFreqDisplay(kHz);
    updatePresetHighlight(kHz);
    if (isPlaying) applyFreq(kHz);
}

function updateFreqDisplay(kHz) {
    document.getElementById('freq-display').textContent = (kHz / 1000).toFixed(1);
}

function updatePresetHighlight(kHz) {
    document.querySelectorAll('.preset-btn').forEach(btn => {
        btn.classList.remove('active');
    });
    const presetFreqs = [87900,88700,90000,91500,93000,95000,97400,100000,103800,106600];
    const idx = presetFreqs.indexOf(kHz);
    if (idx >= 0) {
        document.querySelectorAll('.preset-btn')[idx].classList.add('active');
    }
}

async function applyFreq(kHz) {
    try {
        await fetch('/fm/freq', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ freq: kHz * 1000 })
        });
    } catch (e) {
        console.warn('applyFreq error:', e);
    }
}

// ---------------------------------------------------------------------------
// 音量控制
// ---------------------------------------------------------------------------

function setVolume(val) {
    const pct = Math.round(parseFloat(val) * 100);
    document.getElementById('volume-label').textContent = pct + '%';
    if (gainNode) gainNode.gain.value = parseFloat(val);
}

// ---------------------------------------------------------------------------
// 状态更新
// ---------------------------------------------------------------------------

function setStatus(text, state) {
    document.getElementById('status-text').textContent = text;
    const dot = document.getElementById('status-dot');
    dot.className = 'dot' + (state ? ' ' + state : '');
}

// ---------------------------------------------------------------------------
// 启动 FM
// ---------------------------------------------------------------------------

async function startFM() {
    if (isPlaying) return;

    document.getElementById('btn-start').disabled = true;
    setStatus('正在启动 FM...', 'loading');

    try {
        // 通知 ESP32 启动 FM
        const res = await fetch('/fm/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ freq: currentFreqKHz * 1000 })
        });
        const data = await res.json();
        if (!data.ok) throw new Error('ESP32 returned ok=false');

        // 创建 AudioContext
        audioCtx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: SAMPLE_RATE });
        gainNode = audioCtx.createGain();
        gainNode.gain.value = parseFloat(document.getElementById('volume-slider').value);
        gainNode.connect(audioCtx.destination);

        nextPlayTime = audioCtx.currentTime + 0.3;
        isPlaying    = true;

        document.getElementById('btn-start').disabled = true;
        document.getElementById('btn-stop').disabled  = false;
        setStatus('正在收听 ' + (currentFreqKHz / 1000).toFixed(1) + ' MHz', 'active');

        // 显示可视化
        const canvas = document.getElementById('visualizer');
        canvas.style.display = 'block';
        startVisualizer();

        // 开始拉取音频流
        streamAudio();

    } catch (e) {
        console.error('startFM error:', e);
        setStatus('启动失败: ' + e.message, '');
        document.getElementById('btn-start').disabled = false;
        isPlaying = false;
    }
}

// ---------------------------------------------------------------------------
// 停止 FM
// ---------------------------------------------------------------------------

async function stopFM() {
    isPlaying = false;

    if (reader) {
        try { reader.cancel(); } catch(e) {}
        reader = null;
    }
    if (audioCtx) {
        try { await audioCtx.close(); } catch(e) {}
        audioCtx = null;
        gainNode = null;
    }

    document.getElementById('visualizer').style.display = 'none';

    try {
        await fetch('/fm/stop', { method: 'POST' });
    } catch(e) {}

    document.getElementById('btn-start').disabled = false;
    document.getElementById('btn-stop').disabled  = true;
    setStatus('已停止', '');
}

// ---------------------------------------------------------------------------
// 音频流处理（HTTP chunked PCM → Web Audio API）
// ---------------------------------------------------------------------------

async function streamAudio() {
    try {
        const response = await fetch('/fm/audio');
        if (!response.ok) throw new Error('HTTP ' + response.status);

        reader = response.body.getReader();
        let leftover = new Uint8Array(0);

        while (isPlaying) {
            const { done, value } = await reader.read();
            if (done) break;

            // 合并上次剩余字节
            const combined = new Uint8Array(leftover.length + value.length);
            combined.set(leftover);
            combined.set(value, leftover.length);

            const bytesPerChunk = CHUNK_SAMPLES * 2;
            let offset = 0;

            while (offset + bytesPerChunk <= combined.length) {
                const chunk = combined.subarray(offset, offset + bytesPerChunk);
                offset += bytesPerChunk;
                scheduleChunk(chunk);
            }

            leftover = combined.subarray(offset);
        }
    } catch (e) {
        if (isPlaying) {
            console.error('Stream error:', e);
            setStatus('流中断，重连中...', 'loading');
            setTimeout(() => { if (isPlaying) streamAudio(); }, 1000);
        }
    }
}

function scheduleChunk(pcmBytes) {
    if (!audioCtx || !isPlaying) return;

    const buf = audioCtx.createBuffer(1, CHUNK_SAMPLES, SAMPLE_RATE);
    const ch  = buf.getChannelData(0);

    for (let i = 0; i < CHUNK_SAMPLES; i++) {
        // int16 LE → float [-1, 1]
        let s = pcmBytes[i * 2] | (pcmBytes[i * 2 + 1] << 8);
        if (s >= 0x8000) s -= 0x10000;
        ch[i] = s / 32768.0;
    }

    const src = audioCtx.createBufferSource();
    src.buffer = buf;
    src.connect(gainNode);

    const now = audioCtx.currentTime;
    if (nextPlayTime < now) nextPlayTime = now + 0.05;
    src.start(nextPlayTime);
    nextPlayTime += buf.duration;
}

// ---------------------------------------------------------------------------
// 简单音频可视化（波形）
// ---------------------------------------------------------------------------

let analyser = null;
let vizAnimId = null;

function startVisualizer() {
    if (!audioCtx) return;
    analyser = audioCtx.createAnalyser();
    analyser.fftSize = 256;
    gainNode.connect(analyser);

    const canvas = document.getElementById('visualizer');
    const ctx    = canvas.getContext('2d');
    canvas.width  = canvas.offsetWidth;
    canvas.height = canvas.offsetHeight;

    const bufLen = analyser.frequencyBinCount;
    const dataArr = new Uint8Array(bufLen);

    function draw() {
        vizAnimId = requestAnimationFrame(draw);
        analyser.getByteFrequencyData(dataArr);

        ctx.fillStyle = '#0d1117';
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        const barW = (canvas.width / bufLen) * 2.5;
        let x = 0;
        for (let i = 0; i < bufLen; i++) {
            const barH = (dataArr[i] / 255) * canvas.height;
            const hue  = 200 + (dataArr[i] / 255) * 60;
            ctx.fillStyle = `hsl(${hue},80%,55%)`;
            ctx.fillRect(x, canvas.height - barH, barW, barH);
            x += barW + 1;
        }
    }
    draw();
}

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------

window.addEventListener('DOMContentLoaded', async () => {
    updateFreqDisplay(currentFreqKHz);
    updatePresetHighlight(currentFreqKHz);

    // 查询当前状态
    try {
        const res  = await fetch('/fm/status');
        const data = await res.json();
        if (data.active) {
            const kHz = Math.round(data.freq / 1000);
            setFreq(kHz);
            setStatus('FM 已在运行 ' + (kHz / 1000).toFixed(1) + ' MHz（点击开始收听）', '');
        } else {
            setStatus('就绪，点击"开始收听"', '');
        }
    } catch(e) {
        setStatus('无法连接到 ESP32', '');
    }
});

window.addEventListener('beforeunload', () => {
    if (isPlaying) {
        fetch('/fm/stop', { method: 'POST', keepalive: true });
    }
});