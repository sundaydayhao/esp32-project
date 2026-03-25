/**
 * ESP32-S3 Oscilloscope Web Frontend
 * Side-by-side dual-channel layout.
 * CH1 (left column) and CH2 (right column) each have their own canvas.
 */

'use strict';

/* ============================================================
   Constants
   ============================================================ */
const GRID_COLS = 10;
const GRID_ROWS = 8;
const ADC_MAX = 4095;
const VREF_MV = 3300;
const WS_URL = `ws://${location.hostname}/ws`;

/* ============================================================
   State
   ============================================================ */
const state = {
    ws: null,
    running: true,
    singleShot: false,
    singleCaptured: false,

    ch1: {
        enabled: true,
        vdiv: 1.0,
        offset: 0,
        color: '#00d4ff',
        glowColor: 'rgba(0,212,255,0.45)',
        fillColor: 'rgba(0,212,255,0.07)',
    },
    ch2: {
        enabled: true,
        vdiv: 1.0,
        offset: 0,
        color: '#ff6b35',
        glowColor: 'rgba(255,107,53,0.45)',
        fillColor: 'rgba(255,107,53,0.07)',
    },

    timebase: 10000,
    sampleRate: 1000,

    displayMode: 'line',
    persistence: 0,

    frameCount: 0,
    fps: 0,
    fpsTimer: 0,

    cursor: {
        ch1: { x: 0, y: 0, show: false },
        ch2: { x: 0, y: 0, show: false },
    },

    ch1Data: null,
    ch2Data: null,
    actualSampleRate: 0,
    bufferSize: 0,
    timestamp: 0,
};

/* ============================================================
   Canvas references
   ============================================================ */
const canvasCh1 = document.getElementById('canvas-ch1');
const canvasCh2 = document.getElementById('canvas-ch2');
const ctxCh1 = canvasCh1.getContext('2d');
const ctxCh2 = canvasCh2.getContext('2d');

function resizeCanvas(canvas) {
    const wrap = canvas.parentElement;
    const rect = wrap.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.floor(rect.width * dpr);
    canvas.height = Math.floor(rect.height * dpr);
    canvas.style.width = rect.width + 'px';
    canvas.style.height = rect.height + 'px';
    canvas.getContext('2d').scale(dpr, dpr);
}

function resizeAll() {
    resizeCanvas(canvasCh1);
    resizeCanvas(canvasCh2);
}

/* ============================================================
   WebSocket
   ============================================================ */
function connectWS() {
    setStatus('connecting', '正在连接...');
    const ws = new WebSocket(WS_URL);
    ws.binaryType = 'arraybuffer';
    state.ws = ws;

    ws.onopen = () => {
        setStatus('connected', '已连接');
        setMsg('已连接到 ESP32-S3 示波器');
        sendConfig();
    };

    ws.onclose = () => {
        setStatus('error', '已断开');
        setMsg('连接断开，5 秒后重连...');
        setTimeout(connectWS, 5000);
    };

    ws.onerror = () => {
        setStatus('error', '连接错误');
    };

    ws.onmessage = (e) => {
        if (e.data instanceof ArrayBuffer) {
            parseFrame(e.data);
        }
    };
}

/**
 * Binary frame:
 *   [0-1]  magic 0xAD 0xC1
 *   [2-3]  sample_count uint16 LE
 *   [4-7]  sample_rate  uint32 LE
 *   [8-11] timestamp_ms uint32 LE
 *   [12+]  interleaved uint16: ch1[0],ch2[0],ch1[1],ch2[1],...
 */
function parseFrame(buf) {
    const view = new DataView(buf);
    if (view.getUint8(0) !== 0xAD || view.getUint8(1) !== 0xC1) {
        return;
    }

    const sampleCount = view.getUint16(2, true);
    const rate = view.getUint32(4, true);
    const timestampMs = view.getUint32(8, true);

    const ch1 = new Float32Array(sampleCount);
    const ch2 = new Float32Array(sampleCount);
    for (let i = 0; i < sampleCount; i++) {
        ch1[i] = (view.getUint16(12 + i * 4, true) / ADC_MAX) * VREF_MV;
        ch2[i] = (view.getUint16(12 + i * 4 + 2, true) / ADC_MAX) * VREF_MV;
    }

    state.ch1Data = ch1;
    state.ch2Data = ch2;
    state.actualSampleRate = rate;
    state.bufferSize = sampleCount;
    state.timestamp = timestampMs;

    state.frameCount++;
    const now = performance.now();
    if (now - state.fpsTimer >= 1000) {
        state.fps = state.frameCount;
        state.frameCount = 0;
        state.fpsTimer = now;
        set('frameRate', `${state.fps} fps`);
    }

    set('sampleRate', fmtHz(rate));
    set('bufferSize', `${sampleCount} pts`);
    set('timestampDisplay', `T=${(timestampMs / 1000).toFixed(3)}s`);

    if (!state.running) {
        return;
    }

    if (state.singleShot && state.singleCaptured) {
        return;
    }

    renderCh(canvasCh1, ctxCh1, ch1, rate, state.ch1, state.cursor.ch1);
    renderCh(canvasCh2, ctxCh2, ch2, rate, state.ch2, state.cursor.ch2);
    calcMeas(ch1, rate, 'ch1');
    calcMeas(ch2, rate, 'ch2');

    if (state.singleShot) {
        state.singleCaptured = true;
        setMsg('单次采样已捕获');
    }
}

/* ============================================================
   Render one channel
   ============================================================ */
function renderCh(canvas, ctx, data, rate, ch, cursor) {
    const dpr = window.devicePixelRatio || 1;
    const width = canvas.width / dpr;
    const height = canvas.height / dpr;

    if (state.persistence > 0) {
        ctx.fillStyle = `rgba(5,10,14,${1 - state.persistence * 0.08})`;
    } else {
        ctx.fillStyle = '#050a0e';
    }
    ctx.fillRect(0, 0, width, height);

    drawGrid(ctx, width, height, ch);
    if (ch.enabled && data && data.length > 1) {
        drawWave(ctx, data, rate, ch, width, height);
    }
    if (cursor.show) {
        drawCursor(ctx, width, height, cursor, ch);
    }
}

/* ============================================================
   Grid
   ============================================================ */
function drawGrid(ctx, width, height, ch) {
    ctx.save();
    const cellWidth = width / GRID_COLS;
    const cellHeight = height / GRID_ROWS;

    ctx.strokeStyle = 'rgba(255,255,255,0.04)';
    ctx.lineWidth = 0.5;
    ctx.beginPath();
    for (let i = 0; i <= GRID_COLS * 5; i++) {
        const x = i * width / (GRID_COLS * 5);
        ctx.moveTo(x, 0);
        ctx.lineTo(x, height);
    }
    for (let j = 0; j <= GRID_ROWS * 5; j++) {
        const y = j * height / (GRID_ROWS * 5);
        ctx.moveTo(0, y);
        ctx.lineTo(width, y);
    }
    ctx.stroke();

    ctx.strokeStyle = 'rgba(255,255,255,0.10)';
    ctx.lineWidth = 0.8;
    ctx.beginPath();
    for (let i = 0; i <= GRID_COLS; i++) {
        ctx.moveTo(i * cellWidth, 0);
        ctx.lineTo(i * cellWidth, height);
    }
    for (let j = 0; j <= GRID_ROWS; j++) {
        ctx.moveTo(0, j * cellHeight);
        ctx.lineTo(width, j * cellHeight);
    }
    ctx.stroke();

    ctx.strokeStyle = 'rgba(255,255,255,0.18)';
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.moveTo(width / 2, 0);
    ctx.lineTo(width / 2, height);
    ctx.moveTo(0, height / 2);
    ctx.lineTo(width, height / 2);
    ctx.stroke();
    ctx.setLineDash([]);

    ctx.fillStyle = 'rgba(139,148,158,0.5)';
    ctx.font = '9px Consolas, monospace';

    ctx.textAlign = 'center';
    for (let i = 0; i <= GRID_COLS; i++) {
        ctx.fillText(fmtTime((i - GRID_COLS / 2) * state.timebase), i * cellWidth, height - 2);
    }

    ctx.textAlign = 'right';
    for (let j = 0; j <= GRID_ROWS; j++) {
        ctx.fillText(fmtV(((GRID_ROWS / 2) - j) * ch.vdiv * 1000), 30, j * cellHeight + 4);
    }

    ctx.restore();
}

/* ============================================================
   Waveform
   ============================================================ */
function drawWave(ctx, data, rate, ch, width, height) {
    const windowUs = state.timebase * GRID_COLS;
    const sampleCount = Math.min(Math.floor(windowUs / 1e6 * rate), data.length);
    if (sampleCount < 2) {
        return;
    }

    const xStep = width / sampleCount;
    ctx.save();
    ctx.lineWidth = 1.8;
    ctx.strokeStyle = ch.color;
    ctx.shadowColor = ch.glowColor;
    ctx.shadowBlur = 5;

    if (state.displayMode === 'fill') {
        const midY = height / 2 + ch.offset;
        ctx.fillStyle = ch.fillColor;
        ctx.beginPath();
        ctx.moveTo(0, midY);
        for (let i = 0; i < sampleCount; i++) {
            ctx.lineTo(i * xStep, mv2y(data[i], ch, height));
        }
        ctx.lineTo((sampleCount - 1) * xStep, midY);
        ctx.closePath();
        ctx.fill();
    }

    if (state.displayMode === 'dot') {
        ctx.fillStyle = ch.color;
        for (let i = 0; i < sampleCount; i++) {
            ctx.beginPath();
            ctx.arc(i * xStep, mv2y(data[i], ch, height), 1.5, 0, Math.PI * 2);
            ctx.fill();
        }
    } else {
        ctx.beginPath();
        for (let i = 0; i < sampleCount; i++) {
            const x = i * xStep;
            const y = mv2y(data[i], ch, height);
            if (i === 0) {
                ctx.moveTo(x, y);
            } else {
                ctx.lineTo(x, y);
            }
        }
        ctx.stroke();
    }

    ctx.restore();
}

/* ============================================================
   Cursor
   ============================================================ */
function drawCursor(ctx, width, height, cursor, ch) {
    const cx = cursor.x;
    const cy = cursor.y;

    ctx.save();
    ctx.strokeStyle = 'rgba(255,255,255,0.32)';
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    ctx.moveTo(cx, 0);
    ctx.lineTo(cx, height);
    ctx.moveTo(0, cy);
    ctx.lineTo(width, cy);
    ctx.stroke();
    ctx.setLineDash([]);

    const voltageMv = y2mv(cy, ch, height);
    const timeUs = (cx / width) * state.timebase * GRID_COLS - state.timebase * GRID_COLS / 2;
    const label = `t=${fmtTime(timeUs)}  V=${fmtV(voltageMv)}`;

    ctx.font = '11px Consolas, monospace';
    const textWidth = ctx.measureText(label).width + 14;
    const textX = Math.min(cx + 8, width - textWidth - 4);
    const textY = Math.max(cy - 26, 4);

    ctx.fillStyle = 'rgba(13,17,23,0.88)';
    ctx.strokeStyle = 'rgba(255,255,255,0.22)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(textX, textY, textWidth, 18, 3);
    ctx.fill();
    ctx.stroke();

    ctx.fillStyle = '#e6edf3';
    ctx.textAlign = 'left';
    ctx.fillText(label, textX + 7, textY + 13);
    ctx.restore();
}

/* ============================================================
   Coordinate helpers
   ============================================================ */
function mv2y(mv, ch, height) {
    const pixelPerMv = (height / GRID_ROWS) / (ch.vdiv * 1000);
    return height / 2 + ch.offset - (mv - VREF_MV / 2) * pixelPerMv;
}

function y2mv(y, ch, height) {
    const pixelPerMv = (height / GRID_ROWS) / (ch.vdiv * 1000);
    return VREF_MV / 2 - (y - height / 2 - ch.offset) / pixelPerMv;
}

/* ============================================================
   Measurements
   ============================================================ */
function calcMeas(data, rate, prefix) {
    if (!data || !data.length) {
        return;
    }

    let min = Infinity;
    let max = -Infinity;
    let sum = 0;
    let squareSum = 0;

    for (let i = 0; i < data.length; i++) {
        const value = data[i];
        if (value < min) {
            min = value;
        }
        if (value > max) {
            max = value;
        }
        sum += value;
        squareSum += value * value;
    }

    const mean = sum / data.length;
    const rms = Math.sqrt(squareSum / data.length);
    set(`${prefix}Vpp`, fmtV(max - min));
    set(`${prefix}Vmax`, fmtV(max));
    set(`${prefix}Vmin`, fmtV(min));
    set(`${prefix}Vrms`, fmtV(rms));

    let crossings = 0;
    let firstCrossing = -1;
    let lastCrossing = -1;
    for (let i = 1; i < data.length; i++) {
        if (data[i - 1] < mean && data[i] >= mean) {
            crossings++;
            if (firstCrossing < 0) {
                firstCrossing = i;
            }
            lastCrossing = i;
        }
    }

    const freq = crossings >= 2 ? rate / ((lastCrossing - firstCrossing) / (crossings - 1)) : 0;
    set(`${prefix}Freq`, freq > 0 ? fmtHz(freq) : '---');
}

/* ============================================================
   Format helpers
   ============================================================ */
function fmtV(mv) {
    const abs = Math.abs(mv);
    if (abs >= 1000) {
        return `${(mv / 1000).toFixed(3)} V`;
    }
    if (abs >= 1) {
        return `${mv.toFixed(1)} mV`;
    }
    return `${mv.toFixed(2)} mV`;
}

function fmtHz(hz) {
    if (hz >= 1e6) {
        return `${(hz / 1e6).toFixed(3)} MHz`;
    }
    if (hz >= 1e3) {
        return `${(hz / 1e3).toFixed(2)} kHz`;
    }
    return `${hz.toFixed(1)} Hz`;
}

function fmtTime(us) {
    const abs = Math.abs(us);
    if (abs >= 1e6) {
        return `${(us / 1e6).toFixed(1)}s`;
    }
    if (abs >= 1e3) {
        return `${(us / 1e3).toFixed(1)}ms`;
    }
    return `${us.toFixed(0)}us`;
}

/* ============================================================
   DOM helpers
   ============================================================ */
function set(id, txt) {
    const el = document.getElementById(id);
    if (el) {
        el.textContent = txt;
    }
}

function setStatus(type, txt) {
    const dot = document.getElementById('statusDot');
    const span = document.getElementById('statusText');
    dot.className = 'status-dot' +
        (type === 'connected' ? ' connected' : type === 'error' ? ' error' : '');
    span.textContent = txt;
}

function setMsg(msg) {
    set('statusMsg', msg);
}

function sendConfig() {
    if (state.ws && state.ws.readyState === WebSocket.OPEN) {
        state.ws.send(JSON.stringify({ sample_rate: state.sampleRate }));
    }
}

/* ============================================================
   Controls
   ============================================================ */
function initControls() {
    document.getElementById('timebaseSelect').addEventListener('change', (e) => {
        state.timebase = parseInt(e.target.value, 10);
    });

    document.getElementById('sampleRateSelect').addEventListener('change', (e) => {
        state.sampleRate = parseInt(e.target.value, 10);
        sendConfig();
    });

    document.getElementById('displayMode').addEventListener('change', (e) => {
        state.displayMode = e.target.value;
    });

    document.getElementById('ch1Enable').addEventListener('change', (e) => {
        state.ch1.enabled = e.target.checked;
        document.getElementById('col-ch1').style.opacity = e.target.checked ? '1' : '0.4';
    });
    document.getElementById('ch1VdivSelect').addEventListener('change', (e) => {
        state.ch1.vdiv = parseFloat(e.target.value);
    });
    document.getElementById('ch1Offset').addEventListener('input', (e) => {
        state.ch1.offset = parseInt(e.target.value, 10);
        set('ch1OffsetVal', e.target.value);
    });

    document.getElementById('ch2Enable').addEventListener('change', (e) => {
        state.ch2.enabled = e.target.checked;
        document.getElementById('col-ch2').style.opacity = e.target.checked ? '1' : '0.4';
    });
    document.getElementById('ch2VdivSelect').addEventListener('change', (e) => {
        state.ch2.vdiv = parseFloat(e.target.value);
    });
    document.getElementById('ch2Offset').addEventListener('input', (e) => {
        state.ch2.offset = parseInt(e.target.value, 10);
        set('ch2OffsetVal', e.target.value);
    });

    document.getElementById('persistence').addEventListener('input', (e) => {
        state.persistence = parseInt(e.target.value, 10);
        set('persistenceVal', state.persistence === 0 ? '关' : String(state.persistence));
    });

    document.getElementById('btnRun').addEventListener('click', () => {
        state.running = true;
        state.singleShot = false;
        state.singleCaptured = false;
        document.getElementById('btnRun').classList.add('active');
        document.getElementById('btnStop').classList.remove('active');
        setMsg('运行中...');
    });

    document.getElementById('btnStop').addEventListener('click', () => {
        state.running = false;
        state.singleShot = false;
        document.getElementById('btnStop').classList.add('active');
        document.getElementById('btnRun').classList.remove('active');
        setMsg('已停止');
    });

    document.getElementById('btnSingle').addEventListener('click', () => {
        state.running = true;
        state.singleShot = true;
        state.singleCaptured = false;
        document.getElementById('btnRun').classList.remove('active');
        document.getElementById('btnStop').classList.remove('active');
        setMsg('等待触发...');
    });

    canvasCh1.addEventListener('mousemove', (e) => {
        const rect = canvasCh1.getBoundingClientRect();
        state.cursor.ch1 = { x: e.clientX - rect.left, y: e.clientY - rect.top, show: true };
    });
    canvasCh1.addEventListener('mouseleave', () => {
        state.cursor.ch1.show = false;
    });

    canvasCh2.addEventListener('mousemove', (e) => {
        const rect = canvasCh2.getBoundingClientRect();
        state.cursor.ch2 = { x: e.clientX - rect.left, y: e.clientY - rect.top, show: true };
    });
    canvasCh2.addEventListener('mouseleave', () => {
        state.cursor.ch2.show = false;
    });

    document.addEventListener('keydown', (e) => {
        if (e.key === ' ') {
            e.preventDefault();
            if (state.running) {
                document.getElementById('btnStop').click();
            } else {
                document.getElementById('btnRun').click();
            }
        } else if (e.key === 's' || e.key === 'S') {
            document.getElementById('btnSingle').click();
        }
    });
}

/* ============================================================
   Demo mode
   ============================================================ */
function startDemo() {
    setStatus('error', '演示模式');
    setMsg('未连接到 ESP32-S3，显示演示波形');

    let t = 0;
    const rate = 1000;
    const sampleCount = 256;

    function tick() {
        if (state.ws && state.ws.readyState === WebSocket.OPEN) {
            return;
        }

        const ch1 = new Float32Array(sampleCount);
        const ch2 = new Float32Array(sampleCount);
        for (let i = 0; i < sampleCount; i++) {
            const ts = (t + i) / rate;
            ch1[i] = 1650 + 1200 * Math.sin(2 * Math.PI * 40 * ts);
            ch2[i] = 1650 + 800 * Math.sin(2 * Math.PI * 90 * ts + Math.PI / 4);
        }
        t += sampleCount;

        state.frameCount++;
        const now = performance.now();
        if (now - state.fpsTimer >= 1000) {
            state.fps = state.frameCount;
            state.frameCount = 0;
            state.fpsTimer = now;
            set('frameRate', `${state.fps} fps`);
        }
        set('sampleRate', fmtHz(rate));
        set('bufferSize', `${sampleCount} pts`);

        if (state.running) {
            renderCh(canvasCh1, ctxCh1, ch1, rate, state.ch1, state.cursor.ch1);
            renderCh(canvasCh2, ctxCh2, ch2, rate, state.ch2, state.cursor.ch2);
            calcMeas(ch1, rate, 'ch1');
            calcMeas(ch2, rate, 'ch2');
        }

        setTimeout(tick, 333);
    }

    tick();
}

/* ============================================================
   Init
   ============================================================ */
function init() {
    resizeAll();
    window.addEventListener('resize', resizeAll);
    initControls();
    document.getElementById('btnRun').classList.add('active');
    connectWS();

    setTimeout(() => {
        if (!state.ws || state.ws.readyState !== WebSocket.OPEN) {
            startDemo();
        }
    }, 2000);
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}
