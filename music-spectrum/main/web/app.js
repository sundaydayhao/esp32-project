/* ═══════════════════════════════════════════════════════════════════════
   ESP32 Music Player – Frontend JavaScript
   ═══════════════════════════════════════════════════════════════════════ */

'use strict';

/* ── Preset internet radio stations ─────────────────────────────────── */
const RADIO_PRESETS = [
  { title: 'Jazz Radio',        artist: 'Jazz24',       url: 'http://live.wostreaming.net/jazz24-aac64' },
  { title: 'Classical KING FM', artist: 'KING FM',      url: 'http://live.wostreaming.net/king-aac64'  },
  { title: 'SomaFM Groove',     artist: 'SomaFM',       url: 'http://ice2.somafm.com/groovesalad-128-mp3' },
  { title: 'SomaFM Drone Zone', artist: 'SomaFM',       url: 'http://ice2.somafm.com/dronezone-128-mp3'  },
  { title: 'Radio Paradise',    artist: 'Radio Paradise',url: 'http://stream.radioparadise.com/mp3-128'   },
];

/* ── DOM refs ────────────────────────────────────────────────────────── */
const $ = id => document.getElementById(id);

const connDot      = $('connDot');
const connLabel    = $('connLabel');
const trackTitle   = $('trackTitle');
const trackArtist  = $('trackArtist');
const trackMeta    = $('trackMeta');
const timePos      = $('timePos');
const timeDur      = $('timeDur');
const progressFill = $('progressFill');
const btnPlay      = $('btnPlay');
const btnStop      = $('btnStop');
const btnMute      = $('btnMute');
const volumeSlider = $('volumeSlider');
const volVal       = $('volVal');
const vinylSpin    = $('vinylSpin');
const specCanvas   = $('spectrumCanvas');
const urlInput     = $('urlInput');
const titleInput   = $('titleInput');
const artistInput  = $('artistInput');
const btnAdd       = $('btnAdd');
const btnRefresh   = $('btnRefresh');
const fileList     = $('fileList');
const radioList    = $('radioList');

/* ── Canvas setup ────────────────────────────────────────────────────── */
const ctx = specCanvas.getContext('2d');
const FFT_BANDS = 64;

function resizeCanvas() {
  specCanvas.width  = specCanvas.offsetWidth  * window.devicePixelRatio;
  specCanvas.height = specCanvas.offsetHeight * window.devicePixelRatio;
}
window.addEventListener('resize', resizeCanvas);
resizeCanvas();

/* ── Spectrum state ──────────────────────────────────────────────────── */
let specBands     = new Float32Array(FFT_BANDS);   // current values
let specPeak      = new Float32Array(FFT_BANDS);   // peak hold
let specPeakTimer = new Uint32Array(FFT_BANDS);    // frames since peak set
let specSmooth    = new Float32Array(FFT_BANDS);   // smoothed display values

const SMOOTH_UP   = 0.85;   // rise speed  (0=instant, 1=never)
const SMOOTH_DOWN = 0.65;   // fall speed
const PEAK_HOLD   = 40;     // frames to hold peak
const PEAK_FALL   = 0.015;  // peak fall per frame

/* ── Spectrum renderer ───────────────────────────────────────────────── */
function drawSpectrum() {
  const W = specCanvas.width;
  const H = specCanvas.height;
  const dpr = window.devicePixelRatio || 1;

  ctx.clearRect(0, 0, W, H);

  /* Background grid */
  ctx.strokeStyle = 'rgba(42,42,80,0.6)';
  ctx.lineWidth = 1;
  for (let y = 0.25; y <= 1; y += 0.25) {
    ctx.beginPath();
    ctx.moveTo(0, H * y);
    ctx.lineTo(W, H * y);
    ctx.stroke();
  }

  const barW   = W / FFT_BANDS;
  const gap    = Math.max(1, barW * 0.15);
  const bw     = barW - gap;

  for (let i = 0; i < FFT_BANDS; i++) {
    /* Smooth */
    const target = specBands[i];
    if (target > specSmooth[i]) {
      specSmooth[i] = specSmooth[i] * SMOOTH_UP   + target * (1 - SMOOTH_UP);
    } else {
      specSmooth[i] = specSmooth[i] * SMOOTH_DOWN + target * (1 - SMOOTH_DOWN);
    }

    /* Peak hold */
    if (specSmooth[i] >= specPeak[i]) {
      specPeak[i]      = specSmooth[i];
      specPeakTimer[i] = 0;
    } else {
      specPeakTimer[i]++;
      if (specPeakTimer[i] > PEAK_HOLD) {
        specPeak[i] = Math.max(0, specPeak[i] - PEAK_FALL);
      }
    }

    const x  = i * barW;
    const bh = Math.max(2, specSmooth[i] * H);
    const y  = H - bh;

    /* Gradient bar */
    const grad = ctx.createLinearGradient(0, H, 0, 0);
    grad.addColorStop(0,   '#7c3aed');
    grad.addColorStop(0.5, '#a855f7');
    grad.addColorStop(1,   '#06b6d4');

    ctx.fillStyle = grad;
    ctx.beginPath();
    ctx.roundRect(x + gap / 2, y, bw, bh, [2, 2, 0, 0]);
    ctx.fill();

    /* Peak line */
    if (specPeak[i] > 0.02) {
      const py = H - specPeak[i] * H;
      ctx.fillStyle = '#e2e8f0';
      ctx.fillRect(x + gap / 2, py - 1, bw, 2);
    }
  }

  requestAnimationFrame(drawSpectrum);
}
requestAnimationFrame(drawSpectrum);

/* ── WebSocket ───────────────────────────────────────────────────────── */
let ws = null;
let wsReconnectTimer = null;

function wsConnect() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  const url   = `${proto}://${location.host}/ws`;

  ws = new WebSocket(url);
  ws.binaryType = 'arraybuffer';

  ws.onopen = () => {
    console.log('WebSocket connected');
    connDot.className   = 'dot connected';
    connLabel.textContent = '已连接';
    clearTimeout(wsReconnectTimer);
  };

  ws.onclose = () => {
    console.log('WebSocket closed, reconnecting...');
    connDot.className   = 'dot disconnected';
    connLabel.textContent = '断开连接';
    wsReconnectTimer = setTimeout(wsConnect, 3000);
  };

  ws.onerror = () => {
    ws.close();
  };

  ws.onmessage = (evt) => {
    if (evt.data instanceof ArrayBuffer) {
      const view = new DataView(evt.data);
      const type = view.getUint8(0);

      if (type === 0x01) {
        /* Spectrum data: 64 x float32 */
        for (let i = 0; i < FFT_BANDS; i++) {
          specBands[i] = view.getFloat32(1 + i * 4, true);
        }
      }
    }
  };
}

wsConnect();

/* ── Status polling ──────────────────────────────────────────────────── */
let currentState    = 'idle';
let currentDuration = 0;
let isMuted         = false;
let savedVolume     = 80;

async function fetchStatus() {
  try {
    const res  = await fetch('/api/status');
    const data = await res.json();
    updateUI(data);
  } catch (e) {
    /* ignore */
  }
}

function updateUI(data) {
  currentState = data.state;

  /* Track info */
  const t = data.track;
  trackTitle.textContent  = t.title  || '未播放';
  trackArtist.textContent = t.artist || '—';

  const metaParts = [];
  if (t.sample_rate) metaParts.push(`${t.sample_rate / 1000}kHz`);
  if (t.channels)    metaParts.push(t.channels === 1 ? 'Mono' : 'Stereo');
  if (t.bitrate)     metaParts.push(`${t.bitrate}kbps`);
  if (t.source)      metaParts.push(t.source === 'http' ? '🌐 在线' : '💾 本地');
  trackMeta.textContent = metaParts.join(' · ');

  /* Progress */
  currentDuration = t.duration || 0;
  const pos = data.position || 0;
  timePos.textContent = fmtTime(pos);
  timeDur.textContent = currentDuration > 0 ? fmtTime(currentDuration) : '—';
  if (currentDuration > 0) {
    progressFill.style.width = `${Math.min(100, (pos / currentDuration) * 100)}%`;
  } else {
    progressFill.style.width = '0%';
  }

  /* Play button */
  if (currentState === 'playing') {
    btnPlay.textContent = '⏸';
    vinylSpin.classList.add('playing');
  } else {
    btnPlay.textContent = '▶';
    vinylSpin.classList.remove('playing');
  }

  /* Volume */
  if (!isMuted) {
    volumeSlider.value = data.volume;
    volVal.textContent = data.volume;
  }
}

function fmtTime(sec) {
  const m = Math.floor(sec / 60);
  const s = Math.floor(sec % 60);
  return `${m}:${s.toString().padStart(2, '0')}`;
}

/* Poll every 2 seconds */
fetchStatus();
setInterval(fetchStatus, 2000);

/* ── Controls ────────────────────────────────────────────────────────── */
btnPlay.addEventListener('click', async () => {
  if (currentState === 'playing') {
    await fetch('/api/pause', { method: 'POST' });
  } else if (currentState === 'paused') {
    await fetch('/api/resume', { method: 'POST' });
  } else {
    /* Nothing playing – prompt user */
    urlInput.focus();
  }
  fetchStatus();
});

btnStop.addEventListener('click', async () => {
  await fetch('/api/stop', { method: 'POST' });
  fetchStatus();
});

btnMute.addEventListener('click', () => {
  isMuted = !isMuted;
  if (isMuted) {
    savedVolume = parseInt(volumeSlider.value);
    setVolume(0);
    btnMute.textContent = '🔇';
  } else {
    setVolume(savedVolume);
    btnMute.textContent = '🔊';
  }
});

volumeSlider.addEventListener('input', () => {
  const v = parseInt(volumeSlider.value);
  volVal.textContent = v;
  if (isMuted && v > 0) {
    isMuted = false;
    btnMute.textContent = '🔊';
  }
  setVolume(v);
});

async function setVolume(v) {
  await fetch('/api/volume', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ volume: v }),
  });
}

/* ── Play a track ────────────────────────────────────────────────────── */
async function playTrack(url, title, artist) {
  if (!url) return;
  const body = { url };
  if (title)  body.title  = title;
  if (artist) body.artist = artist;

  await fetch('/api/play', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  fetchStatus();
}

btnAdd.addEventListener('click', () => {
  const url    = urlInput.value.trim();
  const title  = titleInput.value.trim();
  const artist = artistInput.value.trim();
  if (!url) { urlInput.focus(); return; }
  playTrack(url, title || undefined, artist || undefined);
});

urlInput.addEventListener('keydown', e => {
  if (e.key === 'Enter') btnAdd.click();
});

/* ── Local files ─────────────────────────────────────────────────────── */
async function loadLocalFiles() {
  fileList.innerHTML = '<li class="file-empty">加载中...</li>';
  try {
    const res  = await fetch('/api/files');
    const data = await res.json();
    fileList.innerHTML = '';

    if (!data.files || data.files.length === 0) {
      fileList.innerHTML = '<li class="file-empty">SPIFFS 中没有 MP3 文件</li>';
      return;
    }

    data.files.forEach(f => {
      const li = document.createElement('li');
      li.className = 'file-item';
      li.innerHTML = `<span>🎵</span><span>${f.name}</span>`;
      li.addEventListener('click', () => {
        document.querySelectorAll('.file-item').forEach(el => el.classList.remove('active'));
        li.classList.add('active');
        playTrack(f.path, f.name.replace(/\.mp3$/i, ''), undefined);
      });
      fileList.appendChild(li);
    });
  } catch (e) {
    fileList.innerHTML = '<li class="file-empty">加载失败</li>';
  }
}

btnRefresh.addEventListener('click', loadLocalFiles);
loadLocalFiles();

/* ── Radio presets ───────────────────────────────────────────────────── */
RADIO_PRESETS.forEach(r => {
  const li = document.createElement('li');
  li.className = 'file-item';
  li.innerHTML = `<span>📻</span><span>${r.title}</span><span style="color:var(--text3);font-size:0.75rem">${r.artist}</span>`;
  li.addEventListener('click', () => {
    document.querySelectorAll('.file-item').forEach(el => el.classList.remove('active'));
    li.classList.add('active');
    playTrack(r.url, r.title, r.artist);
  });
  radioList.appendChild(li);
});