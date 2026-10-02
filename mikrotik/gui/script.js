// ZxheiFi - Frontend Logic

// Bump ZX_GUI_VERSION with every GUI change (and the same value in each
// .html page's stale-script check). REQUIRED_FIRMWARE = the firmware
// build these pages need (firmware/config.h FIRMWARE_VERSION).
const ZX_GUI_VERSION = '2.0.0-dev';
window.ZX_GUI_VERSION = ZX_GUI_VERSION;
const REQUIRED_FIRMWARE = '2.0.0-dev';
// Talks to the NodeMCU's JSON API (see firmware/gui_handler.h).

// In production this GUI is served BY the MikroTik router (see
// docs/06-gui-customize.md) - a completely different device from the
// NodeMCU that actually implements /api/*. A same-origin relative
// fetch('/api/...') would silently hit the ROUTER's own webserver
// instead (which has no such routes, since RouterOS's hotspot server
// doesn't know anything about this JSON API) rather than ever reaching
// the NodeMCU - the entire dynamic backend would be unreachable in a
// real deployment. Every /api/* call below goes through apiBase() so
// it targets the NodeMCU explicitly, UNLESS this page is already being
// served directly from it - dev testing straight off the chip, or
// tools/mock_server.py, which deliberately combines both roles on one
// origin for simplicity. The walled-garden POST to plain "/login" (see
// submitMikrotikHotspotLogin() below) is the one exception that must
// stay relative - that one is MikroTik's own endpoint, not the
// NodeMCU's, by design.
//
// Uses a name, not an IP: nodemcu.zxheifi.lan is a static DNS entry the
// desktop app's Configure MikroTik step adds on the router, pointing at
// the NodeMCU's reserved DHCP address. It used to be the mDNS name
// zxheifi-nodemcu.local, but the captive-portal mini-browsers on
// iPhones and most Androids don't resolve .local names at all - so the
// login page could never reach the NodeMCU on exactly the phones that
// matter. The walled garden lets not-yet-logged-in phones reach it.
const NODEMCU_HOST = 'nodemcu.zxheifi.lan'; // must match desktop-app/mikrotik_commands.py's NODEMCU_HOSTNAME
const NODEMCU_DIRECT_HOSTS = [NODEMCU_HOST, 'zxheifi-nodemcu.local', 'localhost', '127.0.0.1'];
function apiBase() {
  if (NODEMCU_DIRECT_HOSTS.includes(window.location.hostname)) return '';
  return 'http://' + NODEMCU_HOST;
}

// MikroTik fills in $(mac)/$(ip) on the <body> of login.html/status.html
// when it serves them. Anywhere else (mock server, opening the file
// directly) the placeholders stay literal - treat those as unknown.
function deviceInfo() {
  const d = document.body ? document.body.dataset : {};
  const clean = v => (v && !v.includes('$(') ? v : '');
  return { mac: clean(d.mac), ip: clean(d.ip) };
}

let currentMode = 'hotspot';
let statusPollTimer = null;

// ---- Sounds (login.html / status.html only) ---------------------------
// Off by default (AdminAPI.soundsEnabled, pulled from the public
// /api/branding response below) so a shop that doesn't want audio never
// hears any - the admin opts in from Settings. Every named sound is
// optional: mikrotik/gui/sounds/<name>.mp3 dropped in by the OPERATOR
// (see sounds/README.md) - a missing file just means that cue is
// silent, never an error. Background music can't autoplay on page load
// (browser policy); it starts on the first tap of the speaker icon,
// which doubles as the mute toggle.
const SoundManager = (() => {
  let enabled = false;
  let musicStarted = false;
  const cache = {};

  function clipFor(name) {
    if (!cache[name]) {
      const audio = new Audio(`sounds/${name}.mp3`);
      audio.preload = 'none';
      cache[name] = audio;
    }
    return cache[name];
  }

  function play(name) {
    if (!enabled) return;
    const audio = clipFor(name);
    audio.currentTime = 0;
    audio.play().catch(() => {}); // missing file / blocked autoplay - fail silently
  }

  function setEnabled(value) {
    enabled = value;
    const btn = document.getElementById('soundToggle');
    if (btn) {
      btn.textContent = enabled ? '🔊' : '🔇';
      btn.classList.toggle('on', enabled);
    }
    if (!enabled) {
      const music = cache['background-music'];
      if (music) music.pause();
    } else {
      startMusicIfNeeded();
    }
  }

  function startMusicIfNeeded() {
    if (!enabled || musicStarted) return;
    const music = clipFor('background-music');
    music.loop = true;
    music.volume = 0.5;
    music.play().then(() => { musicStarted = true; }).catch(() => {});
  }

  // A second looping track (coin-music while the Insert Coin window
  // counts down). Background music pauses under it and resumes after.
  let loopName = null;
  function startLoop(name) {
    if (!enabled || loopName === name) return;
    stopLoop();
    loopName = name;
    const bg = cache['background-music'];
    if (bg && musicStarted) bg.pause();
    const a = clipFor(name);
    a.loop = true;
    a.currentTime = 0;
    a.play().catch(() => {});
  }
  function stopLoop() {
    if (!loopName) return;
    const a = cache[loopName];
    if (a) a.pause();
    loopName = null;
    const bg = cache['background-music'];
    if (bg && musicStarted && enabled) bg.play().catch(() => {});
  }

  return { play, setEnabled, startMusicIfNeeded, startLoop, stopLoop, isEnabled: () => enabled };
})();

function toggleSound() {
  SoundManager.setEnabled(!SoundManager.isEnabled());
  if (SoundManager.isEnabled()) SoundManager.startMusicIfNeeded();
}

// ---- Coin slot (login.html "Insert Coin", status.html "Add Time") -------
// See firmware/coin_slot.h. Tapping reserves the physical coin slot for
// this phone; coins dropped then are credited to it, and Done (or the
// slot going idle) turns the credit into a code this page logs in with.
// The reservation token is kept in localStorage so a reload - or the
// NodeMCU finishing on its own after a timeout - still lands the
// customer on their paid session.
let coinToken = null;
let coinPollTimer = null;
let coinLastPesos = 0;

function ensureCoinModal() {
  let modal = document.getElementById('coinModal');
  if (modal) return modal;
  modal = document.createElement('div');
  modal.id = 'coinModal';
  modal.className = 'qr-modal coin-modal';
  modal.style.display = 'none';
  modal.innerHTML = `
    <div class="qr-content coin-content">
      <h2>Insert coins now</h2>
      <p class="coin-total" id="coinPesos">₱0</p>
      <p class="coin-minutes" id="coinMinutes">0 minutes</p>
      <p class="coin-note" id="coinNote"></p>
      <p class="coin-timer">Closes in <span id="coinTimer">--</span>s if no coin is added</p>
      <button type="button" class="btn btn-primary" id="coinDoneBtn" onclick="finishCoins()" disabled>Done - Connect</button>
      <button type="button" class="btn btn-secondary" id="coinCancelBtn" onclick="cancelCoins()">Cancel</button>
    </div>`;
  document.body.appendChild(modal);
  return modal;
}

function onStatusPage() { return !document.getElementById('hotspotForm'); }

// Anything shown back as HTML that came from a setting or a user.
function escHtml(s) {
  return String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

// ---- Coin box picker (v2: sub vendos) ----------------------------------
// Shown only when the shop has more than one coin box. The QR sticker on
// each box opens the portal with ?vendo=<id>; the choice is remembered on
// this phone, and offline boxes can't be picked.
function selectedVendo() {
  const sel = document.getElementById('vendoSelect');
  const box = document.getElementById('vendoPicker');
  if (!sel || !box || box.style.display === 'none' || !sel.value) return 0;
  return parseInt(sel.value, 10) || 0;
}

function initVendoPicker(vendos) {
  const box = document.getElementById('vendoPicker');
  const sel = document.getElementById('vendoSelect');
  if (!box || !sel) return;
  const list = Array.isArray(vendos) ? vendos : [];
  if (list.length < 2) {
    box.style.display = 'none';
    return;
  }
  const fromUrl = new URLSearchParams(location.search).get('vendo');
  let stored = null;
  try {
    if (fromUrl !== null) localStorage.setItem('zxheifi_vendo', fromUrl);
    stored = localStorage.getItem('zxheifi_vendo');
  } catch (e) { /* storage blocked - URL/first box still work */ }
  const want = fromUrl !== null ? fromUrl : stored;
  sel.innerHTML = list.map(v =>
    `<option value="${v.id}"${v.online ? '' : ' disabled'}>${escHtml(v.name)}${v.online ? '' : ' (offline)'}</option>`
  ).join('');
  const pick = list.find(v => String(v.id) === String(want) && v.online) || list.find(v => v.online) || list[0];
  sel.value = String(pick.id);
  box.style.display = '';
  // Scanned a box's sticker but that box is down: say so instead of
  // quietly switching to another box the customer isn't standing at.
  const note = document.getElementById('vendoNote');
  const wanted = list.find(v => String(v.id) === String(want));
  if (note) {
    note.textContent = wanted && !wanted.online
      ? `"${wanted.name}" is offline right now - use another coin box, or try again in a minute.` : '';
    note.style.display = note.textContent ? '' : 'none';
  }
}

function onVendoChange() {
  try { localStorage.setItem('zxheifi_vendo', String(selectedVendo())); } catch (e) { /* storage blocked */ }
}

async function onCoinInsertTap() {
  SoundManager.startMusicIfNeeded(); // a real tap - browser will actually allow audio to start now
  SoundManager.play('click');
  // From status.html the coins top up the running session instead of
  // creating a new one.
  const session = onStatusPage() ? (localStorage.getItem('zxheifi_session') || '') : '';
  try {
    const res = await fetch(apiBase() + '/api/coin/start', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ mac: deviceInfo().mac, session, vendo: selectedVendo(),
                             token: localStorage.getItem('zxheifi_coin_token') || '' }),
    });
    const data = await res.json();
    if (res.status === 409 && data.error === 'coin_slot_busy') {
      alert(`Someone else is using this coin box right now. Please try again in ${data.waitSec} seconds.`);
      return;
    }
    if (res.status === 409 && data.error === 'vendo_offline') {
      SoundManager.play('error');
      alert('That coin box is offline right now - please choose another one.');
      applyBranding();
      return;
    }
    if (res.status === 404 && data.error === 'vendo_unknown') {
      SoundManager.play('error');
      alert('That coin box no longer exists - please choose another one.');
      try { localStorage.removeItem('zxheifi_vendo'); } catch (e) { /* storage blocked */ }
      applyBranding();
      return;
    }
    if (!res.ok) throw new Error(data.error || 'coin_start_failed');
    coinToken = data.token;
    localStorage.setItem('zxheifi_coin_token', coinToken);
    coinLastPesos = 0;
    ensureCoinModal().style.display = 'flex';
    SoundManager.play('insert-coin');        // "please insert coins" prompt
    SoundManager.startLoop('coin-music');    // loops while the window counts down
    pollCoin();
  } catch (err) {
    SoundManager.play('error');
    alert('The coin slot is not available: ' + err.message);
  }
}

async function pollCoin() {
  clearTimeout(coinPollTimer);
  if (!coinToken) return;
  try {
    const res = await fetch(apiBase() + '/api/coin/status?token=' + encodeURIComponent(coinToken));
    const d = await res.json();
    if (d.state === 'done') { completeCoinLogin(d.code, d.extended); return; }
    if (d.state !== 'inserting') { closeCoinModal(); return; }

    ensureCoinModal().style.display = 'flex';
    if (d.pesos > coinLastPesos) SoundManager.play('coin-drop');
    coinLastPesos = d.pesos;
    document.getElementById('coinPesos').textContent = '₱' + d.pesos;
    document.getElementById('coinMinutes').textContent = d.minutes + ' minutes' +
      (d.pesos ? (d.dataMb ? ` / ${d.dataMb} MB` : ' / unlimited data') : '');
    document.getElementById('coinNote').textContent = d.unmatchedPesos
      ? `₱${d.unmatchedPesos} was not recognized - please tell the shop owner.` : '';
    document.getElementById('coinTimer').textContent = d.secondsLeft;
    document.getElementById('coinDoneBtn').disabled = !d.pesos;
    // Paid credit can't be thrown away - Cancel only makes sense before the first coin.
    document.getElementById('coinCancelBtn').style.display = d.pesos ? 'none' : '';
  } catch (err) { /* keep polling - brief WiFi hiccups are normal on a phone */ }
  coinPollTimer = setTimeout(pollCoin, 1000);
}

async function finishCoins() {
  SoundManager.play('click');
  const btn = document.getElementById('coinDoneBtn');
  btn.disabled = true;
  btn.textContent = 'Connecting...';
  try {
    const res = await fetch(apiBase() + '/api/coin/done', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ token: coinToken }),
    });
    const data = await res.json();
    if (!res.ok) {
      if (data.error === 'no_coins_inserted' || data.error === 'no_coin_session') { closeCoinModal(); return; }
      throw new Error(data.error || 'coin_done_failed');
    }
    completeCoinLogin(data.sessionId, data.extended);
  } catch (err) {
    SoundManager.play('error');
    alert('Could not connect yet: ' + err.message + '\nYour coins are saved - tap Done again in a moment.');
    btn.disabled = false;
    btn.textContent = 'Done - Connect';
  }
}

async function cancelCoins() {
  SoundManager.play('click');
  try {
    await fetch(apiBase() + '/api/coin/cancel', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ token: coinToken }),
    });
  } catch (err) { /* the slot frees itself after its timeout anyway */ }
  closeCoinModal();
}

function closeCoinModal() {
  clearTimeout(coinPollTimer);
  SoundManager.stopLoop();
  coinToken = null;
  localStorage.removeItem('zxheifi_coin_token');
  const modal = document.getElementById('coinModal');
  if (modal) modal.style.display = 'none';
}

function completeCoinLogin(code, extended) {
  closeCoinModal();
  SoundManager.play('success');
  if (extended) {
    if (onStatusPage()) refreshSessionStatus();
    else window.location.href = 'status.html';
    return;
  }
  localStorage.setItem('zxheifi_session', code);
  localStorage.setItem('zxheifi_mode', 'hotspot');
  localStorage.setItem('zxheifi_password', '');
  submitMikrotikHotspotLogin(code, code);
}

// Picks an unfinished coin session back up after a page reload.
function resumeCoinSession() {
  coinToken = localStorage.getItem('zxheifi_coin_token');
  if (coinToken) pollCoin();
}

// ---- Shared status indicator (login + status + admin pages) --------------

async function updateStatus() {
  const indicator = document.getElementById('statusIndicator') || document.getElementById('adminStatusIndicator');
  const text = document.getElementById('statusText') || document.getElementById('adminStatusText');
  if (!indicator && !text) return;

  try {
    const res = await fetch(apiBase() + '/api/health');
    const data = await res.json();
    // --peso/--alert are fixed semantic colors (healthy/unhealthy),
    // deliberately NOT the rebrandable --accent - a shop's brand color
    // shouldn't determine whether "online" reads as good or bad.
    if (indicator) indicator.style.background = data.mikrotik ? 'var(--peso)' : 'var(--alert)';
    if (text) text.textContent = data.mikrotik ? 'Ready' : 'Connecting...';
  } catch (err) {
    if (indicator) indicator.style.background = 'var(--alert)';
    if (text) text.textContent = 'Offline';
  }
}

function startPeriodicUpdates() {
  updateStatus();
  setInterval(updateStatus, 30000);
}

// ---- No-code branding (business name + accent color) ---------------------

function darkenHex(hex, percent) {
  const num = parseInt(hex.replace('#', ''), 16);
  if (Number.isNaN(num)) return hex;
  const amt = Math.round(2.55 * percent);
  const r = Math.max(0, (num >> 16) - amt);
  const g = Math.max(0, ((num >> 8) & 0x00FF) - amt);
  const b = Math.max(0, (num & 0x0000FF) - amt);
  return '#' + (0x1000000 + r * 0x10000 + g * 0x100 + b).toString(16).slice(1);
}

async function applyBranding() {
  try {
    const res = await fetch(apiBase() + '/api/branding');
    const data = await res.json();
    if (data.brandColor) {
      document.documentElement.style.setProperty('--accent', data.brandColor);
      document.documentElement.style.setProperty('--accent-dark', darkenHex(data.brandColor, 10));
    }
    if (data.brandName) {
      const titleEl = document.getElementById('brandTitle');
      if (titleEl) titleEl.textContent = data.brandName;
      document.title = document.title.replace('ZxheiFi', data.brandName);
    }
    showAnnouncement(data.announcement);
    showRates(data.rates);
    initVendoPicker(data.vendos);
    // Master switch: an operator with no sound files (or who just
    // doesn't want audio) never sees the feature at all - only shown
    // once Settings > Enable Sounds is on.
    const soundBtn = document.getElementById('soundToggle');
    if (soundBtn) {
      if (data.soundEnabled) {
        soundBtn.style.display = '';
        SoundManager.setEnabled(true);
      } else {
        soundBtn.style.display = 'none';
      }
    }
  } catch (err) {
    console.error('Branding load failed', err);
  }
}

// Minutes as the portal shows them: 1440 -> "01d:00H:00M".
function formatDHM(totalMinutes) {
  const m = Math.max(0, Math.round(totalMinutes || 0));
  const pad = n => String(n).padStart(2, '0');
  return `${pad(Math.floor(m / 1440))}d:${pad(Math.floor((m % 1440) / 60))}H:${pad(m % 60)}M`;
}

// Settings > Announcement, on top of login.html/status.html.
function showAnnouncement(text) {
  const box = document.getElementById('announcement');
  if (!box) return;
  const t = (text || '').trim();
  box.textContent = t;
  box.style.display = t ? 'block' : 'none';
}

// Settings > Rate Profiles as a price list at the bottom of login.html.
function showRates(rates) {
  const box = document.getElementById('ratesBox');
  const body = document.getElementById('ratesBody');
  if (!box || !body) return;
  const list = (rates || []).slice().sort((a, b) => a.peso - b.peso);
  body.innerHTML = '';
  list.forEach(r => {
    const tr = document.createElement('tr');
    [`₱${r.peso}`, formatDHM(r.minutes), r.validityMinutes ? formatDHM(r.validityMinutes) : 'No expiry']
      .forEach(text => { const td = document.createElement('td'); td.textContent = text; tr.appendChild(td); });
    body.appendChild(tr);
  });
  box.style.display = list.length ? 'block' : 'none';
}

// ---- login.html ------------------------------------------------------

function setModeActive(mode) {
  document.querySelectorAll('.mode-btn').forEach(btn => btn.classList.remove('active'));
  const btn = document.querySelector(`.mode-btn[data-mode="${mode}"]`);
  if (btn) btn.classList.add('active');
}

function swapForms() {
  document.querySelectorAll('.login-form').forEach(form => { form.style.display = 'none'; });
  const form = document.getElementById(currentMode === 'hotspot' ? 'hotspotForm' : 'pppoeForm');
  if (form) form.style.display = 'flex';
}

function setMode(mode) {
  SoundManager.play('click');
  currentMode = mode;
  setModeActive(mode);
  swapForms();
}

async function submitLogin(event) {
  event.preventDefault();
  SoundManager.play('click');
  const form = event.target;
  const code = form.querySelector('input[name="username"]').value.trim();
  // Only subscriber accounts use this - voucher codes don't have
  // passwords, so an empty field here is the normal case.
  const passwordInput = form.querySelector('input[name="password"]');
  const password = passwordInput ? passwordInput.value : '';
  if (!code) return;

  const submitBtn = form.querySelector('button[type="submit"]');
  if (submitBtn) { submitBtn.disabled = true; submitBtn.textContent = 'Connecting...'; }

  try {
    const res = await fetch(apiBase() + '/api/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ code, password, mode: currentMode, mac: deviceInfo().mac }),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'login_failed');

    localStorage.setItem('zxheifi_session', data.sessionId);
    localStorage.setItem('zxheifi_mode', data.mode);
    // Cached so Resume can redo the walled-garden login below without
    // asking the customer to type their password again - vouchers store
    // '' here and fall back to the code itself, same as at login.
    localStorage.setItem('zxheifi_password', password);

    if (data.mode === 'hotspot') {
      // /api/login only registered the account with MikroTik (via the
      // RouterOS API, NodeMCU-to-router) - it does NOT by itself release
      // *this* browser from the hotspot's walled garden. That only
      // happens once this specific client actually authenticates
      // against MikroTik's own /login handler, so do that now instead
      // of navigating straight to status.html.
      submitMikrotikHotspotLogin(code, password || code);
      return; // the form submit below navigates the browser away
    }
    // PPPoE doesn't need a walled-garden release - the customer's
    // PPPoE dialer (outside this browser) makes its own connection
    // using this same code/password.
    window.location.href = 'status.html';
  } catch (err) {
    SoundManager.play('error');
    alert('Connection failed: ' + err.message + '\nCheck your voucher code and try again.');
  } finally {
    if (submitBtn) { submitBtn.disabled = false; submitBtn.textContent = 'Connect'; }
  }
}

// Actually authenticates THIS browser session against MikroTik's own
// hotspot walled garden. Creating the /ip/hotspot/user record (done via
// /api/login above) only registers the account - RouterOS still needs
// this specific client to submit credentials to its own login handler
// before it releases that client's traffic. Uses a real (non-AJAX) form
// POST because RouterOS's hotspot login responds with a redirect meant
// for full page navigation, not something to read via fetch(). `dst`
// tells RouterOS where to send the browser after a successful login,
// instead of its own default "logged in" page.
function submitMikrotikHotspotLogin(username, password) {
  const form = document.createElement('form');
  form.method = 'POST';
  form.action = '/login';
  form.style.display = 'none';
  const dst = new URL('status.html', window.location.href).href;
  const fields = { username, password, dst };
  for (const name in fields) {
    const input = document.createElement('input');
    input.type = 'hidden';
    input.name = name;
    input.value = fields[name];
    form.appendChild(input);
  }
  document.body.appendChild(form);
  form.submit();
}

// QR scanning uses the browser's native BarcodeDetector where available
// (recent Chrome/Android) instead of shipping a hand-rolled decoder that
// can't be verified against real hardware in this environment.
async function scanQR() {
  SoundManager.play('click');
  if (!('BarcodeDetector' in window)) {
    alert('QR scanning is not supported on this browser. Please type the voucher code instead.');
    return;
  }

  let stream;
  try {
    stream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: 'environment' } });
  } catch (err) {
    alert('Camera access denied or unavailable.');
    return;
  }

  const video = document.createElement('video');
  video.srcObject = stream;
  video.setAttribute('playsinline', true);
  await video.play();

  const detector = new BarcodeDetector({ formats: ['qr_code'] });
  const stop = () => stream.getTracks().forEach(t => t.stop());

  const activeForm = document.getElementById(currentMode === 'hotspot' ? 'hotspotForm' : 'pppoeForm');
  const codeInput = activeForm ? activeForm.querySelector('input[name="username"]') : null;

  const started = Date.now();
  const timeoutMs = 20000;
  (async function tick() {
    if (Date.now() - started > timeoutMs) { stop(); alert('QR scan timed out.'); return; }
    try {
      const codes = await detector.detect(video);
      if (codes.length) {
        stop();
        if (codeInput) codeInput.value = codes[0].rawValue;
        return;
      }
    } catch (err) { /* keep trying until timeout */ }
    requestAnimationFrame(tick);
  })();
}

function initLoginPage() {
  setMode('hotspot');
  document.querySelectorAll('.login-form').forEach(form => form.addEventListener('submit', submitLogin));
  resumeCoinSession();
}

// ---- status.html -------------------------------------------------------

// Mirrors firmware/config.h's UNLIMITED_SECONDS/UNLIMITED_BYTES sentinels
// (time-only/data-only tier modes, and subscriptions) - shown as
// "Unlimited" instead of a meaningless huge countdown.
const UNLIMITED_THRESHOLD_SEC = 4000000000;
const UNLIMITED_THRESHOLD_BYTES = 1e18;

function formatTime(totalSeconds) {
  if (totalSeconds >= UNLIMITED_THRESHOLD_SEC) return 'Unlimited';
  const h = Math.floor(totalSeconds / 3600);
  const m = Math.floor((totalSeconds % 3600) / 60);
  const s = Math.floor(totalSeconds % 60);
  return [h, m, s].map(n => String(n).padStart(2, '0')).join(':');
}

function formatData(bytes) {
  if (bytes >= UNLIMITED_THRESHOLD_BYTES) return 'Unlimited';
  const gb = bytes / (1024 ** 3);
  if (gb >= 1) return gb.toFixed(2) + ' GB';
  return (bytes / (1024 ** 2)).toFixed(0) + ' MB';
}

let sessionIsPaused = false;
let sessionIsSubscriber = false;
// Only the FIRST successful load of this page should chime - it polls
// every 10s afterward, and a repeated "success" cue on routine refreshes
// would be noise rather than the "you're connected" confirmation it's
// meant to be.
let statusSuccessSoundPlayed = false;

// Loop breaker. MikroTik sends a logged-in customer from login.html back
// to status.html, so status.html sending them to login.html (because THIS
// browser had no saved session - e.g. logged in through the phone's
// sign-in popup, then opened in Chrome) made the page flash back and
// forth forever. Refuses a 3rd redirect within 15s and explains instead.
function safeRedirect(url, reason) {
  let hops = [];
  try { hops = JSON.parse(sessionStorage.getItem('zxheifi_hops') || '[]'); } catch (e) { hops = []; }
  const now = Date.now();
  hops = hops.filter(t => now - t < 15000);
  if (hops.length >= 2) {
    showStatusNotice('Hindi ma-load ang page nang tama (paulit-ulit na lumilipat). ' + (reason || '') +
      ' Isara ang page na ito at buksan ulit ang WiFi login.', true);
    return;
  }
  hops.push(now);
  sessionStorage.setItem('zxheifi_hops', JSON.stringify(hops));
  window.location.href = url;
}

function showStatusNotice(message, withLogout) {
  const box = document.getElementById('statusNotice');
  if (!box) return;
  box.style.display = 'block';
  box.innerHTML = '';
  const p = document.createElement('p');
  p.textContent = message;
  box.appendChild(p);
  if (withLogout) {
    const a = document.createElement('a');
    a.href = '/logout?erase-cookie=true';
    a.className = 'btn btn-secondary';
    a.textContent = 'Log out of the WiFi';
    box.appendChild(a);
  }
}

// The hotspot username MikroTik filled into status.html ($(username)) -
// the same code the session was created with. '' when unknown.
function hotspotUsername() {
  const u = document.body ? document.body.dataset.username : '';
  return u && !u.includes('$(') ? u : '';
}

async function refreshSessionStatus() {
  // MikroTik knows who is logged in even when this browser never saw the
  // login (different browser/popup, cleared storage) - trust it first.
  const hsUser = hotspotUsername();
  if (hsUser && localStorage.getItem('zxheifi_session') !== hsUser) {
    localStorage.setItem('zxheifi_session', hsUser);
    localStorage.setItem('zxheifi_mode', 'hotspot');
  }
  const sessionId = localStorage.getItem('zxheifi_session');
  if (!sessionId) { safeRedirect('login.html', 'Walang session na makita.'); return; }

  try {
    const res = await fetch(apiBase() + '/api/status?session=' + encodeURIComponent(sessionId));
    if (res.status === 404) {
      if (hsUser) {
        // Still logged in on MikroTik, but the NodeMCU has no such session
        // (e.g. it restarted). Redirecting would just loop - explain.
        showStatusNotice(`Naka-login ka pa sa WiFi (${hsUser}), pero hindi makita ng system ang oras mo. ` +
          'Kung tapos na ang oras mo, mag-log out at mag-login ulit.', true);
        return;
      }
      localStorage.removeItem('zxheifi_session');
      localStorage.removeItem('zxheifi_password');
      safeRedirect('login.html', 'Tapos na ang session.');
      return;
    }
    const notice = document.getElementById('statusNotice');
    if (notice) notice.style.display = 'none';
    sessionStorage.removeItem('zxheifi_hops'); // a working page resets the loop counter
    const data = await res.json();

    document.getElementById('connectionMode').textContent = data.mode === 'pppoe' ? 'PPPoE' : 'Hotspot';
    document.getElementById('timeRemaining').textContent = formatTime(data.timeRemainingSec);
    document.getElementById('dataRemaining').textContent = formatData(data.dataRemainingBytes);
    document.getElementById('sessionId').textContent = data.sessionId;

    const sig = document.getElementById('signalStrength');
    if (sig) sig.textContent = 'N/A';

    sessionIsPaused = !!data.paused;
    sessionIsSubscriber = !!data.isSubscriber;
    const pauseBtn = document.getElementById('pauseBtn');
    if (pauseBtn) pauseBtn.textContent = sessionIsPaused ? 'Resume' : 'Pause';
    const extendBtn = document.getElementById('extendBtn');
    if (extendBtn) extendBtn.style.display = sessionIsSubscriber ? 'none' : '';
    const coinTopUpBtn = document.getElementById('coinTopUpBtn');
    if (coinTopUpBtn) coinTopUpBtn.style.display = (sessionIsSubscriber || data.mode !== 'hotspot') ? 'none' : '';

    if (!statusSuccessSoundPlayed) {
      statusSuccessSoundPlayed = true;
      SoundManager.play('success');
    }
  } catch (err) {
    console.error('Status refresh failed', err);
  }
}

async function togglePause() {
  SoundManager.play('click');
  const sessionId = localStorage.getItem('zxheifi_session');
  const wasPaused = sessionIsPaused;
  const endpoint = wasPaused ? '/api/resume' : '/api/pause';
  // Pause fully deprovisions the MikroTik account (see gui_handler.h),
  // so Resume needs to recreate it with the same password Login used -
  // vouchers cached '' here and fall back to the session code, same as
  // at login time.
  const body = wasPaused
    ? { session: sessionId, password: localStorage.getItem('zxheifi_password') || '' }
    : { session: sessionId };
  try {
    const res = await fetch(apiBase() + endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'pause_failed');

    if (wasPaused && localStorage.getItem('zxheifi_mode') === 'hotspot') {
      // Resume also fully re-provisioned the hotspot account, which
      // means this browser was kicked from the walled garden while
      // paused and needs to re-authenticate against it, same as a
      // fresh login - see submitMikrotikHotspotLogin().
      const password = localStorage.getItem('zxheifi_password') || '';
      submitMikrotikHotspotLogin(sessionId, password || sessionId);
      return; // navigates away
    }
    refreshSessionStatus();
  } catch (err) {
    alert('Could not ' + (wasPaused ? 'resume' : 'pause') + ': ' + err.message);
  }
}

async function extendSession() {
  SoundManager.play('click');
  const sessionId = localStorage.getItem('zxheifi_session');
  const code = prompt('Enter a new voucher code to add more time/data:');
  if (!code) return;

  try {
    const res = await fetch(apiBase() + '/api/extend', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ session: sessionId, code: code.trim() }),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'extend_failed');
    alert('Session extended.');
    refreshSessionStatus();
  } catch (err) {
    alert('Could not extend session: ' + err.message);
  }
}

async function disconnect() {
  SoundManager.play('click');
  const sessionId = localStorage.getItem('zxheifi_session');
  if (!sessionId) return;
  if (!confirm('Disconnect and end this session?')) return;

  try {
    await fetch(apiBase() + '/api/disconnect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ session: sessionId }),
    });
  } finally {
    localStorage.removeItem('zxheifi_session');
    localStorage.removeItem('zxheifi_password');
    // Through MikroTik's own logout, so the hotspot also forgets this
    // device - otherwise it would send login.html straight back here.
    window.location.href = hotspotUsername() ? '/logout?erase-cookie=true' : 'login.html';
  }
}

function showQR() {
  SoundManager.play('click');
  const sessionId = localStorage.getItem('zxheifi_session') || '';
  const qrCode = document.getElementById('qrCode');
  const modal = document.getElementById('qrModal');
  if (!qrCode || !modal) return;

  // qrcode.min.js is the same vendored library (davidshimjs/qrcodejs)
  // the original JuanFi project ships and uses in production - not a
  // hand-rolled encoder, so it doesn't carry the same "can't verify
  // this is actually correct" risk this project had previously declined
  // to take on. Encodes the plain session code as text, matching what
  // login.html's scanQR() (BarcodeDetector) expects to read back.
  qrCode.innerHTML = ''; // clear any previous QR before drawing a new one
  if (window.QRCode && sessionId) {
    new QRCode(qrCode, {
      text: sessionId,
      width: 180,
      height: 180,
      colorDark: '#000000',
      colorLight: '#ffffff',
      correctLevel: QRCode.CorrectLevel.M,
    });
  } else {
    // qrcode.min.js failed to load, or there's no session code yet -
    // fall back to plain text rather than showing a blank panel.
    qrCode.innerHTML = `<div style="font-family:monospace;font-size:20px;font-weight:700;padding:24px 12px;">${sessionId}</div>`;
  }
  modal.style.display = 'flex';
}

function hideQR() {
  const modal = document.getElementById('qrModal');
  if (modal) modal.style.display = 'none';
}

function initStatusPage() {
  refreshSessionStatus();
  resumeCoinSession();
  statusPollTimer = setInterval(refreshSessionStatus, 10000);
}

// ---- admin.html ----------------------------------------------------------

// Super-only tabs are hidden client-side once we know the logged-in
// admin's role (see loadOverview()) - the server enforces this too via
// requireAdmin(requireSuper=true), this is just so staff don't see
// tabs that would just 403.
const SUPER_ONLY_TABS = ['subscriptions', 'admins', 'settings'];
let currentAdminRole = null;

// Admin login: one form (admin.html #adminLogin), checked against the
// NodeMCU BEFORE it's saved. It used to be two prompt() boxes in a row -
// "Admin username:" then "Admin password:" - that looked like the same
// question twice; typing the password into both failed, the failure
// wiped what was saved, and every tab click prompted twice again.
// Lets a NodeMCU with no internet (so no NTP) borrow this device's clock -
// see adoptBrowserClock() in firmware/gui_handler.h.
function clientTime() { return String(Math.floor(Date.now() / 1000)); }

function savedAdminCreds() {
  const username = sessionStorage.getItem('zxheifi_admin_user');
  const pass = sessionStorage.getItem('zxheifi_admin_pass');
  return username && pass ? { username, pass } : null;
}

function showAdminLogin(message) {
  const overlay = document.getElementById('adminLogin');
  if (!overlay) return;
  document.getElementById('adminLoginError').textContent = message || '';
  if (overlay.style.display !== 'flex') {
    overlay.style.display = 'flex';
    document.getElementById('adminPass').value = '';
    document.getElementById('adminPass').focus();
  }
}

function adminLogout() {
  sessionStorage.removeItem('zxheifi_admin_user');
  sessionStorage.removeItem('zxheifi_admin_pass');
  showAdminLogin('');
}

async function submitAdminLogin(event) {
  event.preventDefault();
  const username = document.getElementById('adminUser').value.trim();
  const pass = document.getElementById('adminPass').value;
  const btn = document.getElementById('adminLoginBtn');
  const error = document.getElementById('adminLoginError');
  btn.disabled = true;
  btn.textContent = 'Checking...';
  error.textContent = '';
  // Without a timeout a NodeMCU that is offline (e.g. not on the WiFi)
  // left the button on "Checking..." forever.
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 15000);
  try {
    const res = await fetch(apiBase() + '/api/admin/overview', {
      headers: { 'X-Admin-Username': username, 'X-Admin-Password': pass, 'X-Client-Time': clientTime() },
      signal: controller.signal,
    });
    if (res.ok) {
      sessionStorage.setItem('zxheifi_admin_user', username);
      sessionStorage.setItem('zxheifi_admin_pass', pass);
      document.getElementById('adminLogin').style.display = 'none';
      startAdminDashboard();
    } else if (res.status === 429) {
      error.textContent = 'Masyadong maraming maling subok. Maghintay ng 1 minuto, tapos subukan ulit.';
    } else if (res.status === 401) {
      error.textContent = 'Mali ang username o password.';
    } else {
      error.textContent = `May error sa NodeMCU (${res.status}). Subukan ulit.`;
    }
  } catch (err) {
    error.textContent = 'Hindi maabot ang NodeMCU (walang sagot). Naka-on ba ito at nakakonekta sa WiFi ng MikroTik? ' +
      'Tingnan sa desktop app: Flash Firmware > Start Serial Monitor - dapat may "WiFi connected".';
  } finally {
    clearTimeout(timer);
    btn.disabled = false;
    btn.textContent = 'Login';
  }
}

// Never prompts. Without a (working) login it shows the login form and
// the call just never completes - so the page doesn't pile an
// "unauthorized" alert on top of the login form for every request.
async function adminFetch(path, options = {}) {
  const creds = savedAdminCreds();
  if (!creds) {
    showAdminLogin('');
    return new Promise(() => {});
  }
  options.headers = Object.assign({}, options.headers, {
    'X-Admin-Username': creds.username, 'X-Admin-Password': creds.pass, 'X-Client-Time': clientTime(),
  });
  const res = await fetch(apiBase() + path, options);
  if (res.status === 401 || res.status === 429) {
    sessionStorage.removeItem('zxheifi_admin_user');
    sessionStorage.removeItem('zxheifi_admin_pass');
    showAdminLogin(res.status === 429
      ? 'Masyadong maraming maling subok. Maghintay ng 1 minuto.'
      : 'Mag-login ulit (napalitan ang password o nag-expire ang login).');
    return new Promise(() => {});
  }
  return res;
}

function applyRoleVisibility() {
  if (currentAdminRole !== 'staff') return;
  SUPER_ONLY_TABS.forEach(tabName => {
    const btn = document.querySelector(`.tab-btn[data-tab="${tabName}"]`);
    if (btn) btn.style.display = 'none';
  });
}

function openTab(event, tabName) {
  document.querySelectorAll('.tab-content').forEach(el => { el.style.display = 'none'; });
  document.querySelectorAll('.tab-btn').forEach(btn => btn.classList.remove('active'));
  document.getElementById(tabName).style.display = 'block';
  event.currentTarget.classList.add('active');

  if (tabName === 'overview') loadOverview();
  if (tabName === 'users') loadActiveUsers();
  if (tabName === 'subscriptions') loadSpeedLabels();
  if (tabName === 'sales') loadSales();
  if (tabName === 'vendos') { loadVendos(); loadCollections(); }
  if (tabName === 'logs') loadLogs();
  if (tabName === 'admins') loadAdminAccounts();
  if (tabName === 'settings') loadSettingsIntoForm();
}

async function loadOverview() {
  try {
    const res = await adminFetch('/api/admin/overview');
    const data = await res.json();
    if (!res.ok) return;
    document.getElementById('totalUsers').textContent = data.totalUsers;
    document.getElementById('activeSessions').textContent = data.activeSessions;
    document.getElementById('dataUsed').textContent = formatData(data.dataUsedTodayBytes);
    document.getElementById('revenueToday').textContent = '₱' + Number(data.revenueToday).toFixed(2);
    if (currentAdminRole === null) {
      currentAdminRole = data.role;
      applyRoleVisibility();
    }
  } catch (err) {
    console.error('Overview load failed', err);
  }
}

async function loadActiveUsers() {
  const tbody = document.querySelector('#usersTable tbody');
  if (!tbody) return;
  try {
    const res = await adminFetch('/api/admin/users');
    const rows = await res.json();
    if (!res.ok) return;
    tbody.innerHTML = rows.map(r => {
      const status = r.isSubscriber ? 'Subscriber' : (r.paused ? 'Paused' : 'Active');
      return `
      <tr>
        <td>${r.sessionId}</td>
        <td>${r.mode}</td>
        <td>${formatTime(r.timeRemainingSec)}</td>
        <td>${formatData(r.dataRemainingBytes)}</td>
        <td>${status}</td>
        <td>
          <button class="btn btn-danger" onclick="kickUser('${r.sessionId}')">Kick</button>
          <button class="btn btn-danger" onclick="blockUser('${r.sessionId}')">Block</button>
        </td>
      </tr>`;
    }).join('');
  } catch (err) {
    console.error('Active users load failed', err);
  }
}

// Admin-authorized kick/block - deliberately separate from /api/disconnect
// (the customer's OWN self-service path on the status page, unauthenticated
// by design). These hit /api/admin/* endpoints that require admin login.
async function kickUser(sessionId) {
  await adminFetch('/api/admin/kick', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ session: sessionId }),
  });
  loadActiveUsers();
}

async function blockUser(sessionId) {
  if (!confirm('Block this device? It will not be able to reconnect with any code until unblocked in Settings.')) return;
  await adminFetch('/api/admin/block', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ session: sessionId }),
  });
  loadActiveUsers();
}

// Generated vouchers open on a printable sheet (print.html) in a new tab
// instead of piling up under the button. The tab is opened right away,
// inside the click, so pop-up blockers allow it; it's pointed at the
// sheet once the NodeMCU has made the codes.
async function generateVouchers() {
  const pesoAmount = parseInt(document.getElementById('voucherType').value, 10) || 0;
  const count = Math.min(20, Math.max(1, parseInt(document.getElementById('voucherCount').value, 10) || 1));
  const note = document.getElementById('voucherPrintNote');
  const sheet = window.open('', '_blank');
  if (sheet) sheet.document.write('<p style="font-family:sans-serif;padding:24px">Generating vouchers...</p>');

  try {
    const res = await adminFetch('/api/admin/vouchers/generate', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ pesoAmount, count }),
    });
    const codes = await res.json();
    if (!res.ok) throw new Error(codes.error || 'generate_failed');

    const profile = rateProfilesCache.find(p => p.pesoAmount === pesoAmount) || { pesoAmount };
    const batch = {
      brand: document.title.replace(/ - Admin$/, '').replace(/^ZXHEIFI$/, 'ZXHEIFI'),
      codes, peso: pesoAmount, minutes: profile.minutes || 0, dataMb: profile.dataMb || 0,
      validityMinutes: profile.validityMinutes || 0, generatedAt: Date.now(),
    };
    localStorage.setItem('zxheifi_print_batch', JSON.stringify(batch));
    // Absolute URL: a blank pop-up has no reliable base to resolve 'print.html' against.
    if (sheet) sheet.location.href = new URL('print.html', window.location.href).href;
    note.innerHTML = '';
    const msg = document.createElement('span');
    msg.textContent = `${codes.length} voucher(s) of ₱${pesoAmount} made. `;
    const link = document.createElement('a');
    link.href = 'print.html';
    link.target = '_blank';
    link.textContent = sheet ? 'Open the print sheet again' : 'Open the print sheet (your browser blocked the new tab)';
    note.appendChild(msg);
    note.appendChild(link);
  } catch (err) {
    if (sheet) sheet.close();
    alert('Could not generate vouchers: ' + err.message);
  }
}

async function importVoucherCsv() {
  const input = document.getElementById('voucherCsvFile');
  const result = document.getElementById('importResult');
  if (!input.files.length) { alert('Choose a CSV file first.'); return; }

  const text = await input.files[0].text();
  try {
    const res = await adminFetch('/api/admin/vouchers/import', {
      method: 'POST',
      headers: { 'Content-Type': 'text/csv' },
      body: text,
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'import_failed');
    result.textContent = `Imported ${data.imported} voucher(s).`;
  } catch (err) {
    result.textContent = 'Import failed: ' + err.message;
  }
}

async function loadSubscribers() {
  const tbody = document.querySelector('#subscribersTable tbody');
  if (!tbody) return;
  try {
    const res = await adminFetch('/api/admin/subscribers');
    const rows = await res.json();
    if (!res.ok) return;
    tbody.innerHTML = rows.map(s => {
      const expires = new Date(s.expiryEpoch * 1000).toLocaleDateString();
      const status = !s.active ? 'Disabled' : (s.expired ? 'Expired' : 'Active');
      return `
      <tr>
        <td>${s.username}</td>
        <td>${speedLabel(s.tier)}</td>
        <td>${expires}</td>
        <td>${status}</td>
        <td>
          <button class="btn btn-success" onclick="renewSubscriber('${s.username}')">+30d</button>
          <button class="btn btn-secondary" onclick="toggleSubscriber('${s.username}', ${!s.active})">${s.active ? 'Disable' : 'Enable'}</button>
        </td>
      </tr>`;
    }).join('');
  } catch (err) {
    console.error('Subscribers load failed', err);
  }
}

async function addSubscriber() {
  const payload = {
    username: document.getElementById('subUsername').value.trim(),
    password: document.getElementById('subPassword').value,
    tier: document.getElementById('subTier').value,
    days: parseInt(document.getElementById('subDays').value, 10) || 30,
    price: parseFloat(document.getElementById('subPrice').value) || 0,
  };
  if (!payload.username || !payload.password) { alert('Username and password are required.'); return; }

  try {
    const res = await adminFetch('/api/admin/subscribers', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });
    const data = await res.json();
    if (!res.ok) {
      throw new Error({
        username_taken_or_table_full: 'Ginagamit na ang username na iyan, o puno na ang listahan ng subscribers.',
        clock_not_synced_try_again: 'Hindi pa alam ng NodeMCU ang oras. I-refresh ang page (F5) at subukan ulit.',
        missing_username_or_password: 'Kailangan ng username at password.',
      }[data.error] || data.error || 'add_failed');
    }
    document.getElementById('subUsername').value = '';
    document.getElementById('subPassword').value = '';
    document.getElementById('subPrice').value = '';
    loadSubscribers();
  } catch (err) {
    alert('Could not add subscriber: ' + err.message);
  }
}

async function renewSubscriber(username) {
  const days = parseInt(prompt('Extend by how many days?', '30'), 10);
  if (!days || days < 1) return;
  const price = parseFloat(prompt('Amount collected (PHP)?', '0')) || 0;
  try {
    const res = await adminFetch('/api/admin/subscribers/renew', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, days, price }),
    });
    if (!res.ok) throw new Error('renew_failed');
    loadSubscribers();
  } catch (err) {
    alert('Could not renew: ' + err.message);
  }
}

async function toggleSubscriber(username, active) {
  try {
    const res = await adminFetch('/api/admin/subscribers/toggle', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, active }),
    });
    if (!res.ok) throw new Error('toggle_failed');
    loadSubscribers();
  } catch (err) {
    alert('Could not update subscriber: ' + err.message);
  }
}

// ---- Sales Inventory -------------------------------------------------

function dayTotal(r) { return r.coinRevenue + r.voucherRevenue + r.subscriptionRevenue + (r.chargingRevenue || 0); }
function sumSales(rows) { return rows.reduce((t, r) => t + dayTotal(r), 0); }
function ymd(d) {
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
}

let salesDays = [];   // newest first, today included

async function loadSales() {
  if (!document.querySelector('#salesTable tbody')) return;
  try {
    const res = await adminFetch('/api/admin/sales');
    const data = await res.json();
    if (!res.ok) return;
    // Today's row may not have its date yet if the NodeMCU's clock isn't
    // set - label it with this device's date so it still shows up.
    const today = Object.assign({}, data.today, { dateStamp: data.today.dateStamp || ymd(new Date()) });
    salesDays = [today, ...data.history.filter(d => d.dateStamp && d.dateStamp !== today.dateStamp)];

    const now = new Date();
    const weekStart = ymd(new Date(now.getFullYear(), now.getMonth(), now.getDate() - 6));
    const monthStart = ymd(new Date(now.getFullYear(), now.getMonth(), 1));
    await loadSalesVendos();
    document.getElementById('salesToday').textContent = '₱' + dayTotal(today).toFixed(2);
    document.getElementById('salesWeek').textContent = '₱' + sumSales(salesDays.filter(d => d.dateStamp >= weekStart)).toFixed(2);
    document.getElementById('salesMonth').textContent = '₱' + sumSales(salesDays.filter(d => d.dateStamp >= monthStart)).toFixed(2);
    renderSales();
  } catch (err) {
    console.error('Sales load failed', err);
  }
}

function onSalesRangeChange() {
  const range = document.getElementById('salesRange').value;
  const from = document.getElementById('salesFrom');
  const to = document.getElementById('salesTo');
  from.style.display = (range === 'day' || range === 'custom') ? '' : 'none';
  to.style.display = document.getElementById('salesToLabel').style.display = range === 'custom' ? '' : 'none';
  if (!from.value) from.value = ymd(new Date());
  if (!to.value) to.value = ymd(new Date());
  renderSales();
}

// [from, to] as YYYY-MM-DD for the chosen range.
function salesRangeBounds() {
  const range = document.getElementById('salesRange').value;
  const now = new Date();
  const today = ymd(now);
  const back = n => ymd(new Date(now.getFullYear(), now.getMonth(), now.getDate() - n));
  if (range === 'today') return [today, today];
  if (range === '7') return [back(6), today];
  if (range === '30') return [back(29), today];
  if (range === 'month') return [ymd(new Date(now.getFullYear(), now.getMonth(), 1)), today];
  if (range === 'day') { const d = document.getElementById('salesFrom').value || today; return [d, d]; }
  if (range === 'custom') {
    const a = document.getElementById('salesFrom').value || today;
    const b = document.getElementById('salesTo').value || today;
    return a <= b ? [a, b] : [b, a];
  }
  return ['0000-00-00', '9999-99-99'];
}

function filteredSales() {
  const [from, to] = salesRangeBounds();
  return salesDays.filter(d => d.dateStamp >= from && d.dateStamp <= to);
}

// ---- Sales per vendo (v2) ------------------------------------------------
let salesVendos = [];   // [{id, name, commissionPct}] for the vendo filter

async function loadSalesVendos() {
  const sel = document.getElementById('salesVendo');
  if (!sel) return;
  try {
    const res = await adminFetch('/api/admin/vendos');
    if (!res.ok) return;
    const data = await res.json();
    salesVendos = data.vendos.map(v => ({ id: v.id, name: v.name, commissionPct: v.commissionPct }));
  } catch (err) {
    salesVendos = [{ id: 0, name: 'Main', commissionPct: 0 }];
  }
  const keep = sel.value;
  sel.innerHTML = '<option value="all">All coin boxes</option>' +
    salesVendos.map(v => `<option value="${v.id}">${escHtml(v.name)}</option>`).join('');
  sel.value = [...sel.options].some(o => o.value === keep) ? keep : 'all';
  document.getElementById('salesVendoWrap').style.display = salesVendos.length > 1 ? '' : 'none';
  document.getElementById('salesVendoCsvBtn').style.display = salesVendos.length > 1 ? '' : 'none';
}

// Coin pesos a vendo took on a day. Days saved before v2 have no split:
// all their coins were the main unit's.
function vendoPeso(day, id) {
  if (!Array.isArray(day.byVendo) || !day.byVendo.length) return id === 0 ? day.coinRevenue : 0;
  const e = day.byVendo.find(x => x.id === id);
  return e ? e.peso : 0;
}

function commissionSplit(amount, pct) {
  const host = Math.round(amount * pct) / 100;
  return { host, owner: amount - host };
}

function selectedSalesVendo() {
  const sel = document.getElementById('salesVendo');
  return !sel || sel.value === 'all' ? null : parseInt(sel.value, 10);
}

function renderSalesByVendo(vid) {
  const v = salesVendos.find(x => x.id === vid) || { name: 'Vendo ' + vid, commissionPct: 0 };
  const rows = filteredSales();
  const tbody = document.querySelector('#salesVendoTable tbody');
  let total = 0, hostTotal = 0;
  tbody.innerHTML = rows.length ? rows.map(d => {
    const peso = vendoPeso(d, vid);
    const split = commissionSplit(peso, v.commissionPct);
    total += peso;
    hostTotal += split.host;
    return `<tr><td>${d.dateStamp}</td><td>₱${peso.toFixed(2)}</td><td>${v.commissionPct}%</td>
      <td>₱${split.host.toFixed(2)}</td><td><b>₱${split.owner.toFixed(2)}</b></td></tr>`;
  }).join('') : '<tr><td colspan="5" style="color:#aaa;">No sales in this period.</td></tr>';
  const [from, to] = salesRangeBounds();
  const period = from === to ? from : (from === '0000-00-00' ? 'all saved days' : `${from} to ${to}`);
  document.getElementById('salesSummary').textContent =
    `${v.name}, ${period}: ₱${total.toFixed(2)} in coins` +
    (v.commissionPct ? ` - host share ₱${hostTotal.toFixed(2)}, yours ₱${(total - hostTotal).toFixed(2)}.` : '.');
}

// One row per date x coin box - for sharing income with location hosts.
function exportVendoSalesCsv() {
  const rows = filteredSales().slice().reverse();
  const [from, to] = salesRangeBounds();
  const lines = ['date,vendo,coins,commission_pct,host_share,owner_share'];
  rows.forEach(d => salesVendos.forEach(v => {
    const peso = vendoPeso(d, v.id);
    if (!peso) return;
    const split = commissionSplit(peso, v.commissionPct);
    const name = '"' + String(v.name).replace(/"/g, '""') + '"';
    lines.push([d.dateStamp, name, peso.toFixed(2), v.commissionPct, split.host.toFixed(2), split.owner.toFixed(2)].join(','));
  }));
  const blob = new Blob([lines.join('\r\n') + '\r\n'], { type: 'text/csv' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = from === '0000-00-00' ? 'sales-per-vendo-all.csv' :
    (from === to ? `sales-per-vendo-${from}.csv` : `sales-per-vendo-${from}_to_${to}.csv`);
  a.click();
  URL.revokeObjectURL(a.href);
}

function renderSales() {
  const tbody = document.querySelector('#salesTable tbody');
  if (!tbody) return;
  const vid = selectedSalesVendo();
  document.getElementById('salesTable').style.display = vid === null ? '' : 'none';
  document.getElementById('salesVendoTable').style.display = vid === null ? 'none' : '';
  if (vid !== null) {
    renderSalesByVendo(vid);
    return;
  }
  const rows = filteredSales();
  tbody.innerHTML = rows.length ? rows.map(d => `
      <tr>
        <td>${d.dateStamp}</td>
        <td>₱${d.coinRevenue.toFixed(2)}</td>
        <td>₱${d.voucherRevenue.toFixed(2)}</td>
        <td>₱${d.subscriptionRevenue.toFixed(2)}</td>
        <td>₱${(d.chargingRevenue || 0).toFixed(2)}</td>
        <td><b>₱${dayTotal(d).toFixed(2)}</b></td>
        <td>${d.users}</td>
      </tr>`).join('') : '<tr><td colspan="7" style="color:#aaa;">No sales in this period.</td></tr>';
  const [from, to] = salesRangeBounds();
  const period = from === to ? from : (from === '0000-00-00' ? 'all saved days' : `${from} to ${to}`);
  document.getElementById('salesSummary').textContent =
    `${period}: ₱${sumSales(rows).toFixed(2)} total, ${rows.reduce((t, r) => t + r.users, 0)} customers.`;
}

function exportSalesCsv() {
  const rows = filteredSales().slice().reverse();   // oldest first in the file
  const [from, to] = salesRangeBounds();
  const lines = ['Date,Coin,Voucher,Subscription,Charging,Total,Customers,Data Used (MB)'];
  rows.forEach(d => lines.push([d.dateStamp, d.coinRevenue.toFixed(2), d.voucherRevenue.toFixed(2),
    d.subscriptionRevenue.toFixed(2), (d.chargingRevenue || 0).toFixed(2), dayTotal(d).toFixed(2), d.users,
    (d.dataUsedBytes / 1048576).toFixed(0)].join(',')));
  const col = key => rows.reduce((t, d) => t + (d[key] || 0), 0).toFixed(2);
  lines.push(['TOTAL', col('coinRevenue'), col('voucherRevenue'), col('subscriptionRevenue'), col('chargingRevenue'),
    sumSales(rows).toFixed(2), rows.reduce((t, r) => t + r.users, 0), ''].join(','));
  const blob = new Blob([lines.join('\r\n') + '\r\n'], { type: 'text/csv' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = from === '0000-00-00' ? 'sales-all.csv' : (from === to ? `sales-${from}.csv` : `sales-${from}_to_${to}.csv`);
  a.click();
  URL.revokeObjectURL(a.href);
}

// ---- Activity Log ------------------------------------------------------

async function loadLogs() {
  const tbody = document.querySelector('#logsTable tbody');
  if (!tbody) return;
  try {
    const res = await adminFetch('/api/admin/logs');
    const rows = await res.json();
    if (!res.ok) return;
    tbody.innerHTML = rows.slice().reverse().map(e => `
      <tr>
        <td>${new Date(e.epoch * 1000).toLocaleString()}</td>
        <td>${e.type}</td>
        <td>${e.detail}</td>
      </tr>`).join('');
  } catch (err) {
    console.error('Logs load failed', err);
  }
}

// ---- Admin accounts (super only) ---------------------------------------

async function loadAdminAccounts() {
  const tbody = document.querySelector('#adminsTable tbody');
  if (!tbody) return;
  try {
    const res = await adminFetch('/api/admin/accounts');
    const rows = await res.json();
    if (!res.ok) return;
    tbody.innerHTML = rows.map(a => `
      <tr>
        <td>${a.username}</td>
        <td>${a.role}</td>
        <td>${a.active ? 'Active' : 'Disabled'}</td>
        <td>
          <button class="btn btn-secondary" onclick="toggleAdminAccount('${a.username}', ${!a.active})">${a.active ? 'Disable' : 'Enable'}</button>
        </td>
      </tr>`).join('');
  } catch (err) {
    console.error('Admin accounts load failed', err);
  }
}

async function addAdminAccount() {
  const payload = {
    username: document.getElementById('adminUsername').value.trim(),
    password: document.getElementById('adminNewPassword').value,
    role: document.getElementById('adminRole').value,
  };
  if (!payload.username || !payload.password) { alert('Username and password are required.'); return; }

  try {
    const res = await adminFetch('/api/admin/accounts', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'add_failed');
    document.getElementById('adminUsername').value = '';
    document.getElementById('adminNewPassword').value = '';
    loadAdminAccounts();
  } catch (err) {
    alert('Could not add admin account: ' + err.message);
  }
}

async function toggleAdminAccount(username, active) {
  try {
    const res = await adminFetch('/api/admin/accounts/toggle', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, active }),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'toggle_failed');
    loadAdminAccounts();
  } catch (err) {
    alert('Could not update admin account: ' + err.message);
  }
}

// ---- Rate Profiles table (Settings tab) -------------------------------

function addRateProfileRow(profile) {
  const p = profile || { pesoAmount: '', minutes: '', dataMb: 0, validityMinutes: 1440,
                         speedProfile: (speedProfilesCache[0] || { id: '1' }).id };
  const tbody = document.getElementById('rateProfilesBody');
  const tr = document.createElement('tr');
  tr.innerHTML = `
    <td><input type="number" min="1" step="1" class="rp-peso" value="${p.pesoAmount}" style="width:70px;"></td>
    <td><input type="number" min="1" step="1" class="rp-minutes" value="${p.minutes}" style="width:80px;">
        <div class="rp-hint rp-minutes-hint"></div></td>
    <td><input type="number" min="0" step="1" class="rp-data" value="${p.dataMb}" style="width:90px;"></td>
    <td><input type="number" min="0" step="1" class="rp-validity" value="${p.validityMinutes}" style="width:90px;">
        <div class="rp-hint rp-validity-hint"></div></td>
    <td><select class="rp-speed" data-value="${p.speedProfile}"></select></td>
    <td><button type="button" class="btn btn-danger" onclick="this.closest('tr').remove()">&times;</button></td>`;
  tbody.appendChild(tr);
  const hints = () => {
    tr.querySelector('.rp-minutes-hint').textContent = formatDHM(parseInt(tr.querySelector('.rp-minutes').value, 10) || 0);
    const v = parseInt(tr.querySelector('.rp-validity').value, 10) || 0;
    tr.querySelector('.rp-validity-hint').textContent = v ? formatDHM(v) : 'no expiry';
  };
  tr.querySelector('.rp-minutes').addEventListener('input', hints);
  tr.querySelector('.rp-validity').addEventListener('input', hints);
  hints();
  fillSpeedSelect(tr.querySelector('.rp-speed'), p.speedProfile);
}

// ---- Speed Profiles (Settings) ---------------------------------------

let speedProfilesCache = [];   // [{id, name, downMbps, upMbps}] as last loaded/edited

function speedLabel(id) {
  const sp = speedProfilesCache.find(s => s.id === String(id));
  return sp ? `${sp.name} (${sp.downMbps}/${sp.upMbps} Mbps)` : `Speed ${id}`;
}

function fillSpeedSelect(select, selectedId) {
  if (!select) return;
  const keep = selectedId !== undefined ? String(selectedId) : (select.value || select.dataset.value);
  select.innerHTML = '';
  if (!speedProfilesCache.length) {
    const o = document.createElement('option');
    o.value = keep || '1';
    o.textContent = '(walang Speed Profile - magdagdag sa ibaba)';
    select.appendChild(o);
    return;
  }
  speedProfilesCache
    .forEach(sp => {
      const o = document.createElement('option');
      o.value = sp.id;
      o.textContent = speedLabel(sp.id);
      select.appendChild(o);
    });
  if ([...select.options].some(o => o.value === keep)) select.value = keep;
}

function addSpeedProfileRow(sp) {
  const ids = [...document.querySelectorAll('#speedProfilesBody tr')].map(tr => parseInt(tr.dataset.id, 10) || 0);
  const s = sp || { id: String(Math.max(3, ...ids) + 1), name: '', downMbps: 2, upMbps: 1 };
  const tr = document.createElement('tr');
  tr.dataset.id = s.id;
  tr.innerHTML = `
    <td><input type="text" class="sp-name" maxlength="24" placeholder="e.g. Piso 2M" style="width:140px;"></td>
    <td><input type="number" class="sp-down" min="0.5" step="0.5" style="width:90px;"></td>
    <td><input type="number" class="sp-up" min="0.5" step="0.5" style="width:90px;"></td>
    <td><button type="button" class="btn btn-danger">&times;</button></td>`;
  tr.querySelector('.sp-name').value = s.name;
  tr.querySelector('.sp-down').value = s.downMbps;
  tr.querySelector('.sp-up').value = s.upMbps;
  tr.querySelector('button').addEventListener('click', () => {
    const inUse = [...document.querySelectorAll('#rateProfilesBody .rp-speed')].some(sel => sel.value === s.id);
    if (inUse) { alert('This speed is still chosen by a Rate Profile - change those rows first.'); return; }
    tr.remove();
    syncSpeedProfiles();
  });
  tr.querySelectorAll('input').forEach(i => i.addEventListener('input', syncSpeedProfiles));
  document.getElementById('speedProfilesBody').appendChild(tr);
  if (!sp) syncSpeedProfiles();
}

function collectSpeedProfiles() {
  return [...document.querySelectorAll('#speedProfilesBody tr')].map(tr => ({
    id: tr.dataset.id,
    name: tr.querySelector('.sp-name').value.trim(),
    downMbps: parseFloat(tr.querySelector('.sp-down').value) || 0,
    upMbps: parseFloat(tr.querySelector('.sp-up').value) || 0,
  }));
}

// Keeps the Rate Profiles' speed dropdowns in step with edits here.
function syncSpeedProfiles() {
  speedProfilesCache = collectSpeedProfiles();
  document.querySelectorAll('#rateProfilesBody .rp-speed').forEach(sel => fillSpeedSelect(sel));
}

function speedProfileProblems(list) {
  const problems = [];
  if (!list.length) problems.push('Kailangan ng kahit isang Speed Profile.');
  list.forEach((sp, i) => {
    if (!sp.name) problems.push(`Speed row ${i + 1}: lagyan ng pangalan.`);
    if (sp.downMbps <= 0 || sp.upMbps <= 0) problems.push(`Speed row ${i + 1}: lagyan ng Download at Upload (Mbps).`);
  });
  if (list.length > 8) problems.push('Hanggang 8 Speed Profiles lang.');
  return problems;
}

function collectRateProfilesFromTable() {
  return Array.from(document.querySelectorAll('#rateProfilesBody tr')).map(tr => ({
    pesoAmount: parseInt(tr.querySelector('.rp-peso').value, 10) || 0,
    minutes: parseInt(tr.querySelector('.rp-minutes').value, 10) || 0,
    dataMb: parseInt(tr.querySelector('.rp-data').value, 10) || 0,
    validityMinutes: parseInt(tr.querySelector('.rp-validity').value, 10) || 0,
    speedProfile: tr.querySelector('.rp-speed').value,
  }));
}

// Mirrors firmware/gui_handler.h handleAdminSaveRateProfiles(). Rows with
// a missing ₱ or minutes used to be dropped silently before saving, which
// looked like "Save did nothing".
const MAX_RATE_PROFILES = 20; // firmware/config.h
function rateProfileProblems(profiles) {
  const problems = [];
  const seen = {};
  profiles.forEach((p, i) => {
    const row = `Row ${i + 1}`;
    if (p.pesoAmount <= 0) problems.push(`${row}: lagyan ng ₱ amount (hal. 1, 5, 10).`);
    if (p.minutes <= 0) problems.push(`${row}: lagyan ng minutes (hal. 30).`);
    if (p.pesoAmount > 0 && seen[p.pesoAmount]) problems.push(`${row}: may iba nang profile para sa ₱${p.pesoAmount} (Row ${seen[p.pesoAmount]}).`);
    if (p.pesoAmount > 0) seen[p.pesoAmount] = seen[p.pesoAmount] || i + 1;
  });
  if (profiles.length > MAX_RATE_PROFILES) problems.push(`Hanggang ${MAX_RATE_PROFILES} profiles lang.`);
  return problems;
}

const RATE_PROFILE_ERRORS = {
  unknown_speed_profile: 'May Rate Profile na walang katugmang Speed Profile.',
  speed_profile_in_use: 'May Speed Profile na tinanggal pero ginagamit pa ng isang Rate Profile o subscriber.',
  invalid_speed_profile: 'May Speed Profile na kulang ang pangalan o Mbps.',
  duplicate_speed_profile: 'May dalawang Speed Profile na pareho ang id.',
  too_many_speed_profiles: 'Hanggang 8 Speed Profiles lang.',
  need_one_speed_profile: 'Kailangan ng kahit isang Speed Profile.',
  invalid_pin: 'Hindi pwedeng gamitin ang napiling pin.',
  coin_and_relay_same_pin: 'Magkaiba dapat ang coin pin at relay pin.',
  invalid_pulse_value: 'Pesos per pulse: 1 hanggang 100.',
  invalid_profile: 'May profile na kulang ang ₱ o minutes.',
  duplicate_peso_amount: 'May dalawang profile na pareho ang ₱ amount.',
  too_many_profiles: `Hanggang ${MAX_RATE_PROFILES} profiles lang.`,
};

async function loadRateProfilesIntoTable() {
  try {
    const res = await adminFetch('/api/admin/rate-profiles');
    const rows = await res.json();
    if (!res.ok) return;
    document.getElementById('rateProfilesBody').innerHTML = '';
    rows.forEach(addRateProfileRow);
  } catch (err) {
    console.error('Rate profiles load failed', err);
  }
}

async function saveRateProfiles() {
  const profiles = collectRateProfilesFromTable();
  const problems = rateProfileProblems(profiles);
  if (problems.length) throw new Error('\n' + problems.join('\n'));
  const res = await adminFetch('/api/admin/rate-profiles', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(profiles),
  });
  if (!res.ok) {
    const data = await res.json().catch(() => ({}));
    throw new Error(RATE_PROFILE_ERRORS[data.error] || data.error || 'rate_profiles_save_failed');
  }
}

function exportRateProfiles() {
  const profiles = collectRateProfilesFromTable();
  const blob = new Blob([JSON.stringify(profiles, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'rate_profiles.json';
  a.click();
  URL.revokeObjectURL(url);
}

function importRateProfiles(event) {
  const file = event.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => {
    try {
      const profiles = JSON.parse(reader.result);
      if (!Array.isArray(profiles)) throw new Error('not_an_array');
      document.getElementById('rateProfilesBody').innerHTML = '';
      profiles.forEach(p => addRateProfileRow({
        pesoAmount: p.pesoAmount, minutes: p.minutes, dataMb: p.dataMb || 0,
        validityMinutes: p.validityMinutes || 0, speedProfile: String(p.speedProfile || '1'),
      }));
    } catch (err) {
      alert('Could not import - not a valid Rate Profiles JSON file.');
    }
  };
  reader.readAsText(file);
  event.target.value = ''; // allow re-importing the same filename later
}

// ---- Blocked devices (Settings tab) ------------------------------------

async function loadBlockedList() {
  const el = document.getElementById('blockedList');
  if (!el) return;
  try {
    const res = await adminFetch('/api/admin/blocked');
    const macs = await res.json();
    if (!res.ok) return;
    el.innerHTML = macs.length ? macs.map(mac => `
      <div class="voucher-item">
        <span class="voucher-code">${mac}</span>
        <button type="button" class="btn btn-secondary" onclick="unblockMac('${mac}')">Unblock</button>
      </div>`).join('') : '<p style="color:#aaa;font-size:13px;">No blocked devices.</p>';
  } catch (err) {
    console.error('Blocked list load failed', err);
  }
}

async function unblockMac(mac) {
  await adminFetch('/api/admin/unblock', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ mac }),
  });
  loadBlockedList();
}

// Visible result next to the Save button (alerts were easy to miss, and
// a crash before the first alert showed nothing at all).
function setSaveStatus(kind, message) {
  const el = document.getElementById('settingsSaveStatus');
  if (!el) return;
  el.className = 'save-status ' + kind;
  el.textContent = message;
  el.style.display = message ? 'block' : 'none';
}

async function saveSettings() {
  const btn = document.getElementById('saveSettingsBtn');
  if (firmwareOutdated) {
    setSaveStatus('error', 'Hindi ma-save: luma ang firmware ng NodeMCU. I-flash muna ang bagong zxheifi_firmware.bin.');
    return;
  }
  if (btn) { btn.disabled = true; btn.textContent = 'Saving...'; }
  setSaveStatus('pending', 'Sine-save... (ilang segundo, ipinapadala rin sa MikroTik)');
  try {
    await doSaveSettings();
  } finally {
    if (btn) { btn.disabled = false; btn.textContent = 'Save Settings'; }
  }
}

async function doSaveSettings() {
  let payload;
  try {
  payload = {
    nightPromoEnabled: document.getElementById('nightPromoEnabled').checked,
    idleTimeoutMin: parseInt(document.getElementById('idleTimeout').value, 10) || 5,
    autoRebootTime: document.getElementById('autoReboot').value,
    brandName: document.getElementById('brandName').value.trim(),
    brandColor: document.getElementById('brandColor').value,
    soundEnabled: document.getElementById('soundEnabled').checked,
    announcement: document.getElementById('announcement').value.trim(),
    speedProfiles: collectSpeedProfiles(),
    rateProfiles: collectRateProfilesFromTable(),
    coinPin: document.getElementById('coinPin').value,
    relayPin: document.getElementById('relayPin').value,
    relayActiveHigh: document.getElementById('relayActiveHigh').value === 'true',
    coinPulseValue: parseInt(document.getElementById('coinPulseValue').value, 10) || 1,
    telegramEnabled: document.getElementById('telegramEnabled').checked,
    telegramBotToken: document.getElementById('telegramBotToken').value.trim(),
    telegramChatId: document.getElementById('telegramChatId').value.trim(),
    chargeRates: collectChargeRates(),
    chargeMaxMinutes: parseInt(document.getElementById('chargeMaxMinutes').value, 10) || 0,
    chargeLang: document.getElementById('chargeLang').value,
  };
  } catch (err) {
    setSaveStatus('error', 'Hindi na-save (page error: ' + err.message + '). I-refresh ang page at subukan ulit.');
    return;
  }
  try {
    // Speed and rate profiles go in the same save so a new speed can be
    // used by a rate straight away (see handleAdminSaveSettings).
    const problems = speedProfileProblems(payload.speedProfiles).concat(rateProfileProblems(payload.rateProfiles))
      .concat(chargeRateProblems(payload.chargeRates, payload.chargeMaxMinutes));
    if (payload.coinPin === payload.relayPin) problems.push('Magkaiba dapat ang coin pin at relay pin.');
    if (problems.length) throw new Error('\n' + problems.join('\n'));
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 60000);   // pushing up to 8 speeds to a slow router takes a while
    let res;
    try {
      res = await adminFetch('/api/admin/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
        signal: controller.signal,
      });
    } catch (err) {
      throw new Error(err.name === 'AbortError'
        ? 'walang sagot ang NodeMCU sa loob ng 60 segundo. Tingnan kung naka-on ito, i-refresh ang Settings para makita kung na-save, tapos subukan ulit.'
        : 'hindi maabot ang NodeMCU (' + err.message + ').');
    } finally {
      clearTimeout(timer);
    }
    const data = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(RATE_PROFILE_ERRORS[data.error] || data.error || ('HTTP ' + res.status));
    // mikrotikPushed reflects whether the new Mbps caps actually made
    // it to the router live - false just means the router was
    // unreachable at that moment; the values are still saved here and
    // will apply on the next successful save.
    setSaveStatus(data.mikrotikPushed ? 'ok' : 'warn', data.mikrotikPushed
      ? '✅ Na-save! Naipadala na rin sa MikroTik ang mga Speed Profile.'
      : '✅ Na-save sa NodeMCU. ⚠️ Hindi naabot ang MikroTik para sa Speed Profiles - pindutin ulit ang Save kapag online na ang router.');
    applyBranding(); // reflect a name/color change immediately, no reload needed
    loadTierLabels(); // refresh the Voucher Type dropdown with any rate changes
    loadSettingsIntoForm(); // show exactly what the NodeMCU now has
  } catch (err) {
    setSaveStatus('error', '❌ Hindi na-save: ' + err.message);
  }
}

async function loadSettingsIntoForm() {
  try {
    const res = await adminFetch('/api/admin/settings');
    const data = await res.json();
    if (!res.ok) return;
    document.getElementById('nightPromoEnabled').checked = !!data.nightPromoEnabled;
    document.getElementById('idleTimeout').value = data.idleTimeoutMin;
    document.getElementById('autoReboot').value = data.autoRebootTime;
    document.getElementById('brandName').value = data.brandName || '';
    document.getElementById('brandColor').value = data.brandColor || '#2dd4c9';
    document.getElementById('soundEnabled').checked = !!data.soundEnabled;
    document.getElementById('announcement').value = data.announcement || '';
    fillPinChoices(data.pinChoices, data.coinPin, data.relayPin);
    document.getElementById('relayActiveHigh').value = data.relayActiveHigh === false ? 'false' : 'true';
    document.getElementById('coinPulseValue').value = data.coinPulseValue || 1;
    speedProfilesCache = (data.speedProfiles || []).map(sp => Object.assign({}, sp, { id: String(sp.id) }));
    const speedBody = document.getElementById('speedProfilesBody');
    speedBody.innerHTML = '';
    speedProfilesCache.forEach(sp => addSpeedProfileRow(sp));
    if (!Array.isArray(data.speedProfiles)) {
      firmwareOutdated = true;
      checkFirmwareVersion();
      setSaveStatus('error', 'Luma ang firmware ng NodeMCU - hindi pa nito kilala ang Speed Profiles. I-flash muna ang bagong firmware.');
    }
    document.getElementById('telegramEnabled').checked = !!data.telegramEnabled;
    document.getElementById('telegramBotToken').value = data.telegramBotToken || '';
    document.getElementById('telegramChatId').value = data.telegramChatId || '';
    document.getElementById('chargeRatesBody').innerHTML = '';
    (data.chargeRates || []).forEach(r => addChargeRateRow(r));
    document.getElementById('chargeMaxMinutes').value = data.chargeMaxMinutes || 180;
    document.getElementById('chargeLang').value = data.chargeLang === 'en' ? 'en' : 'tl';
    loadRateProfilesIntoTable();
    loadBlockedList();
  } catch (err) {
    console.error('Settings load failed', err);
  }
}

function initAdminPage() {
  document.getElementById('adminLoginForm').addEventListener('submit', submitAdminLogin);
  if (savedAdminCreds()) startAdminDashboard();
  else showAdminLogin('');
}

let firmwareOutdated = false;

// Semantic version check ("1.2.10" >= "1.2.9"). Pre-1.0 development builds
// were stamped with dates ("2026.09.26.2") - those count as older.
function versionAtLeast(have, want) {
  if (!have) return false;
  const [haveCore, haveTag] = String(have).split('-');
  const [wantCore, wantTag] = String(want).split('-');
  const a = haveCore.split('.').map(n => parseInt(n, 10) || 0);
  const b = wantCore.split('.').map(n => parseInt(n, 10) || 0);
  if (a[0] >= 2000) return false;
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    if ((a[i] || 0) !== (b[i] || 0)) return (a[i] || 0) > (b[i] || 0);
  }
  // Same numbers: a pre-release ("2.0.0-dev") is older than the release.
  return !(haveTag && !wantTag);
}

// Pages newer than the NodeMCU's firmware = features that silently don't
// work (no speed profiles, Save rejected). Say so plainly, at the top.
async function checkFirmwareVersion() {
  const banner = document.getElementById('fwBanner');
  try {
    const res = await fetch(apiBase() + '/api/health');
    const d = await res.json();
    firmwareOutdated = !versionAtLeast(d.fw, REQUIRED_FIRMWARE);
    if (banner) {
      banner.style.display = firmwareOutdated ? 'block' : 'none';
      banner.textContent = firmwareOutdated
        ? `Luma ang firmware ng NodeMCU (${d.fw || 'lumang build'}). Kailangan: ${REQUIRED_FIRMWARE} o mas bago. ` +
          'I-flash ang bagong zxheifi_firmware.bin gamit ang desktop app (Flash Firmware tab, huwag i-check ang Erase). ' +
          'Hangga\'t hindi, hindi gagana nang tama ang Settings (Speed Profiles, Coin Slot) at Sales.'
        : '';
    }
  } catch (err) {
    // NodeMCU unreachable - the rest of the page reports that already.
  }
}

function startAdminDashboard() {
  checkFirmwareVersion();
  loadOverview();
  loadTierLabels();
  // Settings is super-only (see requireAdmin(requireSuper=true) in
  // gui_handler.h) - loaded lazily by openTab('settings') instead of
  // here, so a staff account doesn't get an immediate 401 on page load.
}

// ---- Tier dropdown labels (Vouchers / Subscriptions / Coin Slot) ------

function formatDurationLabel(mins) {
  if (mins > 0 && mins % 60 === 0) return (mins / 60) + 'hr';
  return mins + 'min';
}

function formatDataLabel(dataMb) {
  if (dataMb >= 1024) {
    const gb = dataMb / 1024;
    return (gb % 1 === 0 ? gb : gb.toFixed(1)) + 'GB';
  }
  return dataMb + 'MB';
}

// The Voucher Type dropdown (Vouchers tab) and the Speed dropdowns
// (Subscriptions tab) used to be plain hardcoded text - once pricing
// became fully Settings-editable, that text could show stale numbers
// the moment an admin changed Settings. Pulled live from
// /api/admin/tier-info (rate profiles, open to staff too) and
// /api/admin/settings (Mbps, super-only - falls back to leaving the
// Speed labels as-is for a staff account, matching the existing
// role-gating pattern elsewhere in this file).
let rateProfilesCache = [];   // for the voucher print sheet

async function loadTierLabels() {
  try {
    const res = await adminFetch('/api/admin/tier-info');
    const profiles = await res.json();
    if (!res.ok) return;
    rateProfilesCache = profiles;
    const genBtn = document.getElementById('generateVouchersBtn');
    if (genBtn) genBtn.disabled = !profiles.length;
    if (!profiles.length) {
      const sel = document.getElementById('voucherType');
      if (sel) sel.innerHTML = '<option value="">Walang Rate Profile - gumawa muna sa Settings</option>';
      return;
    }

    const select = document.getElementById('voucherType');
    if (select) {
      const previous = select.value;
      select.innerHTML = profiles.map(p =>
        `<option value="${p.pesoAmount}">&#8369;${p.pesoAmount} - ${formatDHM(p.minutes)} / ${p.dataMb > 0 ? formatDataLabel(p.dataMb) : 'unlimited data'}</option>`
      ).join('');
      if (profiles.some(p => String(p.pesoAmount) === previous)) select.value = previous;
    }
  } catch (err) {
    console.error('Tier labels load failed', err); // dropdown just stays empty/stale
  }
}

// Subscriptions tab speed dropdown = Settings > Speed Profiles
// (super-only data, like the tab itself).
async function loadSpeedLabels() {
  try {
    const res = await adminFetch('/api/admin/settings');
    const d = await res.json();
    if (!res.ok) return;
    speedProfilesCache = (d.speedProfiles || []).map(sp => Object.assign({}, sp, { id: String(sp.id) }));
    fillSpeedSelect(document.getElementById('subTier'), (speedProfilesCache[0] || {}).id);
    loadSubscribers();   // re-render so the Speed column shows names
  } catch (err) {
    console.error('Speed labels load failed', err);
  }
}

// ---- Boot ------------------------------------------------------------

document.addEventListener('DOMContentLoaded', () => {
  startPeriodicUpdates();
  applyBranding();
  if (document.getElementById('hotspotForm')) initLoginPage();
  if (document.getElementById('timeRemaining')) initStatusPage();
  if (document.getElementById('totalUsers')) initAdminPage();
});


// ---- Vendos tab (v2: sub vendos) ----------------------------------------
// Coin boxes: this unit ("Main") plus paired sub vendos. Staff can see
// them and mark a box Collected; adding/editing/removing is super-only.
let vendoCache = null;

const VENDO_ERRORS = {
  too_many_pending: 'Two pairing codes are already waiting. Use one first, or wait 15 minutes for them to expire.',
  bad_name: 'The name must be 1-32 characters.',
  invalid_pin: 'That pin cannot be used (allowed: D1, D2, D5, D6, D7).',
  coin_and_relay_same_pin: 'The coin signal and the relay cannot use the same pin.',
  bad_pulse_value: 'Pesos per pulse must be 1-100.',
  bad_commission: 'Commission must be 0-100%.',
  cannot_remove_main: 'The main unit cannot be removed.',
  vendo_unknown: 'That coin box no longer exists - reload the tab.',
  bad_type: 'Choose a type: WiFi coin box or Charging Station.',
  bad_ports: 'Ports must be 1-4.',
  bad_port: 'That port does not exist on this box.',
  not_charging: 'Only Charging Stations have ports to stop.',
  bad_relay_level: 'Choose HIGH or LOW.',
};

function vendoError(d) {
  if (d && d.error === 'vendo_limit') {
    return `Limit reached: this main unit (${d.board === 'esp8266' ? 'NodeMCU' : 'ESP32'}) supports up to ${d.limit} sub vendos.`;
  }
  return VENDO_ERRORS[d && d.error] || ('Error: ' + ((d && d.error) || 'unknown'));
}

function vendoStatus(v) {
  if (v.id === 0 || v.online) return '🟢 online';
  if (!v.paired) return '⚪ waiting for pairing';
  return v.lastSeenSec < 0 ? '🔴 offline' : `🔴 offline ${Math.max(1, Math.round(v.lastSeenSec / 60))} min`;
}

async function loadVendos() {
  const tbody = document.querySelector('#vendosTable tbody');
  if (!tbody) return;
  try {
    const res = await adminFetch('/api/admin/vendos');
    const data = await res.json();
    if (!res.ok) return;
    vendoCache = data;
    const isSuper = currentAdminRole !== 'staff';
    document.getElementById('vendoLimitNote').textContent = data.board === 'esp8266'
      ? `NodeMCU main unit: up to ${data.limit} sub vendos. For more, or for upcoming features (Telegram bot, GCash), use an ESP32 as the main unit.`
      : `ESP32 main unit: up to ${data.limit} sub vendos.`;
    const subs = data.vendos.filter(v => v.id !== 0).length + data.pending.filter(p => !p.targetId).length;
    const addBtn = document.getElementById('addVendoBtn');
    addBtn.style.display = isSuper ? '' : 'none';
    addBtn.disabled = subs >= data.limit;
    addBtn.title = addBtn.disabled ? 'Limit reached for this board' : '';
    tbody.innerHTML = data.vendos.map(v => {
      const charging = v.type === 'charging';
      const actions = [`<button class="btn btn-secondary btn-inline" onclick="collectVendo(${v.id})">Collected</button>`];
      if (!charging) {
        actions.push(`<button class="btn btn-secondary btn-inline" onclick="openVendoSticker(${v.id})">QR</button>`);
      }
      if (isSuper) {
        actions.push(`<button class="btn btn-secondary btn-inline" onclick="editVendo(${v.id})">Edit</button>`);
        if (v.id !== 0) {
          actions.push(`<button class="btn btn-secondary btn-inline" onclick="repairVendo(${v.id})">Re-pair</button>`);
          actions.push(`<button class="btn btn-danger btn-inline" onclick="removeVendo(${v.id})">Remove</button>`);
        }
      }
      const label = charging ? '<br><span class="hint">Charging Station</span>' : '';
      return `<tr><td>${escHtml(v.name)}${label}</td><td>${vendoStatus(v)}${charging ? chargingPorts(v) : ''}</td><td>₱${v.todayPeso}</td>
        <td>₱${v.boxTotal}</td><td>${v.commissionPct}%</td><td class="vendo-actions">${actions.join(' ')}</td></tr>`;
    }).join('');
    document.getElementById('vendoPending').innerHTML = data.pending.map(p => `
      <div class="pair-code-box">
        <b>${escHtml(p.name)}</b>${p.targetId ? ' (re-pair)' : ''} - pairing code
        <code>${escHtml(p.code)}</code> <span class="hint">expires in ${Math.ceil(p.expiresInSec / 60)} min</span>
      </div>`).join('');
  } catch (err) {
    console.error('Vendos load failed', err);
  }
}

async function postVendo(path, body) {
  const res = await adminFetch(path, {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    alert(vendoError(data));
    return null;
  }
  return data;
}

function showAddVendo() {
  document.getElementById('vendoAddName').value = '';
  document.getElementById('vendoAddType').value = 'wifi';
  document.getElementById('vendoAddForm').style.display = '';
  document.getElementById('vendoAddName').focus();
}

function hideAddVendo() {
  document.getElementById('vendoAddForm').style.display = 'none';
}

async function createVendo() {
  const name = document.getElementById('vendoAddName').value.trim();
  const type = document.getElementById('vendoAddType').value;
  if (!name) {
    alert(VENDO_ERRORS.bad_name);
    return;
  }
  const data = await postVendo('/api/admin/vendos/add', { name, type });
  if (!data) return;
  hideAddVendo();
  const charging = type === 'charging';
  alert(`Pairing code for "${name}":\n\n${data.code}\n\nValid for 15 minutes, one use.\n` +
        `1. Flash the box with the Setup Companion (Device type: ${charging ? 'Charging Station' : 'Sub Vendo'}).\n` +
        `2. On a phone, join the WiFi "${charging ? 'ZxheiFi-Charge-Setup' : 'ZxheiFi-Sub-Setup'}".\n` +
        '3. Enter the WiFi of this spot and this code, then Save.');
  loadVendos();
}

function fmtPortTime(secs) {
  if (!secs) return 'libre';
  const h = Math.floor(secs / 3600), m = Math.round((secs % 3600) / 60);
  return h ? `${h}h${String(m).padStart(2, '0')}m` : `${Math.max(1, m)}m`;
}

// "P1 libre · P2 23m" plus a Stop button per running port (charging boxes).
function chargingPorts(v) {
  const secs = v.portSecs || [];
  const stale = v.portSecsAgoSec < 0 || v.portSecsAgoSec > 30;
  return `<div class="port-chips">${secs.map((s, i) => `<span class="port-chip${s ? ' on' : ''}">P${i + 1} ${fmtPortTime(s)}` +
    (s ? ` <button class="btn btn-danger btn-mini" onclick="stopPort(${v.id}, ${i + 1})">Stop</button>` : '') +
    '</span>').join('')}${stale && v.paired ? ' <span class="hint">(last report may be old)</span>' : ''}</div>`;
}

async function stopPort(id, port) {
  const v = vendoCache && vendoCache.vendos.find(x => x.id === id);
  if (!v || !confirm(`Stop port ${port} of "${v.name}"?\nIts remaining time is lost (it's logged, so you can refund).`)) return;
  if (await postVendo('/api/admin/vendos/stop', { id, port })) {
    alert('Stop sent - the box applies it within a few seconds.');
    loadVendos();
  }
}

async function collectVendo(id) {
  const v = vendoCache && vendoCache.vendos.find(x => x.id === id);
  if (!v) return;
  if (!confirm(`Collected ₱${v.boxTotal} from "${v.name}"?\nThis records the collection and resets the box to ₱0.`)) return;
  if (await postVendo('/api/admin/vendos/collected', { id })) {
    loadVendos();
    loadCollections();
  }
}

function openVendoSticker(id) {
  const v = vendoCache && vendoCache.vendos.find(x => x.id === id);
  if (!v) return;
  window.open(`vendo-sticker.html?vendo=${id}&name=${encodeURIComponent(v.name)}`, '_blank');
}

function editVendo(id) {
  const v = vendoCache && vendoCache.vendos.find(x => x.id === id);
  if (!v) return;
  const panel = document.getElementById('vendoEdit');
  panel.dataset.id = id;
  document.getElementById('vendoEditTitle').textContent = 'Edit: ' + v.name;
  document.getElementById('vendoEditName').value = v.name;
  document.getElementById('vendoEditCommission').value = v.commissionPct;
  document.getElementById('vendoEditPins').style.display = id === 0 ? 'none' : '';
  const charging = v.type === 'charging';
  document.getElementById('vendoEditCharging').style.display = charging ? '' : 'none';
  // D1/D2 are a Charging Station's I2C bus
  ['vendoEditCoinPin', 'vendoEditRelayPin'].forEach(sel => {
    [...document.getElementById(sel).options].forEach(o => {
      o.disabled = charging && (o.value === 'D1' || o.value === 'D2');
    });
  });
  if (charging) {
    document.getElementById('vendoEditPorts').value = String(v.ports || 4);
    document.getElementById('vendoEditPortHigh').value = v.portActiveHigh ? 'high' : 'low';
  }
  document.getElementById('vendoEditMainNote').style.display = id === 0 ? '' : 'none';
  if (id !== 0) {
    document.getElementById('vendoEditCoinPin').value = v.coinPin;
    document.getElementById('vendoEditRelayPin').value = v.relayPin;
    document.getElementById('vendoEditRelayHigh').value = v.relayActiveHigh ? 'high' : 'low';
    document.getElementById('vendoEditPulse').value = v.pesosPerPulse;
  }
  panel.style.display = '';
  panel.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
}

function closeVendoEdit() {
  document.getElementById('vendoEdit').style.display = 'none';
}

async function saveVendoEdit() {
  const id = parseInt(document.getElementById('vendoEdit').dataset.id, 10);
  const body = {
    id,
    name: document.getElementById('vendoEditName').value.trim(),
    commissionPct: parseInt(document.getElementById('vendoEditCommission').value, 10),
  };
  if (Number.isNaN(body.commissionPct)) body.commissionPct = -1;   // server explains the range
  if (id !== 0) {
    body.coinPin = document.getElementById('vendoEditCoinPin').value;
    body.relayPin = document.getElementById('vendoEditRelayPin').value;
    body.relayActiveHigh = document.getElementById('vendoEditRelayHigh').value === 'high';
    body.pesosPerPulse = parseInt(document.getElementById('vendoEditPulse').value, 10) || 0;
  }
  const edited = vendoCache && vendoCache.vendos.find(x => x.id === id);
  if (edited && edited.type === 'charging') {
    body.ports = parseInt(document.getElementById('vendoEditPorts').value, 10);
    body.portActiveHigh = document.getElementById('vendoEditPortHigh').value === 'high';
  }
  if (await postVendo('/api/admin/vendos/update', body)) {
    closeVendoEdit();
    loadVendos();
  }
}

async function repairVendo(id) {
  const v = vendoCache && vendoCache.vendos.find(x => x.id === id);
  if (!v || !confirm(`Re-pair "${v.name}"?\nIts current pairing stops working right away; enter the new code in the box's Setup Wizard.`)) return;
  const data = await postVendo('/api/admin/vendos/repair', { id });
  if (data) {
    alert(`New pairing code for "${v.name}":\n\n${data.code}\n\nValid for 15 minutes.`);
    loadVendos();
  }
}

async function removeVendo(id) {
  const v = vendoCache && vendoCache.vendos.find(x => x.id === id);
  if (!v || !confirm(`Remove "${v.name}"?\nIt stops working at once. Its sales history is kept.` +
                     (v.boxTotal ? `\n\nIts coin box still shows ₱${v.boxTotal} - mark it Collected first.` : ''))) return;
  if (await postVendo('/api/admin/vendos/remove', { id })) loadVendos();
}

async function loadCollections() {
  const tbody = document.querySelector('#collectionsTable tbody');
  if (!tbody) return;
  try {
    const res = await adminFetch('/api/admin/vendos/collections');
    const rows = await res.json();
    if (!res.ok) return;
    tbody.innerHTML = rows.length ? rows.map(c => `
      <tr><td>${c.at ? new Date(c.at * 1000).toLocaleString() : '(clock not set)'}</td>
      <td>${escHtml(c.name)}</td><td>₱${c.amount}</td><td>${escHtml(c.admin)}</td></tr>`).join('')
      : '<tr><td colspan="4" style="color:#aaa;">No collections yet.</td></tr>';
  } catch (err) {
    console.error('Collections load failed', err);
  }
}

// ---- Settings > Charging (v2) -------------------------------------------
function addChargeRateRow(r) {
  const tr = document.createElement('tr');
  tr.innerHTML = `<td><input type="number" class="charge-peso" min="1" max="1000" step="1" value="${r ? r.peso : ''}"></td>
    <td><input type="number" class="charge-minutes" min="1" max="1440" step="1" value="${r ? r.minutes : ''}"></td>
    <td><button type="button" class="btn btn-danger btn-inline" onclick="this.closest('tr').remove()">Remove</button></td>`;
  document.getElementById('chargeRatesBody').appendChild(tr);
}

function collectChargeRates() {
  return [...document.querySelectorAll('#chargeRatesBody tr')].map(tr => ({
    peso: parseInt(tr.querySelector('.charge-peso').value, 10) || 0,
    minutes: parseInt(tr.querySelector('.charge-minutes').value, 10) || 0,
  })).filter(r => r.peso || r.minutes);
}

function chargeRateProblems(rates, maxMinutes) {
  const problems = [];
  const seen = new Set();
  if (rates.length > 10) problems.push('Charging: up to 10 prices.');
  rates.forEach(r => {
    if (r.peso < 1 || r.peso > 1000) problems.push(`Charging: ₱${r.peso} - price must be 1-1000.`);
    if (r.minutes < 1 || r.minutes > 1440) problems.push(`Charging: ₱${r.peso} - minutes must be 1-1440.`);
    if (seen.has(r.peso)) problems.push(`Charging: ₱${r.peso} is listed twice.`);
    seen.add(r.peso);
  });
  if (maxMinutes < 10 || maxMinutes > 1440) problems.push('Charging: max time per port must be 10-1440 minutes.');
  return problems;
}

// ---- Coin Slot pins per board (v2: NodeMCU or ESP32) ---------------------
// The unit says which pin labels its board has (D1.. on a NodeMCU, G13.. on
// an ESP32). A saved pin the board doesn't have stays visible, marked.
function fillPinChoices(choices, coinPin, relayPin) {
  const pins = Array.isArray(choices) && choices.length ? choices : ['D1', 'D2', 'D5', 'D6', 'D7'];
  const build = (sel, value, withNone) => {
    const opts = pins.slice();
    let html = opts.map(p => `<option value="${p}">${p}</option>`).join('');
    if (withNone) html += '<option value="none">None (acceptor always on)</option>';
    if (value && value !== 'none' && !opts.includes(value)) {
      html = `<option value="${escHtml(value)}">${escHtml(value)} (not valid on this board)</option>` + html;
    }
    sel.innerHTML = html;
    sel.value = value || pins[0];
  };
  build(document.getElementById('coinPin'), coinPin, false);
  build(document.getElementById('relayPin'), relayPin, true);
}

// ---- Backup & Restore (v2) --------------------------------------------
function setBackupStatus(kind, message) {
  const el = document.getElementById('backupStatus');
  el.className = 'save-status ' + kind;
  el.textContent = message;
  el.style.display = message ? '' : 'none';
}

const BACKUP_ERRORS = {
  bad_backup: 'That file is damaged or is not a ZxheiFi backup. Nothing was changed.',
  not_a_backup: 'That file is not a ZxheiFi backup. Nothing was changed.',
  backup_too_large: 'That file is too big (over 1 MB) to be a ZxheiFi backup. Nothing was changed.',
  no_file: 'Choose a backup file first.',
  not_saved_low_memory: 'The unit ran out of storage while receiving the file. Restart it and try again.',
  restore_in_progress: 'A restore is already running - wait for the unit to restart.',
  super_admin_required: 'Only a super admin can do this.',
};

async function downloadBackup() {
  setBackupStatus('pending', 'Preparing the backup...');
  try {
    const res = await adminFetch('/api/admin/backup');
    if (!res.ok) {
      const data = await res.json().catch(() => ({}));
      throw new Error(BACKUP_ERRORS[data.error] || data.error || ('HTTP ' + res.status));
    }
    const blob = await res.blob();
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `zxheifi-backup-${ymd(new Date())}.json`;
    a.click();
    URL.revokeObjectURL(a.href);
    setBackupStatus('ok', `✅ Backup saved (${Math.round(blob.size / 1024)} KB). Keep it somewhere safe and private.`);
  } catch (err) {
    setBackupStatus('error', '❌ Backup failed: ' + err.message);
  }
}

async function restoreBackup() {
  const input = document.getElementById('restoreFile');
  const file = input.files && input.files[0];
  if (!file) {
    setBackupStatus('error', BACKUP_ERRORS.no_file);
    return;
  }
  if (file.size > 1048576) {
    setBackupStatus('error', BACKUP_ERRORS.backup_too_large);
    return;
  }
  let info;
  try {
    info = JSON.parse(await file.text());
  } catch (err) {
    setBackupStatus('error', BACKUP_ERRORS.bad_backup);
    return;
  }
  if (!info || info.zxheifiBackup !== 1 || typeof info.files !== 'object') {
    setBackupStatus('error', BACKUP_ERRORS.not_a_backup);
    return;
  }
  const when = info.createdAt ? new Date(info.createdAt * 1000).toLocaleString() : 'unknown date';
  const board = info.board === 'esp32' ? 'ESP32' : (info.board === 'esp8266' ? 'NodeMCU' : (info.board || '?'));
  if (!confirm(`Restore this backup?\n\nMade on: ${when}\nBoard: ${board} - firmware ${info.fw || '?'}\n` +
               `${Object.keys(info.files).length} files\n\nEverything on this unit (settings, vouchers, sales, ` +
               'coin boxes...) is replaced by the backup, then the unit restarts.')) return;
  setBackupStatus('pending', 'Uploading and checking the backup...');
  try {
    const form = new FormData();
    form.append('backup', file, file.name);
    const res = await adminFetch('/api/admin/restore', { method: 'POST', body: form });
    const data = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(BACKUP_ERRORS[data.error] || data.error || ('HTTP ' + res.status));
    setBackupStatus('ok', `✅ Restored ${data.files.length} files. The unit is restarting - this page reloads in 20 seconds.`);
    setTimeout(() => location.reload(), 20000);
  } catch (err) {
    setBackupStatus('error', '❌ Not restored: ' + err.message);
  }
}

