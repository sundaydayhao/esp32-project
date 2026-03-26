// ESP32-S3 ADS-B 飞机追踪 - 客户端脚本
'use strict';

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------
let ws           = null;
let aircraftMap  = new Map();   // icao → aircraft object
let selectedIcao = null;
let mapCanvas, mapCtx;
let mapW = 0, mapH = 0;

// 地图视图（经纬度中心 + 缩放）
let mapCenter = { lat: 35.0, lon: 105.0 };
let mapZoom   = 5;   // 度/100px

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
    mapCanvas.addEventListener('mousedown', e => { dragging = true; dragStart = { x: e.clientX, y: e.clientY }; });
    mapCanvas.addEventListener('mousemove', e => {
        if (!dragging) return;
        const dx = e.clientX - dragStart.x;
        const dy = e.clientY - dragStart.y;
        const scale = mapZoom / 100;
        mapCenter.lon -= dx * scale;
        mapCenter.lat += dy * scale;
        dragStart = { x: e.clientX, y: e.clientY };
        drawMap();
    });
    mapCanvas.addEventListener('mouseup',   () => { dragging = false; });
    mapCanvas.addEventListener('mouseleave',() => { dragging = false; });

    connectWebSocket();
    setInterval(fetchStats, 5000);
    setInterval(drawMap, 1000);
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
// WebSocket
// ---------------------------------------------------------------------------
function connectWebSocket() {
    const host = window.location.host;
    ws = new WebSocket('ws://' + host + '/ws');

    ws.onopen = () => {
        setWsStatus(true);
    };

    ws.onmessage = (evt) => {
        try {
            const data = JSON.parse(evt.data);
            if (data.aircraft) updateAircraft(data.aircraft);
        } catch(e) {}
    };

    ws.onerror = () => {};

    ws.onclose = () => {
        setWsStatus(false);
        setTimeout(connectWebSocket, 3000);
    };
}

function setWsStatus(connected) {
    const dot = document.getElementById('ws-dot');
    const txt = document.getElementById('ws-status');
    dot.className = 'dot' + (connected ? ' active' : '');
    txt.textContent = connected ? '已连接' : '未连接';
}

// ---------------------------------------------------------------------------
// 飞机数据更新
// ---------------------------------------------------------------------------
function updateAircraft(list) {
    const now = Date.now();
    list.forEach(ac => {
        aircraftMap.set(ac.icao, { ...ac, _ts: now });
    });

    // 移除超时飞机（60秒）
    for (const [icao, ac] of aircraftMap) {
        if (now - ac._ts > 60000) aircraftMap.delete(icao);
    }

    document.getElementById('ac-count').textContent  = aircraftMap.size;
    document.getElementById('list-count').textContent = aircraftMap.size;

    renderList();
    drawMap();

    if (selectedIcao && aircraftMap.has(selectedIcao)) {
        showDetail(aircraftMap.get(selectedIcao));
    }
}

// ---------------------------------------------------------------------------
// 飞机列表渲染
// ---------------------------------------------------------------------------
function renderList() {
    const container = document.getElementById('aircraft-list');
    const sorted = [...aircraftMap.values()].sort((a, b) => b.msgs - a.msgs);

    container.innerHTML = sorted.map(ac => {
        const alt   = ac.alt   ? ac.alt + ' ft'   : '—';
        const speed = ac.speed ? ac.speed + ' kt'  : '—';
        const hdg   = ac.heading ? ac.heading + '°' : '—';
        const sel   = ac.icao === selectedIcao ? ' selected' : '';
        return `<div class="ac-row${sel}" onclick="selectAircraft('${ac.icao}')">
          <div><span class="ac-icao">${ac.icao}</span><span class="ac-callsign">${ac.callsign || ''}</span></div>
          <div class="ac-info">高度: ${alt} &nbsp; 速度: ${speed} &nbsp; 航向: ${hdg}</div>
        </div>`;
    }).join('');
}

function selectAircraft(icao) {
    selectedIcao = icao;
    const ac = aircraftMap.get(icao);
    if (ac) {
        showDetail(ac);
        if (ac.pos_valid) {
            mapCenter = { lat: ac.lat, lon: ac.lon };
            drawMap();
        }
    }
    renderList();
}

function showDetail(ac) {
    const panel = document.getElementById('detail-panel');
    panel.classList.add('show');
    document.getElementById('detail-icao').textContent = ac.icao;
    document.getElementById('d-callsign').textContent  = ac.callsign || '—';
    document.getElementById('d-alt').textContent       = ac.alt ? ac.alt + ' ft' : '—';
    document.getElementById('d-speed').textContent     = ac.speed ? ac.speed + ' kt' : '—';
    document.getElementById('d-heading').textContent   = ac.heading ? ac.heading + '°' : '—';
    document.getElementById('d-vrate').textContent     = ac.vrate ? ac.vrate + ' ft/min' : '—';
    document.getElementById('d-pos').textContent       = ac.pos_valid
        ? ac.lat.toFixed(4) + ', ' + ac.lon.toFixed(4) : '—';
    document.getElementById('d-msgs').textContent      = ac.msgs || 0;
}

// ---------------------------------------------------------------------------
// 地图绘制
// ---------------------------------------------------------------------------
function latLonToXY(lat, lon) {
    const scale = 100 / mapZoom;
    const x = mapW / 2 + (lon - mapCenter.lon) * scale;
    const y = mapH / 2 - (lat - mapCenter.lat) * scale;
    return { x, y };
}

function drawMap() {
    if (!mapCtx) return;
    mapCtx.fillStyle = '#0d1117';
    mapCtx.fillRect(0, 0, mapW, mapH);

    // 网格
    drawGrid();

    // 飞机
    for (const ac of aircraftMap.values()) {
        if (!ac.pos_valid) continue;
        const { x, y } = latLonToXY(ac.lat, ac.lon);
        if (x < -20 || x > mapW + 20 || y < -20 || y > mapH + 20) continue;

        const isSelected = ac.icao === selectedIcao;
        const color = isSelected ? '#f0a500' : '#58a6ff';

        // 飞机图标（三角形）
        mapCtx.save();
        mapCtx.translate(x, y);
        mapCtx.rotate((ac.heading || 0) * Math.PI / 180);
        mapCtx.beginPath();
        mapCtx.moveTo(0, -10);
        mapCtx.lineTo(-6, 8);
        mapCtx.lineTo(0, 4);
        mapCtx.lineTo(6, 8);
        mapCtx.closePath();
        mapCtx.fillStyle = color;
        mapCtx.fill();
        mapCtx.restore();

        // 标签
        if (isSelected || mapZoom < 3) {
            mapCtx.fillStyle = color;
            mapCtx.font = '11px monospace';
            mapCtx.fillText(ac.callsign || ac.icao, x + 10, y - 4);
            if (ac.alt) mapCtx.fillText(ac.alt + 'ft', x + 10, y + 8);
        }
    }

    // 比例尺
    drawScale();
}

function drawGrid() {
    const scale = 100 / mapZoom;
    const step  = mapZoom > 10 ? 1 : mapZoom > 3 ? 5 : 10;

    mapCtx.strokeStyle = '#21262d';
    mapCtx.lineWidth   = 1;
    mapCtx.fillStyle   = '#8b949e';
    mapCtx.font        = '10px monospace';

    const lonStart = Math.floor((mapCenter.lon - mapW / 2 / scale) / step) * step;
    const lonEnd   = Math.ceil((mapCenter.lon + mapW / 2 / scale) / step) * step;
    for (let lon = lonStart; lon <= lonEnd; lon += step) {
        const x = mapW / 2 + (lon - mapCenter.lon) * scale;
        mapCtx.beginPath();
        mapCtx.moveTo(x, 0);
        mapCtx.lineTo(x, mapH);
        mapCtx.stroke();
        mapCtx.fillText(lon + '°', x + 2, mapH - 4);
    }

    const latStart = Math.floor((mapCenter.lat - mapH / 2 / scale) / step) * step;
    const latEnd   = Math.ceil((mapCenter.lat + mapH / 2 / scale) / step) * step;
    for (let lat = latStart; lat <= latEnd; lat += step) {
        const y = mapH / 2 - (lat - mapCenter.lat) * scale;
        mapCtx.beginPath();
        mapCtx.moveTo(0, y);
        mapCtx.lineTo(mapW, y);
        mapCtx.stroke();
        mapCtx.fillText(lat + '°', 4, y - 2);
    }
}

function drawScale() {
    const scale = 100 / mapZoom;
    const km100 = scale * (100 / 111);  // 100km in pixels
    mapCtx.strokeStyle = '#8b949e';
    mapCtx.lineWidth   = 2;
    mapCtx.beginPath();
    mapCtx.moveTo(16, mapH - 16);
    mapCtx.lineTo(16 + km100, mapH - 16);
    mapCtx.stroke();
    mapCtx.fillStyle = '#8b949e';
    mapCtx.font = '10px monospace';
    mapCtx.fillText('100 km', 16, mapH - 20);
}

function onMapClick(e) {
    const rect = mapCanvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    const scale = 100 / mapZoom;

    let closest = null, minDist = 20;
    for (const ac of aircraftMap.values()) {
        if (!ac.pos_valid) continue;
        const p = latLonToXY(ac.lat, ac.lon);
        const d = Math.hypot(p.x - x, p.y - y);
        if (d < minDist) { minDist = d; closest = ac; }
    }

    if (closest) {
        selectAircraft(closest.icao);
    } else {
        selectedIcao = null;
        document.getElementById('detail-panel').classList.remove('show');
        renderList();
    }
}

function onMapWheel(e) {
    mapZoom *= e.deltaY > 0 ? 1.2 : 0.8;
    mapZoom = Math.max(0.5, Math.min(50, mapZoom));
    drawMap();
}

// ---------------------------------------------------------------------------
// 统计信息
// ---------------------------------------------------------------------------
async function fetchStats() {
    try {
        const res  = await fetch('/api/stats');
        const data = await res.json();
        document.getElementById('msg-count').textContent = data.valid_msgs  || 0;
        document.getElementById('crc-count').textContent = data.crc_errors  || 0;
        const s = data.uptime_s || 0;
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
        document.getElementById('uptime').textContent =
            h > 0 ? `${h}h${m}m` : m > 0 ? `${m}m${sec}s` : `${sec}s`;
    } catch(e) {}
}