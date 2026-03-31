'use strict';

const API = '';  // same origin

/* ---- Tab switching ---- */
function showTab(name) {
  document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
  document.querySelectorAll('.tab-btn').forEach(el => el.classList.remove('active'));
  document.getElementById('tab-' + name).classList.add('active');
  event.target.classList.add('active');
  if (name === 'log') loadLog();
}

/* ---- Toast ---- */
function showToast(msg, duration = 2500) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), duration);
}

/* ---- Status bar ---- */
async function loadStatus() {
  try {
    const r = await fetch(API + '/api/status');
    const d = await r.json();
    const bar = document.getElementById('status-bar');
    const wifi = d.wifi_connected ? `已连接 ${d.ip}` : `AP模式 ${d.ip}`;
    bar.textContent = `🕐 ${d.date} ${d.time}　📶 ${wifi}`;
  } catch (e) {
    document.getElementById('status-bar').textContent = '无法获取状态';
  }
}

/* ---- Reminders ---- */
let remindersData = [];

async function loadReminders() {
  try {
    const r = await fetch(API + '/api/reminders');
    remindersData = await r.json();
    renderReminders();
  } catch (e) {
    showToast('加载提醒失败');
  }
}

function renderReminders() {
  const list = document.getElementById('reminders-list');
  list.innerHTML = '';
  remindersData.forEach((rem, i) => {
    const card = document.createElement('div');
    card.className = 'reminder-card' + (rem.enabled ? '' : ' disabled');
    card.id = 'card-' + i;
    card.innerHTML = `
      <div class="reminder-header">
        <span class="reminder-title">提醒 ${i + 1}</span>
        <label class="toggle-switch">
          <input type="checkbox" id="en-${i}" ${rem.enabled ? 'checked' : ''}
                 onchange="toggleCard(${i})">
          <span class="toggle-slider"></span>
        </label>
      </div>
      <div class="reminder-fields">
        <div class="field-group">
          <label>时间</label>
          <div class="time-row">
            <input type="number" id="hour-${i}" min="0" max="23" value="${rem.hour}"
                   style="width:60px">
            <span class="time-sep">:</span>
            <input type="number" id="min-${i}" min="0" max="59"
                   value="${String(rem.minute).padStart(2,'0')}" style="width:60px">
          </div>
        </div>
        <div class="field-group">
          <label>药品名称</label>
          <input type="text" id="name-${i}" value="${rem.medication_name}" maxlength="24">
        </div>
        <div class="field-group">
          <label>剂量</label>
          <input type="text" id="dose-${i}" value="${rem.dosage}" maxlength="16">
        </div>
      </div>`;
    list.appendChild(card);
  });
}

function toggleCard(i) {
  const enabled = document.getElementById('en-' + i).checked;
  document.getElementById('card-' + i).className =
    'reminder-card' + (enabled ? '' : ' disabled');
}

async function saveReminders() {
  const data = remindersData.map((rem, i) => ({
    id: rem.id,
    hour: parseInt(document.getElementById('hour-' + i).value) || 0,
    minute: parseInt(document.getElementById('min-' + i).value) || 0,
    medication_name: document.getElementById('name-' + i).value.trim(),
    dosage: document.getElementById('dose-' + i).value.trim(),
    enabled: document.getElementById('en-' + i).checked,
  }));
  try {
    const r = await fetch(API + '/api/reminders', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(data),
    });
    const d = await r.json();
    showToast(d.result === 'ok' ? '✅ 保存成功' : '❌ 保存失败');
    remindersData = data;
  } catch (e) {
    showToast('❌ 网络错误');
  }
}

/* ---- Log ---- */
async function loadLog() {
  const box = document.getElementById('log-content');
  box.innerHTML = '加载中...';
  try {
    const r = await fetch(API + '/api/log');
    const text = await r.text();
    const lines = text.trim().split('\n').filter(l => l.trim());
    if (lines.length === 0) {
      box.innerHTML = '<p style="color:#aaa;text-align:center;padding:20px">暂无记录</p>';
      return;
    }
    box.innerHTML = '';
    lines.reverse().forEach(line => {
      try {
        const e = JSON.parse(line);
        const div = document.createElement('div');
        div.className = 'log-entry';
        const statusMap = { taken: ['已服药', 'taken'], snoozed: ['已暂停', 'snoozed'], missed: ['已错过', 'missed'] };
        const [label, cls] = statusMap[e.status] || [e.status, 'taken'];
        div.innerHTML = `
          <span class="log-time">${e.time}</span>
          <span class="log-drug">${e.drug}</span>
          <span class="log-dosage">${e.dosage}</span>
          <span class="badge badge-${cls}">${label}</span>`;
        box.appendChild(div);
      } catch (_) {}
    });
  } catch (e) {
    box.innerHTML = '<p style="color:red">加载失败</p>';
  }
}

async function clearLog() {
  if (!confirm('确定要清空所有用药记录吗？')) return;
  try {
    await fetch(API + '/api/log', { method: 'DELETE' });
    showToast('✅ 记录已清空');
    loadLog();
  } catch (e) {
    showToast('❌ 操作失败');
  }
}

/* ---- WiFi ---- */
async function saveWifi() {
  const ssid = document.getElementById('wifi-ssid').value.trim();
  const pass = document.getElementById('wifi-pass').value;
  if (!ssid) { showToast('请输入WiFi名称'); return; }
  try {
    const r = await fetch(API + '/api/wifi', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, password: pass }),
    });
    const d = await r.json();
    showToast(d.result === 'ok' ? '✅ 已保存，正在连接...' : '❌ 保存失败', 3000);
  } catch (e) {
    showToast('❌ 网络错误');
  }
}

/* ---- Init ---- */
window.addEventListener('load', () => {
  loadStatus();
  loadReminders();
  setInterval(loadStatus, 10000);
});