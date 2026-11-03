// Decoders: POCSAG, FT8 and SSTV can run on the node or in this browser, and either way the events land in the node's
// history (GET /api/v1/decode/events) so a page opened later still shows them.
//
//   DecoderFeed   turns the demodulated audio (48 kS/s, from the page's ChannelDemod) into what a decoder takes and runs the
//                 FT8 slot clock. It is independent of the page so a Node test can drive it with the same code.
//   Decoders      the panel, the Node/Browser/Auto choice and the polling of the node's history.
'use strict';
(function (root) {

const KIND = { pocsag: 1, ft8: 2, sstv: 3, adsb: 4, acars: 5, p25: 6, js8: 7, lora: 8 };
const isSlot = k => k === KIND.ft8 || k === KIND.js8;   // FT8 and JS8 decode whole 15 s slots
const NODE_ONLY = { adsb: 'needs 2 MS/s of IQ, far more than a page can be sent', lora: 'needs a 250 kHz channel, more IQ than a page can be sent' };
// The decoders that take the receiver's IQ. The node sends a page about 0.6 MB/s at most (Phase 2), so the channel is the narrowest the
// signal fits: 240 kS/s (0.48 MB/s). `up` is how many times the page interpolates it to reach the decoder's rate (POCSAG and P25 want
// 960 kS/s; their signals are 12.5 kHz wide). `at` is where the decoder wants the carrier; the channel puts it sps/6 above DC.
const IQ_PLAN = { pocsag: { sps: 240000, up: 4, at: 0 }, p25: { sps: 240000, up: 4, at: 0 }, acars: { sps: 240000, up: 1, at: 60000 } };

// Rotates 8-bit IQ by a fixed frequency (moves a carrier from `from` to `to`), interpolates it `up` times (linearly) and quantizes it back to 8 bits.
class IqShift {
  constructor(rate, from, to, up = 1) {
    const w = 2 * Math.PI * (to - from) / rate;
    this.wr = Math.cos(w); this.wi = Math.sin(w); this.pr = 1; this.pi = 0; this.up = up;
    this.still = to === from && up === 1; this.lr = 0; this.li = 0;
  }
  process(u8) {
    if (this.still) return u8;
    const n = u8.length >> 1, up = this.up, out = new Uint8Array(n * 2 * up);
    let pr = this.pr, pi = this.pi, lr = this.lr, li = this.li, o = 0;
    const wr = this.wr, wi = this.wi;
    const put = (x, y) => {
      const a = x + 127.5, b = y + 127.5;
      out[o++] = a < 0 ? 0 : a > 255 ? 255 : (a + 0.5) | 0;
      out[o++] = b < 0 ? 0 : b > 255 ? 255 : (b + 0.5) | 0;
    };
    for (let k = 0; k < n; k++) {
      const re = u8[2 * k] - 127.5, im = u8[2 * k + 1] - 127.5;
      const xr = re * pr - im * pi, xi = re * pi + im * pr;
      for (let m = 1; m <= up; m++) { const f = m / up; put(lr + (xr - lr) * f, li + (xi - li) * f); }
      lr = xr; li = xi;
      const t = pr * wr - pi * wi; pi = pr * wi + pi * wr; pr = t;
    }
    const g = 1 / Math.hypot(pr, pi);   // keep the phasor on the unit circle
    this.pr = pr * g; this.pi = pi * g; this.lr = lr; this.li = li;
    return out;
  }
}

class DecoderFeed {
  // port: anything with postMessage(msg, transfer) that speaks the protocol of decoder_worker.js.
  // opt: { options, inject (arm FT8 at once, with epochMs as slot 0), epochMs, now() }
  constructor(kind, port, opt = {}) {
    this.kind = kind; this.port = port; this.opt = opt;
    this.slotMs = /ft4/.test(opt.options || '') ? 7500 : 15000;
    this.now = opt.now || (() => Date.now());
    this.inject = !!opt.inject;
    this.boundary = this.inject ? (opt.epochMs !== undefined ? opt.epochMs : 1700000000000) : (Math.floor(this.now() / this.slotMs) + 1) * this.slotMs;
    this.armed = !isSlot(kind);
    this.acc = 0; this.n = 0;
    this.shift = opt.iq ? new IqShift(opt.iq.sps, opt.iq.offset, opt.iq.at, opt.iq.up || 1) : null;   // IQ decoders: opt.iq = {sps, offset (carrier in the stream), at (where the decoder wants it), up (interpolation)}
    this.samples = 0;
    this.slotSamples = this.slotMs * 12;   // 12 kS/s
    this.inSlot = 0;
  }
  start() { this.port.postMessage({ t: 'init', kind: this.kind, options: this.opt.options || '' }); }
  // f32: 48 kS/s mono float in -1..1 (the page's demodulated audio)
  push48(f32) {
    if (this.kind === KIND.pocsag) return;
    const pcm = new Int16Array(isSlot(this.kind) ? (f32.length >> 2) + 1 : f32.length);
    let m = 0;
    for (let i = 0; i < f32.length; i++) {
      const v = f32[i] > 1 ? 1 : f32[i] < -1 ? -1 : f32[i];
      const s = Math.trunc(v * 30000);                    // the node's (int16_t)(v * 30000.0f)
      if (this.kind === KIND.sstv) { pcm[m++] = s; continue; }
      this.acc += s;                                      // FT8: 48 -> 12 kS/s by averaging four samples
      if (++this.n === 4) { pcm[m++] = Math.trunc(this.acc / 4); this.acc = 0; this.n = 0; }
    }
    if (isSlot(this.kind)) {
      if (!this.armed) {
        if (!this.inject && this.now() < this.boundary) return;   // before the first slot boundary: audio is discarded
        this.port.postMessage({ t: 'cmd', cmd: 'slot_begin', arg: this.boundary });
        this.armed = true; this.inSlot = 0;
      }
      this.sendSlots(pcm, m);
    } else if (m) this.send(pcm.subarray(0, m));
  }
  // FT8: feed no further than the end of the slot, then tell the decoder when the next one starts. The decoder cuts slots by
  // sample count; starting each from the wall clock keeps DT right however far the sample clock drifts.
  sendSlots(pcm, m) {
    for (let off = 0; off < m;) {
      const n = Math.min(this.slotSamples - this.inSlot, m - off);
      this.send(pcm.subarray(off, off + n));
      off += n; this.inSlot += n;
      if (this.inSlot >= this.slotSamples) {
        this.inSlot = 0;
        this.boundary = this.inject ? this.boundary + this.slotMs : Math.round(this.now() / this.slotMs) * this.slotMs;
        this.port.postMessage({ t: 'cmd', cmd: 'slot_begin', arg: this.boundary });
      }
    }
  }
  pushIq(u8) { this.send(this.shift ? this.shift.process(u8) : u8); }
  send(view) {
    this.samples += view.length;
    const copy = view.slice().buffer;
    this.port.postMessage({ t: 'feed', data: copy }, [copy]);
  }
  finish() { this.port.postMessage({ t: 'cmd', cmd: 'finish', arg: 0 }); }
  close() { this.port.postMessage({ t: 'close' }); }
  onMessage() {}   // kept so a caller can forward the worker's messages; slots are realigned in sendSlots
}

root.DecoderFeed = DecoderFeed;
root.IqShift = IqShift;
if (typeof document === 'undefined' || !root.m5app) return;

// ---- panel ------------------------------------------------------------------------------------------------------
const { MOCK, API, S, Chan, post, jget, toast } = root.m5app;
const $ = id => document.getElementById(id);
const DEFAULTS = {
  lora: { hz: 906875000, mode: 'USB', note: 'Meshtastic LongFast, US slot 20 (SF11, 250 kHz); the node takes 960 kS/s of IQ' },
  js8: { hz: 14078000, mode: 'USB', note: 'JS8 dial frequency (20 m); audio is the sideband above it' },
  ft8: { hz: 14074000, mode: 'USB', note: 'FT8 dial frequency; audio is the sideband above it' },
  sstv: { hz: 14230000, mode: 'USB', note: 'SSTV calling frequency' },
  pocsag: { hz: 152840000, mode: 'USB', note: 'POCSAG carrier; the node takes 960 kS/s of IQ' },
  adsb: { hz: 1090000000, mode: 'USB', note: 'Mode S transponder frequency; the node takes 2.048 MS/s of IQ' },
  p25: { hz: 453925000, mode: 'USB', note: 'P25 Phase I control channel; the node takes 960 kS/s of IQ' },
  acars: { hz: 131550000, mode: 'USB', note: 'ACARS channel (131.550 MHz in the US); AM, 2400 baud MSK' },
};
const CLOCK_NOTE = 'FT8 needs the clock right to within about a second.';

const D = {
  open: false, events: [], seen: new Set(), after: 0, status: null, where: null,
  worker: null, feed: null, pendingIngest: [], image: null, lastImageId: 0, timer: null, kind: 'ft8',
  cfg() {
    try { return JSON.parse(localStorage.getItem('orcsdr.decoders') || '{}'); } catch { return {}; }
  },
  save() {
    try { localStorage.setItem('orcsdr.decoders', JSON.stringify({ kind: this.kind, hz: +$('dec-hz').value, mode: $('dec-mode').value, where: $('dec-where').value })); } catch {}
  },
  build() {
    const el = document.createElement('div');
    el.id = 'dec'; el.hidden = true;
    el.innerHTML = `
      <div class="dh"><b>Decoders</b><button id="dec-x" title="Close">x</button></div>
      <div class="dr">
        <label>Decoder <select id="dec-kind"><option value="ft8">FT8</option><option value="js8">JS8</option><option value="lora">LoRa</option><option value="sstv">SSTV</option><option value="pocsag">POCSAG</option><option value="adsb">ADS-B</option><option value="acars">ACARS</option><option value="p25">P25</option></select></label>
        <label>Frequency (Hz) <input id="dec-hz" type="number" step="1" style="width:9em"></label>
        <label id="dec-mode-l">Sideband <select id="dec-mode"><option>USB</option><option>LSB</option></select></label>
        <label>Run on <select id="dec-where"><option value="auto">Auto</option><option value="node">Node</option><option value="browser">Browser</option></select></label>
      </div>
      <div class="dr"><button id="dec-go">Start</button><button id="dec-stop">Stop</button><button id="dec-clear" title="Forget the events shown here">Clear list</button></div>
      <div id="dec-stat" class="dim">Not running</div>
      <div id="dec-air" hidden></div>
      <canvas id="dec-img" width="320" height="240" hidden></canvas>
      <div id="dec-list"></div>`;
    document.body.appendChild(el);
    $('dec-x').onclick = () => this.toggle(false);
    $('dec-kind').onchange = () => { this.kind = $('dec-kind').value; this.defaults(); this.save(); this.render(); };
    $('dec-hz').onchange = () => this.save();
    $('dec-mode').onchange = () => this.save();
    $('dec-where').onchange = () => this.save();
    $('dec-go').onclick = () => this.start();
    $('dec-stop').onclick = () => this.stop();
    $('dec-clear').onclick = () => { this.events = []; this.render(); };
    const c = this.cfg();
    this.kind = KIND[c.kind] ? c.kind : 'ft8';
    $('dec-kind').value = this.kind;
    this.defaults(c);
    if (c.where) $('dec-where').value = c.where;
    $('dectoggle').onclick = () => this.toggle();
  },
  defaults(saved) {
    const d = DEFAULTS[this.kind];
    $('dec-hz').value = saved && saved.hz && saved.kind === this.kind ? saved.hz : d.hz;
    $('dec-mode').value = saved && saved.mode && saved.kind === this.kind ? saved.mode : d.mode;
    $('dec-hz').title = d.note;
    $('dec-mode-l').style.display = this.kind === 'ft8' || this.kind === 'js8' || this.kind === 'sstv' ? '' : 'none';
    $('dec-air').hidden = this.kind !== 'adsb';
    $('dec-img').hidden = this.kind !== 'sstv' || !this.lastImageId;
  },
  toggle(on) {
    this.open = on === undefined ? !this.open : on;
    $('dec').hidden = !this.open;
    $('dectoggle').classList.toggle('on', this.open);
    if (this.open) { this.poll(); }
    clearInterval(this.timer);
    if (this.open) this.timer = setInterval(() => this.poll(), 1500);
  },
  setStat(text, bad) { const e = $('dec-stat'); e.textContent = text; e.className = bad ? 'bad' : 'dim'; },
  async start() {
    this.save();
    const kind = this.kind, hz = +$('dec-hz').value, mode = $('dec-mode').value;
    if (!(hz >= 24000000 || ((kind === 'ft8' || kind === 'js8' || kind === 'sstv') && hz >= 100000))) { toast('Enter a frequency in Hz'); return; }
    await this.stop(true);
    let where = $('dec-where').value;
    let st = null;
    if (!MOCK) { try { st = await jget('/api/v1/decode/status'); } catch {} }
    if (NODE_ONLY[kind] && where === 'browser') { toast(kind.toUpperCase() + ' runs on the node only: it ' + NODE_ONLY[kind]); return; }
    if (where === 'auto') where = NODE_ONLY[kind] || IQ_PLAN[kind] ? 'node' : MOCK ? 'browser' : 'node';   // raw IQ is worth sending to a page only when asked
    if (where === 'node') {
      if (MOCK) { toast('Mock mode has no node to decode on'); return; }
      if ((kind === 'ft8' || kind === 'js8') && !(st && st.clock_ok)) {
        const ms = Date.now();
        const r = await post(`/api/v1/time?epoch=${Math.floor(ms / 1000)}&ms=${ms % 1000}`, 'set-time');
        if (!r.ok) { this.fallback('the node has no clock and could not take this browser\'s'); where = 'browser'; }
        else toast('Node clock set from this browser. ' + CLOCK_NOTE);
      }
    }
    if (where === 'node') {
      if (Chan.active) await Chan.stop();
      const opts = kind === 'ft8' ? 'mode=ft8' : '';
      const r = await post(`/api/v1/decode/start?kind=${kind}&index=${S.rx}&hz=${hz}&mode=${mode.toLowerCase()}`, 'decode', { headers: opts ? { 'X-Orcdec-Options': opts } : {} });
      if (!r.ok) {
        if ($('dec-where').value === 'auto' && !NODE_ONLY[kind]) { this.fallback('node refused (' + (r.body && (r.body.err || r.body.error)) + ')'); where = 'browser'; }
        else { this.setStat('Node: ' + ((r.body && (r.body.err || r.body.error)) || 'failed'), true); return; }
      } else { this.where = 'node'; this.setStat('Starting on the node...'); }
    }
    if (where === 'browser') await this.startBrowser(kind, hz, mode);
    this.poll();
  },
  fallback(why) { toast('Running in the browser instead: ' + why); },
  async startBrowser(kind, hz, mode) {
    if (typeof Worker === 'undefined') { this.setStat('This browser has no Web Workers', true); return; }
    this.worker = new Worker('/m5/decoder_worker.js');
    const plan = IQ_PLAN[kind];
    const options = kind === 'ft8' ? 'mode=ft8' : kind === 'lora' || kind === 'acars' ? 'freq=' + hz : '';
    // the channel's carrier offset is known only once it has started; the shifter is built with a placeholder and set after
    this.feed = new DecoderFeed(KIND[kind], this.worker, { options });
    this.worker.onmessage = e => this.fromWorker(e.data);
    this.worker.onerror = e => this.setStat('Decoder worker failed: ' + (e.message || 'load error'), true);
    this.feed.start();
    if (MOCK) { this.where = 'browser'; this.setStat('Browser decoder ready (mock: no audio source)'); return; }
    if (plan) {
      Chan.iqOnly = true;
      const ok = await Chan.start(hz, { sps: plan.sps });
      if (!ok) { Chan.iqOnly = false; this.worker.terminate(); this.worker = this.feed = null; this.setStat('Could not open a channel on the node', true); return; }
      this.feed.shift = new IqShift(Chan.params.sps, Chan.params.offset_hz, plan.at, plan.up);
      Chan.rawTap = u8 => { if (this.feed) this.feed.pushIq(u8); };
      this.where = 'browser';
      this.setStat(`Running in the browser (${(Chan.params.sps / 1000).toFixed(0)} kS/s of IQ from the node)`);
      return;
    }
    Chan.setMode(mode, 2700);
    const ok = await Chan.start(hz);
    if (!ok) { this.worker.terminate(); this.worker = this.feed = null; this.setStat('Could not open a channel on the node', true); return; }
    Chan.tap = a => { if (this.feed) this.feed.push48(a); };
    this.where = 'browser';
    this.setStat('Running in the browser');
  },
  // Developer hook: decode a CU8 recording (300 kS/s, carrier 50 kHz above DC, as a node channel session delivers it) through
  // the browser path without a receiver, e.g. OrcDecoders.replayCu8(await (await fetch('/x.cu8')).arrayBuffer(), 'ft8').
  async replayCu8(buf, kind, mode = 'USB') {
    await this.stop(true);
    this.kind = kind; $('dec-kind').value = kind;
    this.worker = new Worker('/m5/decoder_worker.js');
    this.feed = new DecoderFeed(KIND[kind], this.worker, { options: kind === 'ft8' ? 'mode=ft8' : '', inject: true });
    this.worker.onmessage = e => this.fromWorker(e.data);
    this.feed.start();
    const demod = new root.m5app.ChannelDemod(300000, 50000, mode, 2700), data = new Uint8Array(buf);
    for (let pos = 0; pos < data.length; pos += 16384) this.feed.push48(demod.process(data.subarray(pos, Math.min(pos + 16384, data.length))));
    this.feed.finish();
    this.where = 'browser';
    this.setStat('Replaying a recording in the browser');
    await new Promise(r => setTimeout(r, 1500));
    this.render();
    return this.events.map(e => e.ev);
  },
  fromWorker(msg) {
    if (msg.t === 'error') { this.setStat(msg.error, true); return; }
    if (this.feed) this.feed.onMessage(msg);
    if (msg.t !== 'events') return;
    for (const line of msg.lines) {
      let ev; try { ev = JSON.parse(line); } catch { continue; }
      this.add({ n: 0, placement: 'browser', ev, key: JSON.stringify(ev), local: true });
      if (!MOCK) this.pendingIngest.push(line);
    }
    for (const im of msg.images) this.drawImage(im.width, im.height, new Uint16Array(im.data));
    if (this.pendingIngest.length) this.flushIngest();
  },
  async flushIngest() {
    const body = this.pendingIngest.splice(0).join('\n');
    try { await post('/api/v1/decode/ingest', 'decode', { body, headers: { 'Content-Type': 'text/plain' } }); } catch {}
  },
  async stop(quiet) {
    if (this.where === 'browser' || this.worker) {
      Chan.tap = null; Chan.rawTap = null; Chan.iqOnly = false;
      if (this.feed) this.feed.finish();
      await new Promise(r => setTimeout(r, 400));   // let the final events come back
      if (!MOCK && Chan.active) await Chan.stop();
      if (this.worker) { this.worker.terminate(); this.worker = null; }
      this.feed = null;
    }
    if (!MOCK) { try { const r = await post('/api/v1/decode/stop', 'decode'); void r; } catch {} }
    this.where = null;
    if (!quiet) { this.setStat('Stopped'); this.poll(); }
  },
  add(e) {
    const key = e.key || JSON.stringify(e.ev);
    if (this.seen.has(key)) { if (!e.local) { const old = this.events.find(x => x.key === key && x.local); if (old) { old.local = false; old.n = e.n; } } return; }
    e.key = key; this.seen.add(key);
    this.events.push(e);
    if (this.events.length > 200) { const g = this.events.shift(); this.seen.delete(g.key); }
  },
  async poll() {
    if (MOCK) { this.render(); return; }
    try {
      const st = await jget('/api/v1/decode/status');
      this.status = st;
      const r = await jget('/api/v1/decode/events?after=' + this.after);
      for (const e of r.events) this.add({ n: e.n, placement: e.placement, ev: e.ev, local: false });
      if (r.last > this.after) this.after = r.last;
      for (const e of r.events) if (e.ev && e.ev.type === 'image' && e.placement === 'node') this.fetchImage();
      if (st.active && this.where !== 'browser') {
        this.where = 'node';
        this.setStat(`Node: ${st.kind.toUpperCase()} on ${(st.hz / 1e6).toFixed(4)} MHz${st.kind === 'ft8' || st.kind === 'js8' || st.kind === 'sstv' ? ' ' + st.mode.toUpperCase() : ''}` +
          (st.kind === 'ft8' || st.kind === 'js8' ? (st.armed ? ' · waiting for the next 15 s slot' : '') : '') +
          ` · CPU ${(st.busy_permille / 10).toFixed(0)}% · dropped ${st.dropped_blocks} blocks, ${st.dropped_events} events`);
      } else if (!st.active && this.where === 'node') { this.where = null; this.setStat('Node decoder stopped'); }
      else if (this.where === 'browser' && this.feed) this.setStat(`Browser: ${this.kind.toUpperCase()} · ${(this.feed.samples / 1000).toFixed(0)} k samples fed${st.active ? ' (node also running)' : ''}`);
    } catch (e) { this.setStat('Cannot reach the node: ' + e.message, true); }
    if (this.kind === 'adsb') await this.pollAircraft();
    this.render();
  },
  async pollAircraft() {
    try {
      const a = await jget('/api/v1/decode/aircraft');
      const rows = a.aircraft.map(x => `<tr><td>${x.icao}</td><td>${x.callsign || ''}</td><td>${x.alt_ft !== undefined ? x.alt_ft : ''}</td><td>${x.speed_kts !== undefined ? x.speed_kts : ''}</td><td>${x.heading_deg !== undefined ? x.heading_deg : ''}</td><td>${x.lat !== undefined ? x.lat.toFixed(3) + ' ' + x.lon.toFixed(3) : ''}</td><td>${Math.round(x.age_ms / 1000)}s</td></tr>`).join('');
      $('dec-air').innerHTML = `<table><tr><th>ICAO</th><th>Call</th><th>Alt</th><th>kt</th><th>Hdg</th><th>Position</th><th>Age</th></tr>${rows}</table><div class="dim">${a.count} aircraft, ${a.messages} frames</div>`;
    } catch {}
  },
  async fetchImage() {
    try {
      const r = await fetch(API + '/api/v1/decode/image', { cache: 'no-store' });
      if (!r.ok) return;
      const id = +r.headers.get('X-Image-Id');
      if (id === this.lastImageId) return;
      this.lastImageId = id;
      this.drawImage(+r.headers.get('X-Width'), +r.headers.get('X-Height'), new Uint16Array(await r.arrayBuffer()));
    } catch {}
  },
  drawImage(w, h, px) {
    const c = $('dec-img'); c.hidden = false; c.width = w; c.height = h;
    const im = c.getContext('2d').createImageData(w, h);
    for (let i = 0; i < w * h; i++) {
      const v = px[i];
      im.data[4 * i] = ((v >> 11) & 31) * 255 / 31; im.data[4 * i + 1] = ((v >> 5) & 63) * 255 / 63; im.data[4 * i + 2] = (v & 31) * 255 / 31; im.data[4 * i + 3] = 255;
    }
    c.getContext('2d').putImageData(im, 0, 0);
  },
  describe(e) {
    const ev = e.ev;
    if ((ev.decoder === 'ft8' || ev.decoder === 'js8') && ev.type === 'decode') {
      const t = new Date(ev.slot_ms).toISOString().substr(11, 8);
      return `${t}  ${ev.snr_db === undefined ? '  --' : String(ev.snr_db).padStart(4)} dB  ${(ev.dt_ms / 1000).toFixed(1).padStart(5)} s  ${String(ev.hz).padStart(5)} Hz  ${ev.message}`;
    }
    if ((ev.decoder === 'ft8' || ev.decoder === 'js8') && ev.type === 'slot') return `slot ${new Date(ev.slot_ms).toISOString().substr(11, 8)}: ${ev.decodes} decoded${ev.too_short ? ' (short)' : ''}`;
    if (ev.decoder === 'lora' && ev.type === 'packet') return `!${ev.sender.toString(16).padStart(8, '0')} ${ev.long_name ? ev.long_name + ' ' : ''}${ev.text}${ev.encrypted && ev.text !== 'ENCRYPTED FRAME' ? ' (decrypted)' : ''}`;
    if (ev.decoder === 'lora' && ev.type === 'capture') return `capture ${(ev.samples / 960000).toFixed(1)} s: ${ev.packets} packet(s), ${ev.preambles} preamble(s), CRC ${ev.crc_ok} ok / ${ev.crc_failures} bad`;
    if (ev.decoder === 'pocsag' && ev.type === 'message') return `${ev.capcode}/${ev.function} ${ev.baud} baud ${ev.kind}: ${ev.text}`;
    if (ev.decoder === 'adsb' && ev.type === 'frame') return `${ev.icao} ${ev.callsign || ''}`.trim();
    if (ev.decoder === 'adsb' && ev.type === 'position') return `${ev.icao} at ${ev.lat.toFixed(4)}, ${ev.lon.toFixed(4)}${ev.alt_ft !== undefined ? ' ' + ev.alt_ft + ' ft' : ''}`;
    if (ev.decoder === 'adsb' && ev.type === 'summary') return `summary: ${ev.crc_ok} frames, ${ev.aircraft} aircraft`;
    if (ev.decoder === 'acars' && ev.type === 'message') return `${ev.tail} ${ev.label} ${ev.mode}${ev.block_id} ${ev.text || ''}${ev.corrected ? ' (repaired)' : ''}`;
    if (ev.decoder === 'acars' && ev.type === 'summary') return `summary: ${ev.crc_ok} blocks (${ev.corrected} repaired), ${ev.crc_rejected} CRC failures`;
    if (ev.decoder === 'p25' && ev.type === 'identity') return `NAC ${ev.nac.toString(16).toUpperCase()}  WACN ${ev.wacn.toString(16).toUpperCase()}  system ${ev.system_id.toString(16).toUpperCase()}  RFSS ${ev.rfss} site ${ev.site}`;
    if (ev.decoder === 'p25' && ev.type === 'grant') return `talkgroup ${ev.talkgroup}${ev.source_id ? ' from ' + ev.source_id : ''} on ${(ev.frequency_hz / 1e6).toFixed(4)} MHz${ev.encrypted ? ' (encrypted)' : ''}${ev.emergency ? ' EMERGENCY' : ''}`;
    if (ev.decoder === 'p25' && ev.type === 'sync') return ev.locked ? 'frame sync locked (' + ev.modulation + ')' : 'frame sync lost';
    if (ev.decoder === 'p25' && ev.type === 'summary') return `summary: ${ev.nid_good} NIDs, ${ev.tsbk_good} TSBKs, ${ev.nid_failed + ev.tsbk_failed} failed`;
    if (ev.decoder === 'pocsag' && ev.type === 'summary') return `summary: ${ev.messages} messages, ${ev.codewords_valid}/${ev.codewords_total} codewords valid`;
    if (ev.decoder === 'sstv' && ev.type === 'image') return `${ev.mode} ${ev.width}x${ev.height} ${ev.rows} rows ${ev.end}${ev.partial ? ' (partial)' : ''}`;
    return JSON.stringify(ev);
  },
  render() {
    const box = $('dec-list');
    if (!box) return;
    box.textContent = '';
    for (const e of this.events.slice().reverse().slice(0, 80)) {
      const row = document.createElement('div');
      row.className = 'ev' + (e.ev.type === 'decode' || e.ev.type === 'message' || e.ev.type === 'image' ? '' : ' dim');
      const tag = document.createElement('span');
      tag.className = 'tag ' + e.placement; tag.textContent = e.placement === 'node' ? 'node' : 'web';
      row.append(tag, ' ' + this.describe(e));
      box.appendChild(row);
    }
  },
};

D.build();
root.OrcDecoders = D;

// ---- decoder dashboards (OrcSDR's canvas screens for POCSAG, P25, ...): the C++ in core.wasm draws them; this carries what they
// ask of the node (start/stop a decode session) and what the node decoded back into them ---------------------------------------
const M = () => root.m5 && root.m5.M;
const cstr = text => { const m = M(), n = m.lengthBytesUTF8(text) + 1, p = m._malloc(n); m.stringToUTF8(text, p, n); return p; };
// the C++ side reads \u00xx as one byte, so keep text ASCII
const asciiJson = obj => JSON.stringify(obj).replace(/[\u0080-￿]/g, c => '\\u' + c.charCodeAt(0).toString(16).padStart(4, '0'));
// Screens whose data does not come from a decoder: each has its own node endpoint, a poll and the requests its controller makes.
const AUX = {
  wifi: {
    async request(r) {
      if (r.op !== 'wifi_scan' || MOCK) return;
      const res = await post('/api/v1/wifi/scan', 'wifi-scan');
      if (!res.ok) toast('Wi-Fi scan: ' + ((res.body && (res.body.error || res.body.err)) || 'refused'));
    },
    async poll(ds) { ds.forward(await jget('/api/v1/wifi/scan')); },
  },
  // RF Lab: the node's raw IQ channel (8-bit, 300 kS/s) goes to the screen's analysis; the tuner's own controls are node calls.
  rflab: {
    center: 0, buf: null, fill: 0, heap: 0,
    async request(r, ds) {
      if (MOCK) return;
      if (r.op === 'rfchan') await this.open(r, ds);
      else if (r.op === 'gain') await ds.setGain({ auto: r.auto, tenth: r.tenth });
      else if (r.op === 'agc') await post(`/api/v1/tuner/agc?index=${S.rx}&on=${r.on ? 1 : 0}`, 'tuner-agc');
      else if (r.op === 'bias') {
        const res = await post(`/api/v1/tuner/bias?index=${S.rx}&on=${r.on ? 1 : 0}`, 'tuner-bias');
        if (!res.ok) toast('Bias tee: ' + ((res.body && (res.body.err || res.body.error)) || 'refused'));
      }
    },
    async open(r, ds) {
      if (ds.started) await ds.stopNode();
      const offset = Math.round(r.sps / 6);                  // the node tunes this far below the dial to keep the DC spike out of the channel
      this.buf = new Uint8Array(4096); this.fill = 0;
      Chan.gate = true;                                       // RF noise must not reach the speakers
      Chan.rawTap = u8 => this.tap(u8);
      const ok = await Chan.start(r.center + offset);
      if (!ok) return;
      ds.chanTap = true;
      this.center = Chan.params ? Chan.params.hz - (Chan.params.offset_hz || 0) : r.center;
    },
    tap(u8) {
      const m = M();
      if (!m) return;
      for (let off = 0; off < u8.length;) {
        const n = Math.min(this.buf.length - this.fill, u8.length - off);
        this.buf.set(u8.subarray(off, off + n), this.fill);
        this.fill += n; off += n;
        if (this.fill === this.buf.length) {
          if (!this.heap) this.heap = m._malloc(this.buf.length);
          m.HEAPU8.set(this.buf, this.heap);
          m._m5_ds_iq(this.heap, this.buf.length);
          this.fill = 0;
        }
      }
    },
    leave() { Chan.rawTap = null; Chan.gate = false; if (Chan.active) Chan.stop(); },
    async poll(ds) {
      const [t, rc] = await Promise.all([jget(`/api/v1/tuner?index=${S.rx}`), jget('/api/v1/receivers')]);
      const rx = (rc.receivers || [])[S.rx] || {};
      ds.forward({ type: 'rflab', caps: t.caps, gain_mode: t.gain_mode, gain_tenth_db: t.gain_tenth_db, rtl_agc: t.rtl_agc, bias: t.bias,
        gains: t.gains_tenth_db, device: rx.label || 'RTL-SDR', drops: rx.drops || 0, usb_errors: rx.usb_errors || 0, center: this.center });
    },
  },
  // RF visualizer: the same raw IQ channel, centred on whatever the page is tuned to.
  viz: {
    async request(r, ds) {
      if (MOCK || r.op !== 'vizchan') return;
      await AUX.rflab.open({ center: r.center || Math.round(S.frequency_hz) || 100000000, sps: 300000 }, ds);
    },
    leave() { AUX.rflab.leave(); },
    async poll(ds) {
      const rc = await jget('/api/v1/receivers');
      const rx = (rc.receivers || [])[S.rx] || {};
      ds.forward({ type: 'viz', center: AUX.rflab.center, drops: rx.drops || 0 });
    },
  },
  // Dead Air: the raw IQ (RF level, spectrum) and the audio demodulated here (audio activity), on the channel the screen asks for.
  deadair: {
    async request(r, ds) {
      if (MOCK || r.op !== 'dachan') return;
      if (ds.started) await ds.stopNode();
      Chan.setMode(r.mode, r.bw);
      Chan.gate = true;                                     // the screen measures the audio; nothing is played
      Chan.rawTap = u8 => AUX.rflab.tap(u8);
      AUX.rflab.buf = AUX.rflab.buf || new Uint8Array(4096);
      Chan.tap = a => {
        const m = M(), p = m._malloc(a.length * 4);
        m.HEAPF32.set(a, p >> 2);
        m._m5_ds_audio(p, a.length);
        m._free(p);
      };
      const ok = await Chan.start(r.hz);
      if (ok) ds.chanTap = true;
    },
    leave() { Chan.rawTap = null; Chan.tap = null; Chan.gate = false; if (Chan.active) Chan.stop(); },
    async poll() {},
  },
  // Settings: the node's own state (Wi-Fi link, SD card, firmware, clock), where this browser is, and what the node's radio heard.
  settings: {
    async request(r, ds) {
      if (r.op === 'toast') { toast(r.text); return; }
      if (r.op === 'wifi_scan') { await AUX.wifi.request({ op: 'wifi_scan' }); return; }
      if (r.op === 'settime') {
        try { localStorage.setItem('orcsdr.utc_offset', String(r.offset)); } catch {}
        if (MOCK) return;
        const t = await post(`/api/v1/time?epoch=${r.epoch}&ms=0`, 'set-time');
        toast(t.ok ? 'Node clock set' : 'Node refused the clock: ' + ((t.body && (t.body.error || t.body.err)) || ''));
      } else if (r.op === 'setoffset') {
        try { localStorage.setItem('orcsdr.utc_offset', String(r.offset)); } catch {}
      }
    },
    async poll(ds) {
      const [rep, sd, tm, scan] = await Promise.all([jget('/api/v1/report'), jget('/api/v1/sd'), jget('/api/v1/time'), jget('/api/v1/wifi/scan')]);
      let loc = null, range = 25, off = 0;
      try { loc = JSON.parse(localStorage.getItem('orcsdr.loc') || 'null'); range = +localStorage.getItem('orcsdr.adsb.range') || 25; off = +localStorage.getItem('orcsdr.utc_offset') || 0; } catch {}
      ds.forward({
        type: 'settings', node: rep.node_id, firmware: rep.firmware, uptime_s: Math.floor((rep.uptime_us || 0) / 1e6),
        wifi: rep.wifi || {}, mounted: !!sd.mounted, free_bytes: sd.free_bytes || 0, capacity_bytes: sd.capacity_bytes || 0,
        epoch: tm.epoch || 0, synced: !!tm.synced, offset_min: off,
        loc_set: !!(loc && Number.isFinite(loc.lat)), lat_e7: loc ? Math.round(loc.lat * 1e7) : 0, lon_e7: loc ? Math.round(loc.lon * 1e7) : 0, range_nm: range,
        scan_state: scan.state, aps: scan.aps || [],
      });
    },
  },
};
const DS = {
  kind: '', started: false, after: 0, slow: 0, evBusy: false,
  async startNode(kind, hz, options) {
    if (MOCK) return;
    if (Chan.active) await Chan.stop();
    if (kind === 'ft8' || kind === 'js8') {   // FT8 and JS8 slots follow the clock: give the node the browser's if it has none
      try {
        const st = await jget('/api/v1/decode/status');
        if (!st.clock_ok) { const ms = Date.now(); const t = await post(`/api/v1/time?epoch=${Math.floor(ms / 1000)}&ms=${ms % 1000}`, 'set-time'); if (t.ok) this.clockSynced = Math.floor(ms / 1000); }
      } catch {}
    }
    const go = () => post(`/api/v1/decode/start?kind=${kind}&index=${S.rx}&hz=${hz}&mode=usb`, 'decode', { headers: options ? { 'X-Orcdec-Options': options } : {} });
    let r = await go();
    if (!r.ok && r.body && r.body.err === 'ESP_ERR_INVALID_STATE') {   // a channel session from the screen we just left may still hold the receiver
      try { await post('/api/v1/channel/stop', 'channel'); } catch {}
      r = await go();
    }
    if (!r.ok) toast(kind.toUpperCase() + ': ' + ((r.body && (r.body.err || r.body.error)) || 'could not start on the node'));
    else this.started = true;
  },
  async stopNode() {
    if (MOCK || !this.started) return;
    this.started = false;
    try { await post('/api/v1/decode/stop', 'decode'); } catch {}
  },
  loadAdsbSettings() {
    const m = M();
    let loc = null, range = 25;
    try { loc = JSON.parse(localStorage.getItem('orcsdr.loc') || 'null'); range = +localStorage.getItem('orcsdr.adsb.range') || 25; } catch {}
    const pre = (key, v) => { const p = cstr('adsb'), k = cstr(key); m._m5_ds_pre(p, k, v); m._free(p); m._free(k); };
    if (loc && Number.isFinite(loc.lat) && Number.isFinite(loc.lon)) {
      pre('lat_e7', Math.round(loc.lat * 1e7));
      pre('lon_e7', Math.round(loc.lon * 1e7));
      pre('loc_set', 1);
    }
    pre('range', range);
  },
  saveSetting(r) {
    try {
      if (r.key === 'adsb') {
        localStorage.setItem('orcsdr.adsb.range', String(r.range));
        if (r.loc) localStorage.setItem('orcsdr.loc', JSON.stringify({ lat: r.lat_e7 / 1e7, lon: r.lon_e7 / 1e7 }));   // shared with the airband screen
      }
    } catch {}
  },
  async syncClock() {
    if (MOCK) return;
    const ms = Date.now();
    const t = await post(`/api/v1/time?epoch=${Math.floor(ms / 1000)}&ms=${ms % 1000}`, 'set-time');
    if (t.ok) { this.clockSynced = Math.floor(ms / 1000); toast('Node clock set from this browser'); }
  },
  async setGain(r) {
    if (MOCK) return;
    try {
      if (r.auto) await post(`/api/v1/tuner/gain?index=${S.rx}&mode=auto`, 'tuner-gain');
      else await post(`/api/v1/tuner/gain?index=${S.rx}&mode=manual&tenth_db=${r.tenth}`, 'tuner-gain');
    } catch {}
  },
  async openChannel(r) {   // a browser-placed screen (SSTV): demodulate the node's IQ channel here and hand the audio to the C++ receiver
    if (MOCK) return;
    await this.stopNode();
    Chan.setMode(r.mode, r.bw);
    const ok = await Chan.start(r.hz);
    if (!ok) return;
    this.chanTap = true;
    Chan.tap = a => {
      const m = M(), p = m._malloc(a.length * 4);
      m.HEAPF32.set(a, p >> 2);
      m._m5_ds_audio(p, a.length);
      m._free(p);
    };
    this.channelUp = true;
  },
  setNum(key, v) {
    const m = M(), k = cstr(key);
    m._m5_ds_set_num(k, v);
    m._free(k);
  },
  forward(ev) {
    const p = cstr(asciiJson(ev));
    M()._m5_ds_event(p);
    M()._free(p);
  },
  tick(screen) {
    const m = M();
    if (!m) return;
    if (!this.inited) { this.inited = true; this.loadAdsbSettings(); }
    if (screen !== 6) {
      // requests the screen queued as it closed (RF Lab puts the receiver back as it found it) still go out
      const left = m.UTF8ToString(m._m5_ds_decoder_left());
      const late = left ? m.UTF8ToString(m._m5_ds_request()) : '';
      if (late) {
        let r = null;
        try { r = JSON.parse(late); } catch {}
        if (r && r.op === 'gain') this.setGain(r);
        else if (r && r.op === 'agc') { if (!MOCK) post(`/api/v1/tuner/agc?index=${S.rx}&on=${r.on ? 1 : 0}`, 'tuner-agc').catch(() => {}); }
        else if (r && AUX[left] && AUX[left].request) AUX[left].request(r, this);
      }
      if (this.kind) {
        if (AUX[this.kind] && AUX[this.kind].leave) AUX[this.kind].leave();
        this.kind = '';
        this.stopNode();
        if (this.chanTap) { this.chanTap = false; this.channelUp = false; Chan.tap = null; Chan.stop(); }
      }
      return;
    }
    const kind = m.UTF8ToString(m._m5_ds_decoder());
    if (kind !== this.kind) { this.kind = kind; this.after = 0; this.slow = 0; }
    const req = m.UTF8ToString(m._m5_ds_request());
    if (req) {
      let r = null;
      try { r = JSON.parse(req); } catch {}
      if (r && r.op === 'toast') toast(r.text);
      else if (r && r.op === 'start') { this.after = 0; const k = /mode=js8/.test(r.options || '') ? 'js8' : kind; this.startNode(k, r.hz, k === 'js8' ? '' : r.options || ''); }
      else if (r && r.op === 'stop') this.stopNode();
      else if (r && r.op === 'setting') this.saveSetting(r);
      else if (r && r.op === 'chan') this.openChannel(r);
      else if (r && r.op === 'gain') this.setGain(r);
      else if (r && r.op === 'agc') { if (!MOCK) post(`/api/v1/tuner/agc?index=${S.rx}&on=${r.on ? 1 : 0}`, 'tuner-agc').catch(() => {}); }
      else if (r && r.op === 'sync_clock') this.syncClock();
      else if (r && AUX[kind] && AUX[kind].request) AUX[kind].request(r, this);
    }
    if (MOCK || ++this.slow % 5) return;      // the node is polled twice a second
    if (AUX[kind]) { AUX[kind].poll(this).catch(() => {}); return; }
    this.poll(kind);
  },
  async poll(kind) {
    if (this.evBusy && performance.now() - this.busyAt < 8000) return;   // a request that never answers must not stop the polling for good
    this.evBusy = true;
    this.busyAt = performance.now();
    try {
      const st = await jget('/api/v1/decode/status');
      const m = M();
      this.setNum('receiving', kind === 'sstv' ? (this.channelUp && Chan.active ? 1 : 0) : st.active && (st.kind === kind || (kind === 'ft8' && st.kind === 'js8')) ? 1 : 0);
      this.setNum('node_clock_ok', st.clock_ok ? 1 : 0);
      this.setNum('utc', Math.floor(Date.now() / 1000));
      this.setNum('utc_ms', Date.now());
      this.setNum('clock_synced_utc', this.clockSynced || 0);
      this.setNum('rf_dbfs', S.rf_dbfs);
      const r = await jget('/api/v1/decode/events?after=' + this.after);
      for (const e of r.events) if (e.ev && (e.ev.decoder === kind || (kind === 'ft8' && e.ev.decoder === 'js8'))) this.forward(e.ev);
      if (r.last > this.after) this.after = r.last;
      if (kind === 'adsb') {
        const air = await jget('/api/v1/decode/aircraft');
        this.forward({ decoder: 'adsb', type: 'aircraft_begin', messages: air.messages });
        for (const a of air.aircraft) this.forward(Object.assign({ decoder: 'adsb', type: 'aircraft' }, a));
        this.forward({ decoder: 'adsb', type: 'aircraft_end' });
        this.setNum('sps', st.sps || 2048000);
      }
      else if (st.active && st.kind === kind) { const stats = await jget('/api/v1/decode/stats'); if (stats.type === 'stats') this.forward(stats); }
    } catch {}
    this.evBusy = false;
  },
};
root.OrcDataScreens = DS;
})(typeof window !== 'undefined' ? window : globalThis);
