#pragma once
#include <Arduino.h>

// Single self-contained HTML page (§13.2): inline CSS/JS, no CDN
// dependencies (the device may have no internet path for the browser).
// Mobile-first, dark theme matching the device, large touch targets.
//
// PIN handling: the custom X-IdeaSpeak-PIN header (§13.3) can only be added
// via fetch()/XHR, not a plain browser navigation — so /setup's data is
// pulled into this same page via JS rather than being a separately
// navigable URL, which also satisfies "GET /setup ... PIN" from a single
// page the user never has to re-navigate.

const char PAGE_DASHBOARD_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>IdeaSpeak</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 16px; background: #000; color: #fff;
    font-family: -apple-system, system-ui, sans-serif;
  }
  .card { background: #111; border-radius: 12px; padding: 16px; margin-bottom: 12px; }
  .art { width: 100%; max-width: 280px; aspect-ratio: 1; border-radius: 8px;
         display: block; margin: 0 auto 12px; background: #222; object-fit: cover; }
  h1 { font-size: 1.1rem; margin: 0 0 4px; }
  .track { font-size: 1.3rem; font-weight: 700; margin: 4px 0; }
  .artist { color: #bbb; margin: 2px 0; }
  .album { color: #777; font-size: 0.9rem; margin: 2px 0 12px; }
  .progress-row { display: flex; align-items: center; gap: 8px; font-size: 0.8rem; color: #999; }
  .bar-track { flex: 1; height: 6px; background: #2965 ; background:#294; border-radius: 3px; position: relative; overflow: hidden; }
  .bar-fill { height: 100%; background: #1DB954; width: 0%; }
  .controls { display: flex; justify-content: center; gap: 12px; margin: 16px 0; }
  button.ctl { background: #222; border: none; color: #fff; border-radius: 50%;
               width: 56px; height: 56px; font-size: 1.4rem; }
  button.ctl.primary { width: 68px; height: 68px; background: #1DB954; color: #000; }
  .toggles { display: flex; justify-content: center; gap: 24px; margin-bottom: 8px; }
  .toggles button { background: none; border: 1px solid #444; color: #999; border-radius: 8px;
                     padding: 8px 16px; font-size: 0.9rem; }
  .toggles button.active { border-color: #1DB954; color: #1DB954; }
  .device-row { text-align: center; color: #666; font-size: 0.85rem; margin-top: 8px; }
  input[type=range] { width: 100%; }
  details summary { cursor: pointer; color: #888; padding: 8px 0; }
  .row { display: flex; justify-content: space-between; padding: 6px 0; font-size: 0.85rem; color: #aaa; }
  .row b { color: #eee; }
  .pin-overlay { position: fixed; inset: 0; background: #000d; display: flex;
                 align-items: center; justify-content: center; }
  .pin-box { background: #111; padding: 24px; border-radius: 12px; text-align: center; }
  .pin-box input { font-size: 1.5rem; text-align: center; letter-spacing: 4px;
                    width: 8em; padding: 8px; border-radius: 8px; border: none; }
  .pin-box button { margin-top: 12px; padding: 10px 20px; border-radius: 8px; border: none;
                     background: #1DB954; color: #000; font-weight: 700; }
  .danger { background: #c0392b !important; color: #fff !important; }
  .msg { font-size: 0.85rem; color: #e67e22; min-height: 1.2em; }
</style>
</head>
<body>

<div id="pinOverlay" class="pin-overlay" style="display:none;">
  <div class="pin-box">
    <div>Enter device PIN</div>
    <div><input id="pinInput" inputmode="numeric" maxlength="6" placeholder="000000"></div>
    <button onclick="savePin()">Unlock</button>
  </div>
</div>

<div class="card">
  <img id="art" class="art" src="" alt="">
  <div class="track" id="track">—</div>
  <div class="artist" id="artist"></div>
  <div class="album" id="album"></div>
  <div class="progress-row">
    <span id="elapsed">0:00</span>
    <div class="bar-track" onclick="seekClick(event)"><div class="bar-fill" id="barFill"></div></div>
    <span id="remaining">0:00</span>
  </div>
  <div class="controls">
    <button class="ctl" onclick="ctl('previous')">&#9198;</button>
    <button class="ctl primary" id="playBtn" onclick="togglePlay()">&#9654;</button>
    <button class="ctl" onclick="ctl('next')">&#9197;</button>
  </div>
  <div class="toggles">
    <button id="shuffleBtn" onclick="toggleShuffle()">Shuffle</button>
    <button id="repeatBtn" onclick="cycleRepeat()">Repeat: off</button>
  </div>
  <input type="range" id="volume" min="0" max="100" value="50" onchange="setVolume(this.value)">
  <div class="device-row" id="device">No active device</div>
  <div class="msg" id="msg"></div>
</div>

<details class="card">
  <summary>Setup</summary>
  <div class="row"><span>WiFi</span><b id="setupWifi">-</b></div>
  <div class="row"><span>Spotify auth</span><b id="setupAuth">-</b></div>
  <div class="row"><span>Client ID</span><b id="setupClientId">-</b></div>
  <p style="font-size:0.8rem;color:#888">From your app at
  developer.spotify.com/dashboard — Redirect URI must be exactly
  http://127.0.0.1:8888/callback.</p>
  <input id="clientIdInput" type="text" placeholder="Spotify Client ID" style="width:100%">
  <button onclick="saveClientId()">Save Client ID</button>
  <button onclick="startAuth()">Start Spotify login</button>
  <p style="font-size:0.8rem;color:#888">After approving, your browser will show
  "can't connect" at 127.0.0.1 — that's expected. Copy the full address bar URL
  and paste it below.</p>
  <textarea id="authPaste" rows="3" style="width:100%" placeholder="Paste redirect URL or code=... here"></textarea>
  <button onclick="completeAuth()">Complete login</button>
</details>

<details class="card">
  <summary>Settings</summary>
  <div class="row"><span>Poll interval while playing (s)</span>
    <input id="pollPlay" type="number" min="3" max="60" style="width:4em"></div>
  <div class="row"><span>Poll interval while paused (s)</span>
    <input id="pollPause" type="number" min="3" max="60" style="width:4em"></div>
  <div class="row"><span>Brightness</span>
    <select id="brightness"><option value="0">15%</option><option value="1">40%</option>
      <option value="2">70%</option><option value="3">100%</option></select></div>
  <div class="row"><span>Album art</span>
    <input id="artOn" type="checkbox"></div>
  <div class="row"><span>Screen layout</span>
    <select id="orientation"><option value="0">Portrait</option><option value="1">Landscape</option></select></div>
  <p style="font-size:0.8rem;color:#888">Changing screen layout reboots the device (~15s).</p>
  <button onclick="saveSettings()">Save</button>
</details>

<details class="card">
  <summary>Diagnostics</summary>
  <div class="row"><span>Free heap</span><b id="diagHeap">-</b></div>
  <div class="row"><span>Wifi RSSI</span><b id="diagRssi">-</b></div>
  <div class="row"><span>Uptime</span><b id="diagUptime">-</b></div>
  <div class="row"><span>Requests (last min)</span><b id="diagReqs">-</b></div>
  <div class="row"><span>Token expires in</span><b id="diagToken">-</b></div>
  <button class="danger" onclick="factoryReset()">Factory reset</button>
</details>

<script>
let pin = localStorage.getItem('ideaspeak_pin') || '';

function ensurePin() {
  if (!pin) document.getElementById('pinOverlay').style.display = 'flex';
}
function savePin() {
  pin = document.getElementById('pinInput').value.trim();
  localStorage.setItem('ideaspeak_pin', pin);
  document.getElementById('pinOverlay').style.display = 'none';
}

function authHeaders(json) {
  const h = { 'X-IdeaSpeak-PIN': pin };
  if (json) h['Content-Type'] = 'application/json';
  return h;
}

function showMsg(t) {
  document.getElementById('msg').textContent = t;
  setTimeout(() => { document.getElementById('msg').textContent = ''; }, 4000);
}

function fmtTime(ms) {
  const s = Math.floor(ms / 1000);
  return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
}

let lastState = null;

async function pollNow() {
  try {
    const r = await fetch('/api/now');
    const s = await r.json();
    lastState = s;
    render(s);
  } catch (e) { /* transient — next poll will retry */ }
}

function render(s) {
  document.getElementById('track').textContent = s.valid ? s.track : 'Nothing playing';
  document.getElementById('artist').textContent = s.valid ? s.artist : '';
  document.getElementById('album').textContent = s.valid ? s.album : '';
  document.getElementById('device').textContent = s.valid ? (s.device || 'No active device') : 'No active device';
  document.getElementById('playBtn').innerHTML = s.is_playing ? '&#10074;&#10074;' : '&#9654;';
  document.getElementById('shuffleBtn').className = s.shuffle ? 'active' : '';
  const repeatLabels = ['off', 'context', 'track'];
  document.getElementById('repeatBtn').textContent = 'Repeat: ' + (repeatLabels[s.repeat] || 'off');
  document.getElementById('volume').value = s.volume || 0;

  if (s.art_url) document.getElementById('art').src = s.art_url;

  const frac = s.duration_ms > 0 ? Math.min(1, s.progress_ms / s.duration_ms) : 0;
  document.getElementById('barFill').style.width = (frac * 100) + '%';
  document.getElementById('elapsed').textContent = fmtTime(s.progress_ms || 0);
  document.getElementById('remaining').textContent = fmtTime(Math.max(0, (s.duration_ms || 0) - (s.progress_ms || 0)));
}

async function ctl(action, value) {
  ensurePin();
  try {
    const r = await fetch('/api/control', {
      method: 'POST', headers: authHeaders(true),
      body: JSON.stringify({ action, value: value ?? 0 })
    });
    if (r.status === 404) showMsg('Start playback on a device first');
    else if (r.status === 403) showMsg('Not permitted on this account');
    else if (r.status === 401) showMsg('Wrong PIN');
    // 204/Ok: optimistic state already applied on-device; next poll confirms it
  } catch (e) { showMsg('Network error'); }
}
function togglePlay() { ctl(lastState && lastState.is_playing ? 'pause' : 'play'); }
function toggleShuffle() { ctl('shuffle', lastState && !lastState.shuffle ? 1 : 0); }
function cycleRepeat() { ctl('repeat', ((lastState ? lastState.repeat : 0) + 1) % 3); }
function setVolume(v) { ctl('volume', parseInt(v, 10)); }
function seekClick(e) {
  if (!lastState || !lastState.duration_ms) return;
  const rect = e.currentTarget.getBoundingClientRect();
  const frac = (e.clientX - rect.left) / rect.width;
  ctl('seek', Math.round(frac * lastState.duration_ms));
}

async function loadSetup() {
  try {
    const r = await fetch('/setup', { headers: authHeaders(false) });
    const s = await r.json();
    document.getElementById('setupWifi').textContent = s.wifiConnected ? 'Connected' : 'Not connected';
    document.getElementById('setupAuth').textContent = s.authNeeded ? 'Not authorised' : 'Authorised';
    document.getElementById('setupClientId').textContent = s.hasClientId ? 'Saved' : 'Not set';
    document.getElementById('orientation').value = s.orientation;
    // Populate the Settings form with the device's actual current values —
    // without this, saving any one setting (e.g. orientation) silently
    // resets every other field to its HTML default (art_on unchecked,
    // poll intervals blank), which is exactly what disabled album art
    // during testing.
    document.getElementById('pollPlay').value = s.poll_play;
    document.getElementById('pollPause').value = s.poll_pause;
    document.getElementById('brightness').value = s.brightness;
    document.getElementById('artOn').checked = s.art_on;
  } catch (e) {}
}

async function saveClientId() {
  const clientId = document.getElementById('clientIdInput').value.trim();
  if (!clientId) { showMsg('Enter a Client ID first'); return; }
  const r = await fetch('/api/settings', {
    method: 'POST', headers: authHeaders(true),
    body: JSON.stringify({ client_id: clientId })
  });
  if (r.status === 401) { showMsg('Wrong PIN'); return; }
  showMsg('Client ID saved');
  loadSetup();
}

async function startAuth() {
  const r = await fetch('/auth/start', { headers: authHeaders(false) });
  if (r.status === 400) { showMsg('Save a Client ID first'); return; }
  if (!r.ok) { showMsg('Enter your PIN first'); return; }
  const s = await r.json();
  window.open(s.url, '_blank');
}
async function completeAuth() {
  const val = document.getElementById('authPaste').value;
  const r = await fetch('/auth/complete', {
    method: 'POST', headers: authHeaders(true), body: JSON.stringify({ value: val })
  });
  const s = await r.json();
  showMsg('Auth: ' + s.result);
  loadSetup();
}

async function loadSettings() {
  try {
    const r = await fetch('/healthz');
    const s = await r.json();
    document.getElementById('diagHeap').textContent = s.free_heap + ' B';
    document.getElementById('diagRssi').textContent = s.rssi + ' dBm';
    document.getElementById('diagUptime').textContent = Math.floor(s.uptime_ms / 1000) + ' s';
    document.getElementById('diagReqs').textContent = s.requests_last_minute;
    document.getElementById('diagToken').textContent = s.token_expires_in_s + ' s';
  } catch (e) {}
}
async function saveSettings() {
  const body = {
    poll_play: parseInt(document.getElementById('pollPlay').value, 10),
    poll_pause: parseInt(document.getElementById('pollPause').value, 10),
    brightness: parseInt(document.getElementById('brightness').value, 10),
    art_on: document.getElementById('artOn').checked,
    orientation: parseInt(document.getElementById('orientation').value, 10)
  };
  const r = await fetch('/api/settings', { method: 'POST', headers: authHeaders(true), body: JSON.stringify(body) });
  if (r.status === 401) { showMsg('Wrong PIN'); return; }
  try {
    const s = await r.json();
    if (s && s.rebooting) { showMsg('Layout changed — device is rebooting (~15s), then refresh this page'); return; }
  } catch (e) { /* 204 No Content on a normal save has no JSON body */ }
  showMsg('Settings saved');
}
async function factoryReset() {
  if (!confirm('Factory reset? This wipes WiFi and Spotify authorisation.')) return;
  await fetch('/api/reset', { method: 'POST', headers: authHeaders(false) });
  showMsg('Resetting...');
}

ensurePin();
pollNow();
loadSetup();
loadSettings();
setInterval(pollNow, 1000);
setInterval(loadSettings, 5000);
</script>
</body>
</html>
)HTMLPAGE";
