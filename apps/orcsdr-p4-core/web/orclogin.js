/* OrcSDR node login for the OrcSDR Web page (docs/hardware/WEB_LAYER_DESIGN.md sections 3 and 14).
 *
 * The page POSTs its controls without any login. This script is loaded before the page's own scripts and wraps fetch(): a POST to /api/v1/
 * is held until the browser has a session, then sent with a signature the node verifies. The password never leaves the browser; the node never
 * sends a reusable token. crypto.subtle is not available to a page served over plain HTTP from a LAN address, so SHA-256, HMAC and PBKDF2 are
 * implemented here (pure JavaScript, no dependencies). The same functions run under Node for the protocol test (tools/orclogin_test.js).
 *
 *   key         = PBKDF2-HMAC-SHA256(password, salt, iterations, 32)
 *   login proof = HMAC(key, "login|" + nonce + "|admin")        session key = HMAC(key, "session|" + nonce)
 *   request sig = HMAC(session key, seq "\n" METHOD "\n" path-and-query "\n" hex(SHA256(body)))
 */
(function (root) {
  'use strict';
  const K = new Uint32Array([
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2]);

  function sha256(data) {   // Uint8Array -> Uint8Array(32)
    const h = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
    const n = data.length, total = ((n + 9 + 63) >> 6) << 6, buf = new Uint8Array(total), w = new Uint32Array(64);
    buf.set(data); buf[n] = 0x80;
    const dv = new DataView(buf.buffer); dv.setUint32(total - 8, Math.floor(n / 0x20000000)); dv.setUint32(total - 4, (n << 3) >>> 0);
    for (let off = 0; off < total; off += 64) {
      for (let i = 0; i < 16; i++) w[i] = dv.getUint32(off + 4 * i);
      for (let i = 16; i < 64; i++) {
        const a = w[i - 15], b = w[i - 2];
        const s0 = ((a >>> 7) | (a << 25)) ^ ((a >>> 18) | (a << 14)) ^ (a >>> 3), s1 = ((b >>> 17) | (b << 15)) ^ ((b >>> 19) | (b << 13)) ^ (b >>> 10);
        w[i] = (w[i - 16] + s0 + w[i - 7] + s1) | 0;
      }
      let a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
      for (let i = 0; i < 64; i++) {
        const S1 = ((e >>> 6) | (e << 26)) ^ ((e >>> 11) | (e << 21)) ^ ((e >>> 25) | (e << 7)), ch = (e & f) ^ (~e & g);
        const t1 = (hh + S1 + ch + K[i] + w[i]) | 0;
        const S0 = ((a >>> 2) | (a << 30)) ^ ((a >>> 13) | (a << 19)) ^ ((a >>> 22) | (a << 10)), mj = (a & b) ^ (a & c) ^ (b & c);
        const t2 = (S0 + mj) | 0;
        hh = g; g = f; f = e; e = (d + t1) | 0; d = c; c = b; b = a; a = (t1 + t2) | 0;
      }
      h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    const out = new Uint8Array(32), ov = new DataView(out.buffer);
    for (let i = 0; i < 8; i++) ov.setUint32(4 * i, h[i]);
    return out;
  }

  function hmac(key, msg) {
    if (key.length > 64) key = sha256(key);
    const ipad = new Uint8Array(64 + msg.length), opad = new Uint8Array(96);
    for (let i = 0; i < 64; i++) { const k = i < key.length ? key[i] : 0; ipad[i] = k ^ 0x36; opad[i] = k ^ 0x5c; }
    ipad.set(msg, 64); opad.set(sha256(ipad), 64);
    return sha256(opad);
  }

  function pbkdf2(password, salt, iterations) {   // 32-byte output = one block
    const s = new Uint8Array(salt.length + 4); s.set(salt); s[salt.length + 3] = 1;
    let u = hmac(password, s); const t = Uint8Array.from(u);
    for (let i = 1; i < iterations; i++) { u = hmac(password, u); for (let j = 0; j < 32; j++) t[j] ^= u[j]; }
    return t;
  }

  const enc = new TextEncoder();
  const hex = b => Array.from(b, x => x.toString(16).padStart(2, '0')).join('');
  const unhex = s => Uint8Array.from(s.match(/../g) || [], x => parseInt(x, 16));
  const cat = (...p) => { const o = new Uint8Array(p.reduce((n, x) => n + x.length, 0)); let k = 0; for (const x of p) { o.set(x, k); k += x.length; } return o; };

  function loginProof(key, nonce) { return hmac(key, cat(enc.encode('login|'), nonce, enc.encode('|admin'))); }
  function sessionKey(key, nonce) { return hmac(key, cat(enc.encode('session|'), nonce)); }
  function deviceProof(key, nonce) { return hmac(key, cat(enc.encode('device|'), nonce)); }
  function sign(sessionKeyBytes, seq, method, target, body) {
    return hex(hmac(sessionKeyBytes, enc.encode(seq + '\n' + method + '\n' + target + '\n' + hex(sha256(body)))));
  }

  const api = { sha256, hmac, pbkdf2, hex, unhex, enc, cat, loginProof, sessionKey, deviceProof, sign };
  if (typeof module !== 'undefined' && module.exports) { module.exports = api; return; }
  if (typeof window === 'undefined' || !window.fetch) return;

  // ---- browser side: session handling and the fetch wrapper ---------------------------------------------------
  const nativeFetch = window.fetch.bind(window);
  let session = null;   // { sid, key: Uint8Array, seq }
  try { const s = JSON.parse(sessionStorage.getItem('orcsession') || 'null'); if (s) session = { sid: s.sid, key: unhex(s.key), seq: s.seq }; } catch (e) { /* no storage */ }
  const keep = () => { try { sessionStorage.setItem('orcsession', session ? JSON.stringify({ sid: session.sid, key: hex(session.key), seq: session.seq }) : ''); } catch (e) { /* ignore */ } };
  let chain = Promise.resolve(), loginWait = null;

  // "Remember me" keeps the PBKDF2 key (never the password) in this browser's localStorage, so the next visit signs in silently. The key
  // is only good for this node while its password and salt are unchanged; a change of password makes it fail once and it is dropped.
  const remembered = () => { try { return JSON.parse(localStorage.getItem('orcremember') || 'null'); } catch (e) { return null; } };
  const remember = (key, salt) => { try { localStorage.setItem('orcremember', JSON.stringify({ key: hex(key), salt })); } catch (e) { /* no storage */ } };
  const forget = () => { try { localStorage.removeItem('orcremember'); } catch (e) { /* ignore */ } };

  // Browser audio: an AudioContext may only start from a user gesture. The login button is one; after a silent (remembered) login the first
  // tap or key press does it. This only ever touches the browser's own audio: nothing is sent to the node, so the board's speaker is left alone.
  // If the node is not already playing for the browser and its receiver sits in the FM broadcast band, the node's audio session is started on that
  // station first (a signed request, through the wrapper below); on other bands the audio starts with the screen (AM, shortwave). Nothing here sets the board's volume.
  // The mode follows the frequency the way the page's band table does (broadcast FM wide, airband and military AM, other VHF/UHF narrow FM, below 30 MHz AM).
  const audioMode = hz => hz >= 87500000 && hz < 108000000 ? 'wfm' : hz === 1090000000 ? null : hz >= 118000000 && hz < 137000000 || hz >= 225000000 && hz < 400000000 || hz < 30000000 ? 'am' : 'nfm';
  const say = m => { try { if (typeof toast === 'function') toast(m); } catch (e) { /* no toast */ } };
  async function startAudio(manual) {
    try {
      if (typeof Listen === 'undefined' || Listen.wanted) return;
      if (typeof Chan !== 'undefined' && Chan.active) { if (manual) say('This screen already plays its own audio in the browser'); return; }
      await Listen.prime();
      const get = async p => (await nativeFetch(p, { cache: 'no-store' })).json();
      const fm = await get('/api/v1/fm');
      if (!fm.browser_audio || !fm.browser_audio.available) { if (manual) say('This firmware has no browser audio'); return; }
      if (!fm.playing) {
        const rx = ((await get('/api/v1/receivers')).receivers || []).find(r => r.present && r.center_hz);
        if (!rx) { if (manual) say('No receiver is streaming'); return; }
        // The Home screen already knows the right mode for the band (USB/LSB on HF, AM, NFM, WFM): ask it, and fall back to the page's own band rule.
        let mode = null;
        try { mode = String(((await get('/api/v1/freq')).records[0] || {}).mode || '').toLowerCase(); } catch (e) { /* use the fallback */ }
        if (!['wfm', 'nfm', 'am', 'usb', 'lsb'].includes(mode)) mode = audioMode(rx.center_hz);
        if (!mode) { if (manual) say('No audio at this frequency'); return; }
        const r = await window.fetch('/api/v1/fm?action=start&index=' + rx.index + '&hz=' + rx.center_hz + '&sps=2400000&volume=30&tau=75&mode=' + mode, { method: 'POST' });
        if (!r.ok) { if (manual) say('Browser audio could not start: ' + ((await r.json().catch(() => ({}))).error || r.status)); return; }
      }
      if (!Listen.wanted) Listen.toggle();
    } catch (e) { /* the page's audio is optional */ }
  }

  // The page's "Listen here" button: renamed, and pressing it starts the node's audio for whatever the receiver is tuned to, then plays it here.
  function audioButton() {
    const b = document.getElementById('listen');
    if (!b) return;
    const names = { 'Listen here': 'Start Browser Audio', 'Stop listening': 'Stop Browser Audio' };
    const rename = () => { const t = b.textContent.trim(); if (names[t]) b.textContent = names[t]; };
    rename();
    b.title = 'Play the node\'s audio in this browser (the board\'s own speaker is not touched)';
    new MutationObserver(rename).observe(b, { childList: true, characterData: true, subtree: true });
    document.addEventListener('click', e => {
      if (!e.target.closest || !e.target.closest('#listen')) return;
      if (typeof Listen !== 'undefined' && !Listen.wanted) { e.stopImmediatePropagation(); e.preventDefault(); startAudio(true); }
    }, true);
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', audioButton); else audioButton();
  function armAudio(gestured) {
    if (gestured) return startAudio();
    const once = () => { removeEventListener('pointerdown', once, true); removeEventListener('keydown', once, true); startAudio(); };
    addEventListener('pointerdown', once, true); addEventListener('keydown', once, true);
  }

  function ask(message, defaultNotice) {   // the login screen: resolves with { password, remember }; there is no way past it without logging in
    return new Promise(resolve => {
      const box = document.createElement('div');
      box.style.cssText = 'position:fixed;inset:0;background:#000;z-index:99999;display:flex;align-items:center;justify-content:center;font:16px sans-serif';
      box.innerHTML = '<form style="background:#0b1620;border:2px solid #34d399;border-radius:8px;padding:20px;color:#e5e7eb;min-width:280px">' +
        '<div style="font-size:18px;margin-bottom:8px">OrcSDR login</div><div id="orcmsg" style="margin-bottom:10px;color:#fbbf24"></div>' +
        '<div style="margin-bottom:6px">User: <b>admin</b></div><input id="orcpw" type="password" autocomplete="current-password" style="width:100%;box-sizing:border-box;padding:8px;margin-bottom:10px">' +
        '<label style="display:block;margin-bottom:12px"><input id="orcrem" type="checkbox" style="margin-right:6px">Remember me on this browser</label>' +
        '<div style="text-align:right"><button type="submit" style="padding:6px 12px">Log in</button></div></form>';
      box.querySelector('#orcmsg').textContent = message + (defaultNotice ? ' This OrcSDR still uses the factory password: change it with RTL_WEB PASSWORD over serial.' : '');
      document.body.appendChild(box);
      // The page's own window-level keydown handler swallows every printable key except in a <select> and feeds it to the canvas, so keys typed here
      // must not leave the box (otherwise the password field stays empty).
      for (const type of ['keydown', 'keypress', 'keyup']) box.addEventListener(type, e => e.stopPropagation());
      const input = box.querySelector('#orcpw'); input.focus();
      box.querySelector('form').addEventListener('submit', e => { e.preventDefault(); const v = { password: input.value, remember: box.querySelector('#orcrem').checked }; box.remove(); resolve(v); });
    });
  }

  // silent: try the remembered key only (no screen). Otherwise ask until the node accepts the password; an unreachable node is retried.
  async function login(silent) {
    let message = 'Log in to control this OrcSDR.';
    for (;;) {
      let c;
      try { c = await (await nativeFetch('/api/v1/auth/challenge', { cache: 'no-store' })).json(); }
      catch (e) { if (silent) return false; message = 'Cannot reach the OrcSDR. Retrying...'; await new Promise(r => setTimeout(r, 2000)); continue; }
      let key, keepIt = false, gestured = false;
      if (silent) {
        const m = remembered();
        if (!m || m.salt !== c.salt) return false;
        key = unhex(m.key);
      } else {
        const a = await ask(message, c.default_password);
        await new Promise(r => setTimeout(r, 30));   // let the screen paint before the key derivation
        key = pbkdf2(enc.encode(a.password), unhex(c.salt), c.iterations); keepIt = a.remember; gestured = true;
      }
      const nonce = unhex(c.nonce);
      const r = await nativeFetch('/api/v1/auth/login', { method: 'POST', body: 'nonce=' + c.nonce + '&proof=' + hex(loginProof(key, nonce)) });
      const j = await r.json().catch(() => ({}));
      if (r.ok && j.ok) {
        if (j.device !== hex(deviceProof(key, nonce))) { if (silent) return false; message = 'The OrcSDR did not prove it knows the password.'; continue; }
        session = { sid: j.session, key: sessionKey(key, nonce), seq: 0 }; keep();
        if (keepIt) remember(key, c.salt); else if (!silent) forget();
        armAudio(gestured);
        return true;
      }
      if (silent) { forget(); return false; }   // the remembered key no longer matches (password changed): ask
      message = j.error === 'login_failed' ? 'Wrong password.' : j.error && j.error.indexOf('throttled') === 0 ? 'Too many tries: wait a few seconds. ' + j.error : 'Login failed: ' + (j.error || r.status);
    }
  }
  const needLogin = () => { loginWait = loginWait || login(true).then(ok => ok || login(false)).finally(() => { loginWait = null; }); return loginWait; };

  async function signedFetch(input, init) {
    const url = new URL(typeof input === 'string' ? input : input.url, location.href);
    const body = init && typeof init.body === 'string' ? enc.encode(init.body) : new Uint8Array(0);
    for (let tries = 0; tries < 2; tries++) {
      if (!session) await needLogin();
      const seq = ++session.seq, target = url.pathname + url.search;
      const headers = Object.assign({}, init && init.headers, { 'X-Orc-Session': session.sid, 'X-Orc-Seq': String(seq), 'X-Orc-Sig': sign(session.key, seq, 'POST', target, body) });
      keep();
      const r = await nativeFetch(input, Object.assign({}, init, { headers }));
      if (r.status !== 401) return r;
      const j = await r.clone().json().catch(() => ({}));
      if (j.error !== 'session_required' && j.error !== 'bad_sequence') return r;
      session = null; keep();   // expired or out of step: log in again once
    }
    return new Response('{"ok":false,"error":"login_failed"}', { status: 401, headers: { 'Content-Type': 'application/json' } });
  }

  window.fetch = function (input, init) {
    const url = new URL(typeof input === 'string' ? input : input.url, location.href);
    const method = ((init && init.method) || (typeof input !== 'string' && input.method) || 'GET').toUpperCase();
    if (method !== 'POST' || url.origin !== location.origin || url.pathname.indexOf('/api/v1/') !== 0 || url.pathname.indexOf('/api/v1/auth/') === 0) return nativeFetch(input, init);
    const run = chain.then(() => signedFetch(input, init));   // one signed request at a time keeps the sequence numbers in order
    chain = run.catch(() => {});
    return run;
  };
  // First connect always goes to the login: with no session the login screen comes up as soon as the page exists.
  const gate = () => { if (!session) needLogin(); };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', gate); else gate();
  if (session) armAudio(false);   // a session carried over a reload: the first tap starts the browser audio

  async function logout() {
    try { if (session) await signedFetch('/api/v1/auth/logout', { method: 'POST' }); } catch (e) { /* ends locally anyway */ }
    session = null; keep(); forget(); location.reload();
  }
  const logoutButton = () => {
    const b = document.createElement('button');
    b.textContent = 'Log out'; b.type = 'button';
    b.style.cssText = 'position:fixed;right:6px;bottom:6px;z-index:9000;opacity:.55;padding:2px 8px;font:12px sans-serif;background:#0b1620;color:#e5e7eb;border:1px solid #34d399;border-radius:4px;cursor:pointer';
    b.addEventListener('click', logout);
    for (const type of ['keydown', 'keypress', 'keyup']) b.addEventListener(type, e => e.stopPropagation());
    document.body.appendChild(b);
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', logoutButton); else logoutButton();
  window.OrcLogin = { logout };
})(typeof window !== 'undefined' ? window : globalThis);
