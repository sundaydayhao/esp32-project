// ESP32-S3 AIS 船舶监控 - 前端脚本
'use strict';

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------
let ws          = null;
let isRunning   = false;
let vesselMap   = new Map();   // mmsi → vessel
let selectedMmsi = null;

let mapCanvas, mapCtx;
let mapW = 0, mapH = 0;
let mapCenter = { lat: 30.0, lon: 121.0 };  // 默认中心：上海附近
let mapZoom   = 3;   // 度/100px

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------
window.addEventListener('DOMContentLoaded', () => {
    mapCanvas = document.getElementById('map-canvas');
    mapCtx    = mapCanvas.getContext('2d');

    resizeMap();
    window.addEventListener('resize', resizeMap);

    mapCanvas.addEventListener('click', onMapClick);
    mapCanvas.addEventListener('wheel', onMapWheel, { passive: true });

    // 拖拽平移
    let dragging = false, dragStart = null;
    mapCanvas.addEventListener('mousedown', e => {
        dragging = true; dragStart = { x: e.clientX, y: e.clientY };
    });
    mapCanvas.addEventListener('mousemove', e => {
        if (!dragging) return;
        const scale = mapZoom / 100;
        mapCenter.lon -= (e.clientX - dragStart.x) * scale;
        mapCenter.lat += (e.clientY - dragStart.y) * scale;
        dragStart = { x: e.clientX, y: e.clientY };
        drawMap();
    });
    mapCanvas.addEventListener('mouseup',    () => { dragging = false; });
    mapCanvas.addEventListener('mouseleave', () => { dragging = false; });

    fetchStatus();
    setInterval(fetchStats, 5000);
    setInterval(drawMap, 2000);
    drawMap();
});

function resizeMap() {
    mapCanvas.width  = mapCanvas.offsetWidth;
    mapCanvas.height = mapCanvas.offsetHeight;
    mapW = mapCanvas.width;
    mapH = mapCanvas.height;
    drawMap();
}

// ---------------------------------------------------------------------------
// 状态查询
// ---------------------------------------------------------------------------
async function fetchStatus() {
    try {
        const res  = await fetch('/ais/status');
        const data = await res.json();
        if (data.active) {
            setStatus('已在运行', 'active');
            document.getElementById('btn-start').disabled = true;
            document.getElementById('btn-stop').disabled  = false;
            isRunning = true;
            connectWebSocket();
        } else {
            setStatus('就绪', '');
        }
    } catch(e) {
        setStatus('无法连接', '');
    }
}

async function fetchStats() {
    try {
        const res  = await fetch('/ais/status');
        const data = await res.json();
        document.getElementById('valid-frames').textContent = data.valid_frames || 0;
        document.getElementById('crc-errors').textContent   = data.crc_errors   || 0;
        document.getElementById('ch-a').textContent         = data.ch_a_frames  || 0;
        document.getElementById('ch-b').textContent         = data.ch_b_frames  || 0;
        const s = data.uptime_s || 0;
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
        document.getElementById('uptime').textContent =
            h > 0 ? `${h}h${m}m` : m > 0 ? `${m}m${sec}s` : `${sec}s`;
    } catch(e) {}
}

// ---------------------------------------------------------------------------
// 启动 / 停止
// ---------------------------------------------------------------------------
async function startAIS() {
    if (isRunning) return;
    document.getElementById('btn-start').disabled = true;
    setStatus('正在启动...', 'loading');
    try {
        const res  = await fetch('/ais/start', { method: 'POST' });
        const data = await res.json();
        if (!data.ok) throw new Error('启动失败');
    } catch(e) {
        setStatus('启动失败: ' + e.message, '');
        document.getElementById('btn-start').disabled = false;
        return;
    }
    isRunning = true;
    document.getElementById('btn-stop').disabled = false;
    setStatus('接收中 161.975/162.025 MHz', 'active');
    connectWebSocket();
}

async function stopAIS() {
    isRunning = false;
    if (ws) { ws.close(); ws = null; }
    try { await fetch('/ais/stop', { method: 'POST' }); } catch(e) {}
    document.getElementById('btn-start').disabled = false;
    document.getElementById('btn-stop').disabled  = true;
    setStatus('已停止', '');
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------
function connectWebSocket() {
    const host = window.location.host;
    ws = new WebSocket('ws://' + host + '/ais/ws');

    ws.onopen = () => {
        setStatus('接收中 161.975/162.025 MHz', 'active');
    };

    ws.onmessage = (evt) => {
        try {
            const data = JSON.parse(evt.data);
            if (data.vessels) processVessels(data.vessels);
        } catch(e) {}
    };

    ws.onerror = () => {};

    ws.onclose = () => {
        if (isRunning) {
            setStatus('重连中...', 'loading');
            setTimeout(connectWebSocket, 3000);
        }
    };
}

// ---------------------------------------------------------------------------
// 船只数据处理
// ---------------------------------------------------------------------------
function processVessels(list) {
    const now = Date.now();
    list.forEach(v => {
        vesselMap.set(v.mmsi, { ...v, _ts: now });
    });

    // 移除超时（10分钟）
    for (const [mmsi, v] of vesselMap) {
        if (now - v._ts > 600000) vesselMap.delete(mmsi);
    }

    const count = vesselMap.size;
    document.getElementById('vessel-count').textContent = count;
    document.getElementById('list-count').textContent   = count + ' 艘';

    renderVesselList();
    drawMap();

    if (selectedMmsi && vesselMap.has(selectedMmsi)) {
        showDetail(vesselMap.get(selectedMmsi));
    }
}

// ---------------------------------------------------------------------------
// 船只列表渲染
// ---------------------------------------------------------------------------
function renderVesselList() {
    const container = document.getElementById('vessel-list');
    const sorted = [...vesselMap.values()].sort((a, b) => b.msgs - a.msgs);

    container.innerHTML = sorted.map(v => {
        const name = v.name || v.mmsi;
        const sog  = v.sog  ? v.sog.toFixed(1) + ' kn' : '—';
        const cog  = v.cog  ? v.cog.toFixed(0) + '°'   : '—';
        const sel  = v.mmsi === selectedMmsi ? ' selected' : '';

        let statusClass = 'other', statusText = v.nav_str || '未知';
        if (v.nav_status === 0) { statusClass = 'underway'; }
        else if (v.nav_status === 1 || v.nav_status === 5) { statusClass = 'anchored'; }
        else if (v.nav_status === 5) { statusClass = 'moored'; }

        return `<div class="v-row${sel}" onclick="selectVessel('${v.mmsi}')">
          <div>
            <span class="v-mmsi">${v.mmsi}</span>
            <span class="v-name">${name}</span>
            <span class="v-status ${statusClass}">${statusText}</span>
          </div>
          <div class="v-type">${v.ship_str || '未知船型'}</div>
          <div class="v-info">
            ${v.pos_valid ? `${v.lat.toFixed(3)}°, ${v.lon.toFixed(3)}°` : '无位置'}
            &nbsp;|&nbsp; 速度: ${sog} &nbsp; 航向: ${cog}
          </div>
        </div>`;
    }).join('');
}

function selectVessel(mmsi) {
    selectedMmsi = mmsi;
    const v = vesselMap.get(mmsi);
    if (v) {
        showDetail(v);
        if (v.pos_valid) {
            mapCenter = { lat: v.lat, lon: v.lon };
            drawMap();
        }
    }
    renderVesselList();
}

function showDetail(v) {
    const panel = document.getElementById('detail-panel');
    panel.classList.add('show');
    document.getElementById('d-name').textContent     = v.name || '未知船名';
    document.getElementById('d-mmsi').textContent     = v.mmsi;
    document.getElementById('d-callsign').textContent = v.callsign || '—';
    document.getElementById('d-imo').textContent      = v.imo ? v.imo : '—';
    document.getElementById('d-shiptype').textContent = v.ship_str || '—';
    document.getElementById('d-navstatus').textContent= v.nav_str  || '—';
    document.getElementById('d-pos').textContent      = v.pos_valid
        ? v.lat.toFixed(4) + '°, ' + v.lon.toFixed(4) + '°' : '无位置';
    document.getElementById('d-sog').textContent      = v.sog ? v.sog.toFixed(1) + ' 节' : '—';
    document.getElementById('d-cog').textContent      = v.cog ? v.cog.toFixed(1) + '°' : '—';
    document.getElementById('d-dest').textContent     = v.destination || '—';
    document.getElementById('d-msgs').textContent     = v.msgs || 0;
}

// ---------------------------------------------------------------------------
// 地图绘制
// ---------------------------------------------------------------------------
function latLonToXY(lat, lon) {
    const scale = 100 / mapZoom;
    return {
        x: mapW / 2 + (lon - mapCenter.lon) * scale,
        y: mapH / 2 - (lat - mapCenter.lat) * scale,
    };
}

function vesselColor(v) {
    if (v.nav_status === 0) return '#3fb950';          // 航行中 - 绿
    if (v.nav_status === 1 || v.nav_status === 5) return '#d29922'; // 锚泊/系泊 - 黄
    const t = v.ship_type || 0;
    if (t >= 60 && t <= 69) return '#58a6ff';          // 客船 - 蓝
    if (t >= 70 && t <= 79) return '#58a6ff';          // 货船 - 蓝
    if (t >= 80 && t <= 89) return '#f85149';          // 油轮 - 红
    return '#e6edf3';                                   // 其他 - 白
}

function drawVesselIcon(ctx, x, y, heading, color, size) {
    ctx.save();
    ctx.translate(x, y);
    // 使用 COG 或 heading（511 = 不可用）
    const angle = (heading < 360 ? heading : 0) * Math.PI / 180;
    ctx.rotate(angle);
    // 船形图标（菱形 + 船头三角）
    ctx.beginPath();
    ctx.moveTo(0, -size);          // 船头
    ctx.lineTo(size * 0.5, size * 0.3);
    ctx.lineTo(0, size * 0.1);
    ctx.lineTo(-size * 0.5, size * 0.3);
    ctx.closePath();
    ctx.fillStyle = color;
    ctx.fill();
    ctx.restore();
}

function drawMap() {
    if (!mapCtx) return;
    mapCtx.fillStyle = '#0a1628';
    mapCtx.fillRect(0, 0, mapW, mapH);

    drawGrid();

    for (const v of vesselMap.values()) {
        if (!v.pos_valid) continue;
        const { x, y } = latLonToXY(v.lat, v.lon);
        if (x < -20 || x > mapW + 20 || y < -20 || y > mapH + 20) continue;

        const isSelected = v.mmsi === selectedMmsi;
        const color = isSelected ? '#f0c040' : vesselColor(v);
        const size  = isSelected ? 10 : 7;

        drawVesselIcon(mapCtx, x, y, v.heading < 511 ? v.heading : v.cog, color, size);

        // 选中或缩放小时显示标签
        if (isSelected || mapZoom < 2) {
            mapCtx.fillStyle = color;
            mapCtx.font = '11px monospace';
            const label = v.name || v.mmsi;
            mapCtx.fillText(label, x + size + 3, y - 3);
            if (v.sog) mapCtx.fillText(v.sog.toFixed(1) + 'kn', x + size + 3, y + 9);
        }
    }

    drawScale();
}

function drawGrid() {
    const scale = 100 / mapZoom;
    const step  = mapZoom > 8 ? 0.5 : mapZoom > 3 ? 2 : 5;

    mapCtx.strokeStyle = '#1a2a3a';
    mapCtx.lineWidth   = 1;
    mapCtx.fillStyle   = '#4a6a8a';
    mapCtx.font        = '10px monospace';

    const lonStart = Math.floor((mapCenter.lon - mapW / 2 / scale) / step) * step;
    const lonEnd   = Math.ceil((mapCenter.lon + mapW / 2 / scale) / step) * step;
    for (let lon = lonStart; lon <= lonEnd; lon += step) {
        const x = mapW / 2 + (lon - mapCenter.lon) * scale;
        mapCtx.beginPath(); mapCtx.moveTo(x, 0); mapCtx.lineTo(x, mapH); mapCtx.stroke();
        mapCtx.fillText(lon.toFixed(1) + '°E', x + 2, mapH - 4);
    }

    const latStart = Math.floor((mapCenter.lat - mapH / 2 / scale) / step) * step;
    const latEnd   = Math.ceil((mapCenter.lat + mapH / 2 / scale) / step) * step;
    for (let lat = latStart; lat <= latEnd; lat += step) {
        const y = mapH / 2 - (lat - mapCenter.lat) * scale;
        mapCtx.beginPath(); mapCtx.moveTo(0, y); mapCtx.lineTo(mapW, y); mapCtx.stroke();
        mapCtx.fillText(lat.toFixed(1) + '°N', 4, y - 2);
    }
}

function drawScale() {
    const scale = 100 / mapZoom;
    const nm50  = scale * (50 / 60);  // 50海里（1海里 ≈ 1/60度）
    mapCtx.strokeStyle = '#4a6a8a';
    mapCtx.lineWidth   = 2;
    mapCtx.beginPath();
    mapCtx.moveTo(16, mapH - 16);
    mapCtx.lineTo(16 + nm50, mapH - 16);
    mapCtx.stroke();
    mapCtx.fillStyle = '#4a6a8a';
    mapCtx.font = '10px monospace';
    mapCtx.fillText('50 NM', 16, mapH - 20);
}

// ---------------------------------------------------------------------------
// 地图交互
// ---------------------------------------------------------------------------
function onMapClick(e) {
    const rect = mapCanvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    let closest = null, minDist = 18;
    for (const v of vesselMap.values()) {
        if (!v.pos_valid) continue;
        const p = latLonToXY(v.lat, v.lon);
        const d = Math.hypot(p.x - x, p.y - y);
        if (d < minDist) { minDist = d; closest = v; }
    }

    if (closest) {
        selectVessel(closest.mmsi);
    } else {
        selectedMmsi = null;
        document.getElementById('detail-panel').classList.remove('show');
        renderVesselList();
        drawMap();
    }
}

function onMapWheel(e) {
    mapZoom *= e.deltaY > 0 ? 1.2 : 0.8;
    mapZoom = Math.max(0.2, Math.min(30, mapZoom));
    drawMap();
}

function mapZoomIn()  { mapZoom *= 0.7; mapZoom = Math.max(0.2, mapZoom); drawMap(); }
function mapZoomOut() { mapZoom *= 1.4; mapZoom = Math.min(30, mapZoom);  drawMap(); }

function mapFitAll() {
    const vessels = [...vesselMap.values()].filter(v => v.pos_valid);
    if (vessels.length === 0) return;
    const lats = vessels.map(v => v.lat);
    const lons = vessels.map(v => v.lon);
    const minLat = Math.min(...lats), maxLat = Math.max(...lats);
    const minLon = Math.min(...lons), maxLon = Math.max(...lons);
    mapCenter = { lat: (minLat + maxLat) / 2, lon: (minLon + maxLon) / 2 };
    const dLat = maxLat - minLat + 1;
    const dLon = maxLon - minLon + 1;
    mapZoom = Math.max(dLat, dLon) * 100 / Math.min(mapW, mapH) * 1.2;
    mapZoom = Math.max(0.2, Math.min(30, mapZoom));
    drawMap();
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
    if (isRunning) fetch('/ais/stop', { method: 'POST', keepalive: true });
});