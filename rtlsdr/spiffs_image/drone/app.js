'use strict';

let ws = null, isRunning = false;
let lastEvents = [], lastBands = [];

window.addEventListener('DOMContentLoaded', () => {
    fetchStatus();
    setInterval(fetchStats, 3000);
    loadConfig();
});

// ---------------------------------------------------------------------------
// 状态 / 统计
// ---------------------------------------------------------------------------
async function fetchStatus() {
    try {
        const r = await fetch('/drone/status');
        const d = await r.json();
        if (d.active) {
            setStatus('扫描中', 'active');
            document.getElementById('btn-start').disabled = true;
            document.getElementById('btn-stop').disabled  = false;
            isRunning = true;
            connectWS();
        } else {
            setStatus('就绪', '');
        }
        updateStats(d);
        if (d.bands)  renderBands(d.bands);
        if (d.events) renderLog(d.events);
    } catch(e) { setStatus('无法连接', ''); }
}

async function fetchStats() {
    if (!isRunning) return;
    try {
        const r = await fetch('/drone/status');
        const d = await r.json();
        updateStats(d);
        if (d.bands)  renderBands(d.bands);
        if (d.events) renderLog(d.events);
    } catch(e) {}
}

function updateStats(d) {
    document.getElementById('scan-count').textContent    = d.scan_count    || 0;
    document.getElementById('total-alerts').textContent  = d.total_alerts  || 0;
    const active = (d.events || []).filter(e => e.active).length;
    document.getElementById('active-alerts').textContent = active;
    const s = d.uptime_s || 0;
    const h = Math.floor(s/3600), m = Math.floor((s%3600)/60), sec = s%60;
    document.getElementById('uptime').textContent = h>0?`${h}h${m}m`:m>0?`${m}m${sec}s`:`${sec}s`;

    // 大告警横幅
    const banner = document.getElementById('alert-banner');
    if (active > 0) banner.classList.add('show');
    else            banner.classList.remove('show');
}

// ---------------------------------------------------------------------------
// 启动 / 停止
// ---------------------------------------------------------------------------
async function startScan() {
    document.getElementById('btn-start').disabled = true;
    setStatus('正在启动...', 'loading');
    try {
        const r = await fetch('/drone/start', { method: 'POST' });
        const d = await r.json();
        if (!d.ok) throw new Error();
    } catch(e) {
        setStatus('启动失败', '');
        document.getElementById('btn-start').disabled = false;
        return;
    }
    isRunning = true;
    document.getElementById('btn-stop').disabled = false;
    setStatus('扫描中', 'active');
    connectWS();
}

async function stopScan() {
    isRunning = false;
    if (ws) { ws.close(); ws = null; }
    try { await fetch('/drone/stop', { method: 'POST' }); } catch(e) {}
    document.getElementById('btn-start').disabled = false;
    document.getElementById('btn-stop').disabled  = true;
    setStatus('已停止', '');
    document.getElementById('alert-banner').classList.remove('show');
}

async function clearLog() {
    try { await fetch('/drone/clear', { method: 'POST' }); } catch(e) {}
    document.getElementById('log-list').innerHTML =
        '<div style="padding:20px;text-align:center;color:#8b949e">日志已清除</div>';
    lastEvents = [];
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------
function connectWS() {
    ws = new WebSocket('ws://' + location.host + '/drone/ws');
    ws.onmessage = e => {
        try {
            const d = JSON.parse(e.data);
            updateStats(d);
            if (d.bands)  renderBands(d.bands);
            if (d.events) renderLog(d.events);
        } catch(ex) {}
    };
    ws.onclose = () => {
        if (isRunning) { setStatus('重连中...','loading'); setTimeout(connectWS, 3000); }
    };
}

// ---------------------------------------------------------------------------
// 频段渲染
// ---------------------------------------------------------------------------
function renderBands(bands) {
    const container = document.getElementById('band-list');
    container.innerHTML = bands.map(b => {
        const alertClass = b.alert === 3 ? 'alert-high' :
                           b.alert === 2 ? 'alert-med'  :
                           b.alert === 1 ? 'alert-low'  : '';
        const badgeClass = b.alert === 3 ? 'badge-high' :
                           b.alert === 2 ? 'badge-med'  :
                           b.alert === 1 ? 'badge-low'  : 'badge-none';

        // 功率条：-60dBFS ~ 0dBFS → 0~100%
        const pct = Math.max(0, Math.min(100, (b.power + 60) / 60 * 100));
        const barColor = b.alert === 3 ? '#f85149' :
                         b.alert === 2 ? '#f0883e' :
                         b.alert === 1 ? '#d29922' : '#3fb950';

        return `<div class="band-card ${alertClass}">
          <div style="display:flex;justify-content:space-between;align-items:center">
            <span class="band-name">${b.name}</span>
            <span class="alert-badge ${badgeClass}">${b.alert_str}</span>
          </div>
          <div class="band-desc">${b.desc} &nbsp; ${(b.freq/1e6).toFixed(3)} MHz</div>
          <div class="band-meter"><div class="band-meter-fill" style="width:${pct}%;background:${barColor}"></div></div>
          <div class="band-vals">
            <span>功率: ${b.power.toFixed(1)} dBFS</span>
            <span>噪底: ${b.noise.toFixed(1)} dBFS</span>
            <span>SNR: <strong style="color:${barColor}">${b.snr.toFixed(1)} dB</strong></span>
            <span>触发: ${b.hits}</span>
          </div>
        </div>`;
    }).join('');
}

// ---------------------------------------------------------------------------
// 告警日志渲染
// ---------------------------------------------------------------------------
function renderLog(events) {
    if (!events || events.length === 0) return;
    const container = document.getElementById('log-list');
    container.innerHTML = events.map(e => {
        const lvlClass = e.level === 3 ? 'high' : e.level === 2 ? 'med' : 'low';
        const ts = new Date(e.ts * 1000).toLocaleTimeString('zh-CN');
        return `<div class="log-row">
          <div class="log-active ${e.active ? 'on' : 'off'}"></div>
          <span class="log-ts">${ts}</span>
          <span class="log-band">${e.band}</span>
          <span class="log-power">${e.power.toFixed(1)} dBFS</span>
          <span class="log-snr">SNR ${e.snr.toFixed(1)}dB</span>
          <span class="log-level ${lvlClass}">&#9888; ${e.level_str}级告警</span>
        </div>`;
    }).join('');
    container.scrollTop = 0;
}

// ---------------------------------------------------------------------------
// 配置
// ---------------------------------------------------------------------------
async function loadConfig() {
    try {
        const r = await fetch('/drone/config');
        const d = await r.json();
        document.getElementById('cfg-low').value   = d.snr_low  || 10;
        document.getElementById('cfg-med').value   = d.snr_med  || 20;
        document.getElementById('cfg-high').value  = d.snr_high || 30;
        document.getElementById('cfg-dwell').value = d.dwell_ms || 300;
    } catch(e) {}
}

async function saveConfig() {
    const cfg = {
        snr_low:  parseFloat(document.getElementById('cfg-low').value),
        snr_med:  parseFloat(document.getElementById('cfg-med').value),
        snr_high: parseFloat(document.getElementById('cfg-high').value),
        dwell_ms: parseInt(document.getElementById('cfg-dwell').value),
    };
    try {
        await fetch('/drone/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(cfg),
        });
    } catch(e) {}
}

function setStatus(text, state) {
    document.getElementById('status-text').textContent = text;
    const dot = document.getElementById('status-dot');
    dot.className = 'dot' + (state ? ' ' + state : '');
}

window.addEventListener('beforeunload', () => {
    if (isRunning) fetch('/drone/stop', { method: 'POST', keepalive: true });
});
