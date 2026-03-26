// ESP32-S3 频谱仪 - WebSocket 客户端
'use strict';

// ---------------------------------------------------------------------------
// 配置
// ---------------------------------------------------------------------------
const DB_OFFSET    = 1200;   // 与固件一致
const DB_SCALE     = 10;
const WATERFALL_ROWS = 120;  // 瀑布图行数

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------
let ws          = null;
let isRunning   = false;
let fftSize     = 512;
let centerFreq  = 100e6;   // Hz
let bandwidth   = 1e6;     // Hz
let refLevel    = 0;       // dB
let dbRange     = 80;      // dB
let lastFpsTime = performance.now();
let frameCount  = 0;

// 瀑布图数据（循环缓冲区）
let waterfallData = [];   // 每行是 Float32Array

// ---------------------------------------------------------------------------
// Canvas 引用
// ---------------------------------------------------------------------------
let specCanvas, specCtx;
let wfCanvas,   wfCtx;
let wfImageData = null;

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------
window.addEventListener('DOMContentLoaded', () => {
    specCanvas = document.getElementById('spectrum-canvas');
    wfCanvas   = document.getElementById('waterfall-canvas');
    specCtx    = specCanvas.getContext('2d');
    wfCtx      = wfCanvas.getContext('2d');

    // 鼠标移动显示光标频率
    specCanvas.addEventListener('mousemove', onSpecMouseMove);
    wfCanvas.addEventListener('mousemove', onWfMouseMove);

    // 控件事件
    document.getElementById('ref-level').addEventListener('change', () => {
        refLevel = parseFloat(document.getElementById('ref-level').value);
    });
    document.getElementById('db-range').addEventListener('change', () => {
        dbRange = parseFloat(document.getElementById('db-range').value);
    });

    // 自适应 canvas 尺寸
    resizeCanvases();
    window.addEventListener('resize', resizeCanvases);

    // 查询当前状态
    fetchStatus();
});

function resizeCanvases() {
    specCanvas.width  = specCanvas.offsetWidth;
    specCanvas.height = specCanvas.offsetHeight;
    wfCanvas.width    = wfCanvas.offsetWidth;
    wfCanvas.height   = wfCanvas.offsetHeight;
    wfImageData = wfCtx.createImageData(wfCanvas.width, wfCanvas.height);
    waterfallData = [];  // 重置瀑布图
}

// ---------------------------------------------------------------------------
// 状态查询
// ---------------------------------------------------------------------------
async function fetchStatus() {
    try {
        const res  = await fetch('/spectrum/status');
        const data = await res.json();
        centerFreq = data.freq;
        bandwidth  = data.bw;
        fftSize    = data.fft_size;
        document.getElementById('freq-input').value = (centerFreq / 1e6).toFixed(3);
        document.getElementById('bw-select').value  = String(bandwidth);
        updateFreqBar();
        if (data.active) {
            setStatus('已在运行，点击启动连接', '');
        } else {
            setStatus('就绪', '');
        }
    } catch(e) {
        setStatus('无法连接到 ESP32', '');
    }
}

// ---------------------------------------------------------------------------
// 启动 / 停止
// ---------------------------------------------------------------------------
async function startSpectrum() {
    if (isRunning) return;

    const freqMHz = parseFloat(document.getElementById('freq-input').value);
    const bw      = parseInt(document.getElementById('bw-select').value);
    centerFreq    = freqMHz * 1e6;
    bandwidth     = bw;
    refLevel      = parseFloat(document.getElementById('ref-level').value);
    dbRange       = parseFloat(document.getElementById('db-range').value);

    document.getElementById('btn-start').disabled = true;
    setStatus('正在启动...', 'loading');

    try {
        const res = await fetch('/spectrum/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ freq: centerFreq, bw: bandwidth })
        });
        const data = await res.json();
        if (!data.ok) throw new Error('ESP32 returned ok=false');
    } catch(e) {
        setStatus('启动失败: ' + e.message, '');
        document.getElementById('btn-start').disabled = false;
        return;
    }

    updateFreqBar();
    connectWebSocket();
}

async function stopSpectrum() {
    isRunning = false;
    if (ws) { ws.close(); ws = null; }

    try { await fetch('/spectrum/stop', { method: 'POST' }); } catch(e) {}

    document.getElementById('btn-start').disabled = false;
    document.getElementById('btn-stop').disabled  = true;
    setStatus('已停止', '');
}

// ---------------------------------------------------------------------------
// 频率 / 带宽切换（运行中）
// ---------------------------------------------------------------------------
async function applyFreq() {
    const freqMHz = parseFloat(document.getElementById('freq-input').value);
    centerFreq = freqMHz * 1e6;
    updateFreqBar();
    if (!isRunning) return;
    try {
        await fetch('/spectrum/freq', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ freq: centerFreq })
        });
    } catch(e) {}
}

async function applyBw() {
    bandwidth = parseInt(document.getElementById('bw-select').value);
    updateFreqBar();
    if (!isRunning) return;
    try {
        await fetch('/spectrum/bw', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ bw: bandwidth })
        });
    } catch(e) {}
}

// 绑定控件事件（运行中实时切换）
document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('freq-input').addEventListener('change', applyFreq);
    document.getElementById('bw-select').addEventListener('change', applyBw);
});

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------
function connectWebSocket() {
    const host = window.location.host;
    ws = new WebSocket('ws://' + host + '/spectrum/ws');
    ws.binaryType = 'arraybuffer';

    ws.onopen = () => {
        isRunning = true;
        document.getElementById('btn-start').disabled = true;
        document.getElementById('btn-stop').disabled  = false;
        setStatus('接收中 ' + (centerFreq / 1e6).toFixed(3) + ' MHz', 'active');
    };

    ws.onmessage = (evt) => {
        if (evt.data instanceof ArrayBuffer) {
            processFrame(evt.data);
        }
    };

    ws.onerror = (e) => {
        console.error('WS error', e);
    };

    ws.onclose = () => {
        if (isRunning) {
            setStatus('连接断开，重连中...', 'loading');
            setTimeout(connectWebSocket, 1500);
        }
    };
}

// ---------------------------------------------------------------------------
// 解析二进制帧
// Header (12 bytes): freq(4) bw(4) fft_size(2) ver(1) rsv(1)
// Payload: fft_size * int16 (big-endian, offset=1200, scale=10)
// ---------------------------------------------------------------------------
function processFrame(buf) {
    const view = new DataView(buf);
    const freq = view.getUint32(0);
    const bw   = view.getUint32(4);
    const n    = view.getUint16(8);

    // 同步参数（服务端可能已切换）
    if (freq !== centerFreq || bw !== bandwidth) {
        centerFreq = freq;
        bandwidth  = bw;
        fftSize    = n;
        document.getElementById('freq-input').value = (centerFreq / 1e6).toFixed(3);
        document.getElementById('bw-select').value  = String(bandwidth);
        updateFreqBar();
    }

    const dbArr = new Float32Array(n);
    for (let i = 0; i < n; i++) {
        const raw = view.getInt16(12 + i * 2);
        dbArr[i] = (raw - DB_OFFSET) / DB_SCALE;
    }

    drawSpectrum(dbArr);
    addWaterfallRow(dbArr);
    drawWaterfall();
    updateFps();
}

// ---------------------------------------------------------------------------
// 频谱图绘制
// ---------------------------------------------------------------------------
function drawSpectrum(dbArr) {
    const W = specCanvas.width;
    const H = specCanvas.height;
    const n = dbArr.length;

    specCtx.fillStyle = '#0d1117';
    specCtx.fillRect(0, 0, W, H);

    // 网格
    drawGrid(specCtx, W, H);

    // 频谱曲线
    specCtx.beginPath();
    specCtx.strokeStyle = '#58a6ff';
    specCtx.lineWidth   = 1.5;

    for (let i = 0; i < n; i++) {
        const x = (i / (n - 1)) * W;
        const y = dbToY(dbArr[i], H);
        if (i === 0) specCtx.moveTo(x, y);
        else         specCtx.lineTo(x, y);
    }
    specCtx.stroke();

    // 填充
    specCtx.lineTo(W, H);
    specCtx.lineTo(0, H);
    specCtx.closePath();
    specCtx.fillStyle = 'rgba(88,166,255,0.08)';
    specCtx.fill();
}

function drawGrid(ctx, W, H) {
    ctx.strokeStyle = '#21262d';
    ctx.lineWidth   = 1;
    ctx.fillStyle   = '#8b949e';
    ctx.font        = '10px monospace';

    // 水平网格（dB）
    const topDb    = refLevel;
    const bottomDb = refLevel - dbRange;
    const dbStep   = dbRange <= 40 ? 5 : dbRange <= 80 ? 10 : 20;

    for (let db = Math.ceil(bottomDb / dbStep) * dbStep; db <= topDb; db += dbStep) {
        const y = dbToY(db, H);
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(W, y);
        ctx.stroke();
        ctx.fillText(db + ' dB', 4, y - 2);
    }

    // 垂直网格（频率）
    const freqStep = bandwidth / 4;
    for (let i = 0; i <= 4; i++) {
        const x = (i / 4) * W;
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, H);
        ctx.stroke();
        const f = (centerFreq - bandwidth / 2 + i * freqStep) / 1e6;
        ctx.fillText(f.toFixed(2), x + 3, H - 4);
    }
}

function dbToY(db, H) {
    const top    = refLevel;
    const bottom = refLevel - dbRange;
    return H * (1 - (db - bottom) / (top - bottom));
}

// ---------------------------------------------------------------------------
// 瀑布图
// ---------------------------------------------------------------------------
function addWaterfallRow(dbArr) {
    waterfallData.unshift(dbArr.slice());
    if (waterfallData.length > WATERFALL_ROWS) waterfallData.pop();
}

function drawWaterfall() {
    const W = wfCanvas.width;
    const H = wfCanvas.height;
    if (!wfImageData || wfImageData.width !== W || wfImageData.height !== H) {
        wfImageData = wfCtx.createImageData(W, H);
    }

    const rows = Math.min(waterfallData.length, H);
    const n    = fftSize;
    const data = wfImageData.data;

    for (let row = 0; row < rows; row++) {
        const dbRow = waterfallData[row];
        for (let x = 0; x < W; x++) {
            const binIdx = Math.floor((x / W) * n);
            const db     = dbRow[binIdx] || -120;
            const norm   = Math.max(0, Math.min(1, (db - (refLevel - dbRange)) / dbRange));
            const [r, g, b] = dbToColor(norm);
            const idx = (row * W + x) * 4;
            data[idx]     = r;
            data[idx + 1] = g;
            data[idx + 2] = b;
            data[idx + 3] = 255;
        }
    }

    wfCtx.putImageData(wfImageData, 0, 0);
}

// 热力图颜色映射（蓝→青→绿→黄→红）
function dbToColor(norm) {
    const stops = [
        [0,   [  0,   0,  80]],
        [0.2, [  0,  50, 200]],
        [0.4, [  0, 200, 200]],
        [0.6, [  0, 220,   0]],
        [0.8, [220, 220,   0]],
        [1.0, [255,   0,   0]],
    ];
    for (let i = 1; i < stops.length; i++) {
        if (norm <= stops[i][0]) {
            const t = (norm - stops[i-1][0]) / (stops[i][0] - stops[i-1][0]);
            const a = stops[i-1][1], b = stops[i][1];
            return [
                Math.round(a[0] + t * (b[0] - a[0])),
                Math.round(a[1] + t * (b[1] - a[1])),
                Math.round(a[2] + t * (b[2] - a[2])),
            ];
        }
    }
    return [255, 0, 0];
}

// ---------------------------------------------------------------------------
// 光标信息
// ---------------------------------------------------------------------------
function freqAtX(canvas, x) {
    return centerFreq - bandwidth / 2 + (x / canvas.width) * bandwidth;
}

function onSpecMouseMove(e) {
    const rect = specCanvas.getBoundingClientRect();
    const x    = e.clientX - rect.left;
    const y    = e.clientY - rect.top;
    const freq = freqAtX(specCanvas, x);
    const db   = refLevel - dbRange * (y / specCanvas.height);
    document.getElementById('cursor-freq').textContent = (freq / 1e6).toFixed(4) + ' MHz';
    document.getElementById('cursor-db').textContent   = db.toFixed(1) + ' dBFS';
}

function onWfMouseMove(e) {
    const rect = wfCanvas.getBoundingClientRect();
    const x    = e.clientX - rect.left;
    const freq = freqAtX(wfCanvas, x);
    document.getElementById('cursor-freq').textContent = (freq / 1e6).toFixed(4) + ' MHz';
}

// ---------------------------------------------------------------------------
// 频率轴标签
// ---------------------------------------------------------------------------
function updateFreqBar() {
    const lo = (centerFreq - bandwidth / 2) / 1e6;
    const hi = (centerFreq + bandwidth / 2) / 1e6;
    document.getElementById('freq-left').textContent   = lo.toFixed(3) + ' MHz';
    document.getElementById('freq-center').textContent = (centerFreq / 1e6).toFixed(3) + ' MHz';
    document.getElementById('freq-right').textContent  = hi.toFixed(3) + ' MHz';
}

// ---------------------------------------------------------------------------
// FPS 计数
// ---------------------------------------------------------------------------
function updateFps() {
    frameCount++;
    const now = performance.now();
    if (now - lastFpsTime >= 1000) {
        document.getElementById('fps-counter').textContent = frameCount + ' fps';
        frameCount  = 0;
        lastFpsTime = now;
    }
}

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------
function setStatus(text, state) {
    document.getElementById('status-text').textContent = text;
    const dot = document.getElementById('status-dot');
    dot.className = 'dot' + (state ? ' ' + state : '');
}

window.addEventListener('beforeunload', () => {
    if (isRunning) fetch('/spectrum/stop', { method: 'POST', keepalive: true });
});