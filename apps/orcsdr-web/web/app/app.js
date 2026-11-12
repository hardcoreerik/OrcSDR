// OrcSDR web UI shell. The pixels come from the shared C++ UI (core.wasm); this file feeds it state
// and turns its actions into REST calls (live) or state changes (mock). No framework.
'use strict';
(() => {
const q = new URLSearchParams(location.search);
const MOCK = q.get('mock') === '1';
const API = q.get('api') || '';              // e.g. http://192.168.1.117 when the page is served elsewhere
const W = 1280, H = 720, BINS = 512, FLOOR = -90;
const STEPS = [1000, 5000, 6250, 8330, 9000, 10000, 12500, 25000, 50000, 100000, 200000];
const BANDS = [
  [87500000, 108000000, 'FM BCAST', 'WFM'], [118000000, 137000000, 'AIRBAND', 'AM'],
  [137000000, 138000000, 'WX SAT', 'NFM'], [144000000, 148000000, '2 M HAM', 'NFM'],
  [156000000, 162025000, 'MARINE VHF', 'NFM'], [162400000, 162550000, 'NOAA WX', 'NFM'],
  [225000000, 400000000, 'MIL AIR', 'AM'], [420000000, 450000000, '70 CM HAM', 'NFM'],
  [462000000, 468000000, 'FRS/GMRS', 'NFM'], [806000000, 960000000, '800/900 MHZ', 'NFM'],
  [1090000000, 1090000001, 'ADS-B', 'RAW'],
];
const MODE_NAMES = ['AUTO', 'NFM', 'AM', 'WFM', 'USB', 'LSB'];

const $ = id => document.getElementById(id);
const canvas = $('c'), ctx = canvas.getContext('2d');
const img = ctx.createImageData(W, H);
const px32 = new Uint32Array(img.data.buffer);
let M = null;

// ---- shared state: what the UI shows --------------------------------------------------------
const S = {
  rx: 0, receivers: [], present: false, streaming: false, rxLabel: 'RTL-SDR',
  frequency_hz: 98100000, span_hz: 960000, step_hz: 12500, sps: 960000,
  tune_min_hz: 24000000, tune_max_hz: 1766000000,
  volume: 40, muted: false, fmPlaying: false, fmOwns: false, rf_dbfs: -90,
  gain_available: false, gain_auto_available: false, gain_auto: true, gain_tenth_db: 0, gains: [],
  rtl_agc_available: false, rtl_agc: false, bias_available: false, bias_on: false,
  net: false, ip: '', mode_choice: 0, filter_bandwidth_hz: 0, edges: false, link: '',
  fmStep: 100000, fmStat: null, graphics: true, presets: [], amBw: 10000, amSpan: 240000, chanHz: 0,
};

try { S.presets = JSON.parse(localStorage.getItem('orcsdr.fm.presets') || '[]').filter(Number.isFinite).slice(0, 10); } catch {}
const savePresets = () => { try { localStorage.setItem('orcsdr.fm.presets', JSON.stringify(S.presets)); } catch {} };
const NVS_KEY = 'orcsdr.nvs';
function nvsImport() {
  let text = ''; try { text = localStorage.getItem(NVS_KEY) || ''; } catch {}
  const n = M.lengthBytesUTF8(text) + 1, p = M._malloc(n); M.stringToUTF8(text, p, n); M._m5_nvs_import(p); M._free(p);
  M._m5_cb_load();
  M._m5_sw_load();
  fsImport();
  S.nvsRev = M._m5_nvs_revision();
}
const FS_KEY = 'orcsdr.fs';
function fsImport() {
  let text = ''; try { text = localStorage.getItem(FS_KEY) || ''; } catch {}
  const n = M.lengthBytesUTF8(text) + 1, p = M._malloc(n); M.stringToUTF8(text, p, n); M._m5_fs_import(p); M._free(p);
  S.fsRev = M._m5_fs_revision(); S.exported = new Set(M.UTF8ToString(M._m5_fs_list(cstr('/OrcSDR/exports/'))).split(String.fromCharCode(10)).filter(Boolean));
}
function fsSave() {
  const rev = M._m5_fs_revision(); if (rev === S.fsRev) return;
  S.fsRev = rev; try { localStorage.setItem(FS_KEY, M.UTF8ToString(M._m5_fs_export())); } catch {}
}
function offerExports() {   // hand any newly written logbook export files to the browser as downloads
  const names = M.UTF8ToString(M._m5_fs_list(cstr('/OrcSDR/exports/'))).split(String.fromCharCode(10)).filter(Boolean);
  for (const name of names) {
    if (S.exported.has(name)) continue;
    S.exported.add(name);
    const text = M.UTF8ToString(M._m5_fs_read(cstr(name))), a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([text], { type: 'text/plain' })); a.download = name.split('/').pop();
    document.body.appendChild(a); a.click(); a.remove();
  }
  toast('Logbook exported (CSV and ADIF downloads)');
}
function nvsSave() {
  const rev = M._m5_nvs_revision(); if (rev === S.nvsRev) return;
  S.nvsRev = rev; try { localStorage.setItem(NVS_KEY, M.UTF8ToString(M._m5_nvs_export())); } catch {}
}
function bandOf(hz) {
  for (const b of BANDS) if (hz >= b[0] && hz < b[1]) return b;
  return [0, 0, hz < 30000000 ? 'HF' : hz < 300000000 ? 'VHF' : 'UHF', 'RAW'];
}

// ---- toast ------------------------------------------------------------------------------------
let toastTimer = 0;
function toast(msg) {
  const t = $('toast'); t.textContent = msg; t.classList.add('on');
  clearTimeout(toastTimer); toastTimer = setTimeout(() => t.classList.remove('on'), 3200);
}

// ---- WASM glue --------------------------------------------------------------------------------
let scratch = 0;
function cstr(s) {
  const n = M.lengthBytesUTF8(s) + 1;
  if (!scratch || n > 256) { if (scratch) M._free(scratch); scratch = M._malloc(Math.max(n, 256)); }
  M.stringToUTF8(s, scratch, Math.max(n, 256));
  return scratch;
}
const setNum = (k, v) => { const kp = M._malloc(M.lengthBytesUTF8(k) + 1); M.stringToUTF8(k, kp, 64); M._m5_set_num(kp, +v); M._free(kp); };
const setStr = (k, v) => {
  const kp = M._malloc(M.lengthBytesUTF8(k) + 1); M.stringToUTF8(k, kp, 64);
  const vp = M._malloc(M.lengthBytesUTF8(v) + 1); M.stringToUTF8(v, vp, 128);
  M._m5_set_str(kp, vp); M._free(kp); M._free(vp);
};

function pushSnapshot() { const sc = M._m5_screen(); S.cbScreen = sc === 3; S.swScreen = sc === 4; S.abScreen = sc === 5; if (sc === 5) pushAb(); else if (sc === 4) pushSw(); else if (sc === 3) pushCb(); else if (sc === 2) pushAm(); else if (sc === 1) pushFm(); else pushHome(); }
const CB_CENTER = 27535000, CB_RX_SPS = 1800000;   // one window covers channels 1-40 and keeps the RTL DC spike out of band
const CB_MODES = [['AM', 10000], ['USB', 3000], ['LSB', 3000]];
let cbLastTune = 0;
function pushCb() {
  // 1. the receiver window's spectrum into the dashboard's channel monitor and scanner
  const bins = new Float32Array(M.HEAPF32.buffer, M._m5_cb_bins(), BINS);
  if (lastRow) {
    for (let i = 0; i < BINS; i++) bins[i] = binDb(lastRow.bins[i]);
    M._m5_cb_observe(lastRow.stop - lastRow.start, Math.round((lastRow.start + lastRow.stop) / 2));
  }
  // 2. signal level and squelch
  const sql = M._m5_cb_squelch(), sig = S.chanDb !== undefined ? S.chanDb : -90, open = sql <= -90 || sig > sql;
  M._m5_cb_signal(sig, open ? 1 : 0, Chan.active ? 1 : 0, S.muted ? 0 : 1);
  Chan.gate = !open;
  M._m5_commit();
  // 3. what the dashboard decided: mode, clarifier, a new channel (user or scanner)
  const [mode, bw] = CB_MODES[M._m5_cb_mode()] || CB_MODES[0];
  Chan.setMode(mode, bw); Chan.setClarifier(M._m5_cb_clarifier());
  const tune = M._m5_cb_take_tune();
  if (tune) {
    S.frequency_hz = tune; cbLastTune = tune;
    if (!MOCK) Chan.start(tune, { center: CB_CENTER, rx_sps: CB_RX_SPS }).then(() => Chan.setClarifier(M._m5_cb_clarifier()));
  }
}
const SW_MODES = ['AM', 'USB', 'LSB'];
const setSwNum = (k, v) => { const kp = M._malloc(M.lengthBytesUTF8(k) + 1); M.stringToUTF8(k, kp, 64); M._m5_sw_set_num(kp, +v); M._free(kp); };
let swHunting = null;
function pushSw() {
  const sig = S.chanDb !== undefined ? S.chanDb : -90;
  setSwNum('rf_dbfs', sig); setSwNum('clipping', 0); setSwNum('running', Chan.active ? 1 : 0); setSwNum('driver_ready', S.present ? 1 : 0);
  setSwNum('sound', S.muted ? 0 : 1); setSwNum('tuner_agc', S.gain_auto ? 1 : 0); setSwNum('rtl_agc', S.rtl_agc ? 1 : 0);
  setSwNum('cap_gain', S.gain_available ? 1 : 0); setSwNum('cap_tuner_agc', S.gain_auto_available ? 1 : 0); setSwNum('cap_rtl_agc', S.rtl_agc_available ? 1 : 0);
  setSwNum('route', S.tune_min_hz < 24000000 && S.frequency_hz < 28800000 ? 2 : 3);
  setSwNum('gain_tenth_db', S.gain_tenth_db); setSwNum('volume', S.volume); setSwNum('battery', -1); setSwNum('utc', Math.floor(Date.now() / 1000));
  const dev = M._malloc(64), key = M._malloc(16); M.stringToUTF8('device', key, 16); M.stringToUTF8(S.rxLabel || 'RTL-SDR', dev, 64); M._m5_sw_set_str(key, dev); M._free(dev); M._free(key);
  const n = Math.min(32, S.gains.length); setSwNum('gain_step_count', n);
  for (let i = 0; i < n; i++) M._m5_sw_set_gain_step(i, S.gains[i]);
  M._m5_commit();
  // What the dashboard decided: demodulator, filter, clarifier, level boost, a new frequency, a band hunt.
  const mode = SW_MODES[M._m5_sw_mode()] || 'AM';
  Chan.setMode(mode, M._m5_sw_filter()); Chan.setClarifier(M._m5_sw_clarifier());
  Chan.boost = M._m5_sw_boost() ? 4 : 1;
  S.amSpan = M._m5_sw_span();
  const tune = M._m5_sw_take_tune();
  if (tune) { S.frequency_hz = tune; if (!MOCK && !swHunting) Chan.start(tune).then(() => Chan.setClarifier(M._m5_sw_clarifier())); }
  const hunt = M._m5_sw_hunt_requested();
  if (hunt >= 0) runHunt(hunt);
  else if (hunt === -2 && swHunting) { swHunting.cancel = true; }
}
// A hunt reads one wide spectrum over the band (instead of the device's step-by-step sweep) and returns to the old frequency.
async function runHunt(band) {
  const lo = M._m5_sw_band_min(band), hi = M._m5_sw_band_max(band), center = Math.round((lo + hi) / 2), restore = S.frequency_hz;
  swHunting = { cancel: false };
  try {
    if (!MOCK) {
      if (!(await Chan.start(center, { center, rx_sps: 1200000 }))) throw new Error('could not start the wide session');
      await new Promise(r => setTimeout(r, 1800));   // let the receiver settle and the spectrum fill
    }
    if (swHunting.cancel) return;
    if (MOCK) {   // a few synthetic carriers across the band
      const b = new Uint8Array(BINS), w0 = center - 600000;
      for (let i = 0; i < BINS; i++) {
        const f = w0 + (i + .5) * 1200000 / BINS; let db = -85 + 2 * Math.sin(i * 1.3);
        for (const [pos, a] of [[.2, 38], [.45, 30], [.8, 44]]) db += a * Math.exp(-(((f - (lo + (hi - lo) * pos)) / 2500) ** 2));
        b[i] = Math.max(1, Math.min(255, Math.round(1 + (db + 80) * 254 / 70)));
      }
      lastRow = { start: w0, stop: w0 + 1200000, bins: b };
    }
    const bins = new Float32Array(M.HEAPF32.buffer, M._m5_sw_hunt_bins(), BINS);
    if (lastRow) for (let i = 0; i < BINS; i++) bins[i] = binDb(lastRow.bins[i]);
    M._m5_sw_hunt_finish(lastRow ? lastRow.stop - lastRow.start : 1200000, lastRow ? Math.round((lastRow.start + lastRow.stop) / 2) : center);
  } catch (e) { toast('Band hunt: ' + e.message); }
  finally {
    swHunting = null;
    if (!MOCK) await Chan.start(restore);
    S.frequency_hz = restore;
  }
}
const setAbNum = (k, v) => { const kp = M._malloc(M.lengthBytesUTF8(k) + 1); M.stringToUTF8(k, kp, 64); M._m5_ab_set_num(kp, +v); M._free(kp); };
function readLocation() { try { const v = JSON.parse(localStorage.getItem('orcsdr.loc') || 'null'); return v && Number.isFinite(v.lat) && Number.isFinite(v.lon) ? v : null; } catch { return null; } }
function pushAb() {
  const loc = readLocation();
  setAbNum('channel_db', S.chanDb !== undefined ? S.chanDb : -120); setAbNum('running', Chan.active ? 1 : 0); setAbNum('sound', S.muted ? 0 : 1); setAbNum('battery', -1);
  setAbNum('loc_set', loc ? 1 : 0); setAbNum('lat_e7', loc ? Math.round(loc.lat * 1e7) : 0); setAbNum('lon_e7', loc ? Math.round(loc.lon * 1e7) : 0);
  setAbNum('tuner_agc', S.gain_auto ? 1 : 0); setAbNum('rtl_agc', S.rtl_agc ? 1 : 0); setAbNum('gain_tenth_db', S.gain_tenth_db); setAbNum('volume', S.volume);
  setAbNum('cap_gain', S.gain_available ? 1 : 0); setAbNum('cap_tuner_agc', S.gain_auto_available ? 1 : 0); setAbNum('cap_rtl_agc', S.rtl_agc_available ? 1 : 0);
  const n = Math.min(32, S.gains.length); setAbNum('gain_step_count', n);
  for (let i = 0; i < n; i++) M._m5_ab_set_gain_step(i, S.gains[i]);
  M._m5_commit();
  // what the runtime decided
  Chan.setMode('AM', M._m5_ab_filter()); Chan.gate = !M._m5_ab_audio_open(); S.amSpan = M._m5_ab_span();
  const tune = M._m5_ab_take_tune();
  if (tune) { S.frequency_hz = tune; if (!MOCK) Chan.start(tune); }
  for (let k = M._m5_ab_take_gain_kind(); k; k = M._m5_ab_take_gain_kind()) {
    const v = M._m5_ab_gain_value();
    if (MOCK) { if (k === 1) { S.gain_auto = false; S.gain_tenth_db = v; } else if (k === 2) S.gain_auto = !!v; else S.rtl_agc = !!v; continue; }
    const q = k === 1 ? `/api/v1/tuner/gain?index=${S.rx}&mode=manual&tenth_db=${v}` : k === 2 ? `/api/v1/tuner/gain?index=${S.rx}&mode=${v ? 'auto' : 'manual'}` : `/api/v1/tuner/agc?index=${S.rx}&on=${v ? 1 : 0}`;
    post(q, k === 3 ? 'tuner-agc' : 'tuner-gain').then(r => report(r, 'Gain'));
  }
}
function setLocation() {
  const done = (lat, lon) => { try { localStorage.setItem('orcsdr.loc', JSON.stringify({ lat, lon })); } catch {} toast('Location saved (' + lat.toFixed(3) + ', ' + lon.toFixed(3) + ')'); M._m5_ab_reload(); };
  const manual = () => { const t = prompt('Receiver location as "latitude, longitude" in decimal degrees (kept in this browser):'); if (!t) return; const m = t.split(',').map(Number); if (m.length === 2 && m.every(Number.isFinite) && Math.abs(m[0]) <= 90 && Math.abs(m[1]) <= 180) done(m[0], m[1]); else toast('Could not read that location'); };
  if (!navigator.geolocation) return manual();
  navigator.geolocation.getCurrentPosition(pos => done(pos.coords.latitude, pos.coords.longitude), manual, { timeout: 8000 });
}
async function loadCatalogFile(file) {
  const buf = new Uint8Array(await file.arrayBuffer()), p = M._malloc(buf.length);
  M.HEAPU8.set(buf, p);
  const path = M._malloc(40); M.stringToUTF8('/orcsdr/data/aviation.idx', path, 40);
  const ok = M._m5_fs_write(path, p, buf.length); M._free(p); M._free(path);
  if (ok) { M._m5_ab_reload(); toast('Aviation catalog loaded (' + Math.round(buf.length / 1024) + ' KB' + (buf.length > 2e6 ? '; too large to keep between visits' : '') + ')'); } else toast('Could not store the catalog');
}
$('abloc').addEventListener('click', setLocation);
$('abcat').addEventListener('change', e => { if (e.target.files[0]) loadCatalogFile(e.target.files[0]); e.target.value = ''; });
const setAmNum = (k, v) => { const kp = M._malloc(M.lengthBytesUTF8(k) + 1); M.stringToUTF8(k, kp, 64); M._m5_am_set_num(kp, +v); M._free(kp); };
function pushAm() {
  setAmNum('frequency_hz', S.frequency_hz); setAmNum('step_hz', M._m5_am_tune_step());
  setAmNum('filter_bandwidth_hz', S.amBw); setAmNum('span_hz', S.amSpan);
  setAmNum('relative_dbfs', S.rf_dbfs); setAmNum('running', Chan.active ? 1 : 0); setAmNum('driver_ready', S.present ? 1 : 0);
  setAmNum('sound_enabled', S.muted ? 0 : 1); setAmNum('graphics_enabled', S.graphics ? 1 : 0); setAmNum('recording', 0);
  setAmNum('volume', S.volume);
  setAmNum('gain_available', S.gain_available ? 1 : 0); setAmNum('rtl_agc_available', S.rtl_agc_available ? 1 : 0);
  setAmNum('rtl_agc', S.rtl_agc ? 1 : 0); setAmNum('gain_auto', S.gain_auto ? 1 : 0); setAmNum('gain_tenth_db', S.gain_tenth_db);
  const n = Math.min(32, S.gains.length);
  setAmNum('gain_step_count', n);
  for (let i = 0; i < n; i++) M._m5_set_gain_step(i, S.gains[i]);
  setAmNum('scan_active', 0); setAmNum('scan_total', 1); setAmNum('selected_preset', -1);
  M._m5_commit();
}
const setFmNum = (k, v) => { const kp = M._malloc(M.lengthBytesUTF8(k) + 1); M.stringToUTF8(k, kp, 64); M._m5_fm_set_num(kp, +v); M._free(kp); };
function pushFm() {
  const f = S.fmStat || {};
  setFmNum('frequency_hz', S.frequency_hz); setFmNum('step_hz', S.fmStep); setFmNum('filter_bandwidth_hz', 180000);
  setFmNum('span_hz', S.span_hz); setFmNum('effective_sps', S.sps); setFmNum('target_sps', 2560000);
  setFmNum('usb_overruns', S.drops || 0); setFmNum('consumer_drops', f.queue_drops || 0);
  setFmNum('audio_underruns', f.tx_queue_overflows || 0);
  setFmNum('dsp_percent', f.block_us ? Math.min(100, Math.round(100 * f.dsp_us / f.block_us)) : 0);
  setFmNum('relative_dbfs', S.rf_dbfs);
  const a = f.audio_rms_dbfs !== undefined ? f.audio_rms_dbfs : -90;
  setFmNum('left_dbfs', a); setFmNum('right_dbfs', a);
  setFmNum('running', S.fmOwns ? 1 : 0); setFmNum('driver_ready', S.present ? 1 : 0);
  setFmNum('stereo', 0); setFmNum('rds_carrier', 0); setFmNum('rds_locked', 0);
  setFmNum('wifi_connected', S.net ? 1 : 0); setFmNum('sound_enabled', S.muted ? 0 : 1);
  setFmNum('graphics_enabled', S.graphics ? 1 : 0); setFmNum('recording', 0); setFmNum('preset_scanning', 0);
  setFmNum('gain_auto', S.gain_auto ? 1 : 0); setFmNum('gain_tenth_db', S.gain_tenth_db);
  const n = Math.min(32, S.gains.length);
  setFmNum('gain_step_count', n);
  for (let i = 0; i < n; i++) M._m5_set_gain_step(i, S.gains[i]);
  setFmNum('volume', S.volume);
  const pi = S.presets.indexOf(S.frequency_hz);
  setFmNum('preset_count', S.presets.length); setFmNum('preset_index', pi >= 0 ? pi + 1 : 0);
  M._m5_commit();
}
function pushHome() {
  const hz = S.frequency_hz, band = bandOf(hz);
  const d = new Date();
  const pin = S.mode_pin_band === band[2] ? S.mode_choice : 0;   // the pin is OrcSDR's, per band; the page remembers only the one it set here
  const mode = pin ? MODE_NAMES[pin] : S.fmPlaying && S.fmOwns ? 'WFM' : band[3];
  setNum('frequency_hz', hz); setNum('requested_frequency_hz', hz);
  setNum('tune_min_hz', S.tune_min_hz); setNum('tune_max_hz', S.tune_max_hz);
  setNum('span_hz', S.span_hz); setNum('step_hz', S.step_hz); setNum('step_adjustable', 1);
  setNum('effective_sps', S.sps); setNum('filter_bandwidth_hz', S.filter_bandwidth_hz);
  setNum('edges_hint', S.edges ? 1 : 0);
  setNum('volume', S.volume); setNum('sound_enabled', S.muted ? 0 : 1);
  setNum('relative_dbfs', S.rf_dbfs);
  setNum('wifi_connected', S.net ? 1 : 0); setStr('wifi_ip', S.ip);
  setNum('usb_connected', S.present ? 1 : 0); setNum('driver_ready', S.present ? 1 : 0);
  setNum('receiving', S.streaming ? 1 : 0);
  setStr('receiver', S.rxLabel.slice(0, 11)); setStr('mode', mode); setStr('band', band[2]);
  setNum('mode_choice', pin);
  setNum('mode_active', Math.max(0, MODE_NAMES.indexOf(mode)));
  setNum('mode_suggested', Math.max(0, MODE_NAMES.indexOf(band[3])));
  setNum('mode_selectable', band[2] === 'ADS-B' ? 0 : 1);
  setNum('gain_available', S.gain_available ? 1 : 0);
  setNum('gain_auto_available', S.gain_auto_available ? 1 : 0);
  setNum('gain_smart', 0); setNum('gain_auto', S.gain_auto ? 1 : 0);
  setNum('rtl_agc_available', S.rtl_agc_available ? 1 : 0); setNum('rtl_agc', S.rtl_agc ? 1 : 0);
  setNum('bias_available', S.bias_available ? 1 : 0); setNum('bias_on', S.bias_on ? 1 : 0);
  setNum('gain_tenth_db', S.gain_tenth_db);
  const n = Math.min(32, S.gains.length);
  setNum('gain_step_count', n);
  for (let i = 0; i < n; i++) M._m5_set_gain_step(i, S.gains[i]);
  setStr('clock', d.toTimeString().slice(0, 5));
  setStr('date', d.toLocaleDateString('en-GB', { day: '2-digit', month: 'short' }).toUpperCase());
  M._m5_commit();
}

// ---- spectrum: bins of a start..stop row -> the 512 view columns ------------------------------
let viewFloor = FLOOR;
let lastRow = null;       // {start, stop, bins: Uint8Array(512)} from the node, or synthetic (mock)
let levelsView = null;
function binDb(b) { return b === 0 ? FLOOR : (b - 1) * 70 / 254 - 80; }
function fillLevels() {
  if (!levelsView) levelsView = new Float32Array(M.HEAPF32.buffer, M._m5_levels(), BINS);
  if (levelsView.buffer !== M.HEAPF32.buffer) levelsView = new Float32Array(M.HEAPF32.buffer, M._m5_levels(), BINS);
  const lo = S.frequency_hz - S.span_hz / 2;
  let peak = FLOOR;
  for (let i = 0; i < BINS; i++) {
    let v = FLOOR;
    if (lastRow) {
      const f = lo + (i + 0.5) * S.span_hz / BINS;
      if (f >= lastRow.start && f < lastRow.stop) {
        v = binDb(lastRow.bins[Math.min(BINS - 1, Math.floor((f - lastRow.start) * BINS / (lastRow.stop - lastRow.start)))]);
      }
    }
    levelsView[i] = v; if (v > peak) peak = v;
  }
  // Display floor: just under the noise floor (15th percentile), so the trace uses the plot's height.
  // The device does the equivalent from its own levels; a fixed -90 dBFS leaves real signals pinned at the top.
  const sorted = Float32Array.from(levelsView).sort();
  const noise = sorted[Math.floor(BINS * 0.15)];
  viewFloor = lastRow ? Math.max(-95, Math.min(-50, noise - 4)) : FLOOR;
  return peak;
}

function blit() {
  const p = M._m5_fb(), b = M.HEAPU8;
  for (let i = 0, j = p; i < px32.length; i++, j += 2) {
    const v = (b[j] << 8) | b[j + 1];
    const r = (v >> 11 & 31) * 255 / 31 | 0, g = (v >> 5 & 63) * 255 / 63 | 0, bl = (v & 31) * 255 / 31 | 0;
    px32[i] = 0xff000000 | bl << 16 | g << 8 | r;
  }
  ctx.putImageData(img, 0, 0);
}

// ---- node adapters ----------------------------------------------------------------------------
async function jget(path) { const r = await fetch(API + path, { cache: 'no-store' }); if (!r.ok) throw new Error(path + ' ' + r.status); return r.json(); }
async function post(path, confirm, extra = {}) {
  const r = await fetch(API + path, { method: 'POST', headers: { 'X-OrcSDR-Confirm': confirm, ...(extra.headers || {}) }, body: extra.body });
  let body = null; try { body = await r.json(); } catch {}
  return { ok: r.ok && !(body && (body.ok === false || body.error)), body };
}
const errText = r => (r.body && (r.body.err || r.body.error)) ||
  (r.body && r.body.results && r.body.results.some(x => x.rc === 259) ? 'receiver busy (FM or scanner owns it)' : 'request failed');

const Live = {
  name: 'live',
  async refresh() {
    const [rcv, fm, rep] = await Promise.all([jget('/api/v1/receivers'), jget('/api/v1/fm'), jget('/api/v1/report')]);
    S.receivers = rcv.receivers;
    if (!S.receivers[S.rx] || !S.receivers[S.rx].present) {
      const i = S.receivers.findIndex(r => r.present);
      if (i >= 0) S.rx = i;
    }
    const r = S.receivers[S.rx] || {};
    S.present = !!r.present; S.streaming = r.state === 3 && (r.last_iq_age_ms ?? 99999) < 3000;
    S.rxLabel = (r.label || 'RTL-SDR').replace(/^RTL-SDR\s*/i, 'RTL ').slice(0, 11) || 'RTL-SDR';
    S.fmPlaying = !!fm.playing; S.fmOwns = fm.playing && fm.receiver === S.rx;
    S.fmStat = S.fmOwns ? fm : null; S.drops = r.drops_recent || 0;
    const amScreen = [2, 3, 4, 5].includes(M._m5_screen()) || Chan.active;
    if (amScreen) { S.sps = 300000; }
    else if (S.fmOwns) { S.frequency_hz = fm.frequency_hz; S.sps = fm.input_sps; S.rf_dbfs = fm.rf_dbfs; S.volume = fm.volume; S.muted = fm.volume === 0; }
    else if (r.present) { S.frequency_hz = r.center_hz || S.frequency_hz; S.sps = r.sample_rate || S.sps; }
    // FM parks the tuner a quarter of the sample rate below the station, so only +/- sps/4 around the
    // station is covered on both sides; show that much rather than a view with an empty edge.
    S.span_hz = M._m5_screen() === 3 ? CB_RX_SPS : amScreen ? S.amSpan : S.fmOwns ? Math.round(S.sps / 2) : S.sps;
    S.net = !!(rep.eth && rep.eth.link) || !!(rep.wifi && rep.wifi.connected);
    S.ip = (rep.eth && rep.eth.ip) || (rep.wifi && rep.wifi.ip) || location.hostname;
    $('src').textContent = 'node ' + rep.node_id + ' · ' + rep.firmware;
    if (r.present) {
      try {
        const t = await jget('/api/v1/tuner?index=' + S.rx);
        S.tune_min_hz = t.tune_min_hz; S.tune_max_hz = t.tune_max_hz;
        S.gain_available = t.gain_manual; S.gain_auto_available = t.gain_auto;
        S.gain_auto = t.gain_mode === 'auto'; S.gain_tenth_db = t.gain_tenth_db; S.gains = t.gains_tenth_db || [];
        S.rtl_agc_available = t.rtl_agc_available; S.rtl_agc = t.rtl_agc;
        S.bias_available = t.bias_available; S.bias_on = t.bias;
        S.nodeClock = t.clock;
      } catch (e) { /* tuner endpoint missing on older firmware: keep defaults */ }
    }
    if (S.nodeClock === false && !Live.clockSet) { Live.clockSet = true; this.setClock(); }
  },
  async setClock() {
    const r = await post('/api/v1/time?epoch=' + Math.floor(Date.now() / 1000), 'set-time');
    toast(r.ok ? 'Node clock set from this browser' : 'Could not set node clock: ' + errText(r));
  },
  async spectrum() {
    let buf, off = 0, source = 'receiver window';
    const r = await fetch(API + '/api/v1/spectrum?index=' + S.rx, { cache: 'no-store' });
    if (r.ok) buf = await r.arrayBuffer();
    else {                                   // firmware without /api/v1/spectrum: FM or scanner rows only
      source = 'FM/scanner row';
      if (S.fmOwns) buf = await (await fetch(API + '/api/v1/fm/spectrum', { cache: 'no-store' })).arrayBuffer();
      else { buf = await (await fetch(API + '/api/v1/waterfall?mode=live', { cache: 'no-store' })).arrayBuffer(); off = 8 + 532 * S.rx; }
    }
    if (buf.byteLength < off + 532) return;
    const v = new DataView(buf, off, 532);
    const start = v.getUint32(8, true), stop = v.getUint32(12, true), running = v.getUint8(16);
    if (!running || stop <= start) { lastRow = null; $('spec').textContent = 'no data yet'; return; }
    lastRow = { start, stop, bins: new Uint8Array(buf, off + 20, BINS) };
    $('spec').textContent = ((stop - start) / 1e6).toFixed(2) + ' MHz ' + source;
  },
  async do(name, value) {
    const idx = S.rx;
    switch (name) {
      case 'tune_frequency': return tuneTo(value);
      case 'step_up': return tuneTo(S.frequency_hz + curStep());
      case 'step_down': return tuneTo(S.frequency_hz - curStep());
      case 'span_up': case 'span_down': {
        if (S.fmOwns) {   // the FM service owns the receiver and refuses a rate change; its span is the rate it runs at, so restart it at the next one
          const rates = [300000, 2560000, 3200000], i = Math.max(0, rates.indexOf(S.sps)), j = Math.max(0, Math.min(2, i + (name === 'span_up' ? 1 : -1)));
          if (j === i) return toast('Span: ' + (rates[i] / 1e6).toFixed(2) + ' MS/s is the ' + (j ? 'widest' : 'narrowest') + ' the FM receiver runs at');
          return report(await post(`/api/v1/fm?action=start&index=${idx}&hz=${S.frequency_hz}&sps=${rates[j]}&volume=${S.volume}&tau=75`, 'fm-control'), 'Span ' + (rates[j] / 1e6).toFixed(2) + ' MS/s');
        }
        const sps = M._m5_step_span(S.span_hz, name === 'span_up' ? 1 : -1);
        const r = await post(`/api/v1/receivers/rate?index=${idx}&sps=${Math.max(225000, Math.min(3200000, sps))}`, 'rx-rate');
        return report(r, 'Span ' + (sps / 1e6).toFixed(2) + ' MS/s');
      }
      case 'volume_up': case 'volume_down': return setVolume(S.volume + (name === 'volume_up' ? 5 : -5));
      case 'sound_toggle': return S.muted ? setVolume(S.prevVolume || 40) : (S.prevVolume = S.volume, setVolume(0));
      case 'gain_auto': return report(await post(`/api/v1/tuner/gain?index=${idx}&mode=${S.gain_auto ? 'manual' : 'auto'}`, 'tuner-gain'), 'Gain mode');
      case 'gain_tenth_db': return report(await post(`/api/v1/tuner/gain?index=${idx}&mode=manual&tenth_db=${value | 0}`, 'tuner-gain'), 'Gain');
      case 'rtl_agc': return report(await post(`/api/v1/tuner/agc?index=${idx}&on=${value ? 1 : 0}`, 'tuner-agc'), 'RTL AGC');
      default: return clientAction(name, value);
    }
  },
};

const curStep = () => (M._m5_screen() === 2 ? M._m5_am_tune_step() : M._m5_screen() === 1 ? S.fmStep : S.step_hz);
async function tuneTo(hz) {
  if (M._m5_screen() === 2) {
    hz = Math.max(520000, Math.min(1710000, Math.round(hz)));            // the AM broadcast band this screen is built for
    M._m5_am_note_tuned(hz); S.frequency_hz = hz;
    if (MOCK) return;
    return report({ ok: await Chan.start(hz), body: null }, (hz / 1e3).toFixed(0) + ' kHz');
  }
  hz = Math.max(S.tune_min_hz, Math.min(S.tune_max_hz, Math.round(hz)));
  if (MODE_IS_MOCK) { S.frequency_hz = hz; return; }
  const idx = S.rx;
  if (!S.fmOwns && M._m5_screen() === 1 && hz >= 87500000 && hz <= 108000000) {   // FM screen: tuning starts FM
    const sps = S.sps === 3200000 ? 3200000 : 2560000;   // an idle receiver's rate is whatever the last session left (often 300 kS/s): start wide
    return report(await post(`/api/v1/fm?action=start&index=${idx}&hz=${hz}&sps=${sps}&volume=${S.volume || 30}&tau=75`, 'fm-control'), 'FM ' + (hz / 1e6).toFixed(1) + ' MHz');
  }
  if (S.fmOwns) {
    if (hz < 87500000 || hz > 108000000) return toast('FM is playing; stop it to tune outside 87.5–108 MHz');
    return report(await post(`/api/v1/fm?action=start&index=${idx}&hz=${hz}&sps=${S.sps === 300000 ? 300000 : 2560000}&volume=${S.volume}&tau=75`, 'fm-control'), (hz / 1e6).toFixed(3) + ' MHz');
  }
  return report(await post(`/api/v1/receivers/tune?index=${idx}&hz=${hz}`, 'rx-tune'), (hz / 1e6).toFixed(3) + ' MHz');
}
async function setVolume(v) {
  v = Math.max(0, Math.min(80, v));
  if (MODE_IS_MOCK || Chan.active) { S.volume = v; S.muted = v === 0; return; }
  if (!S.fmOwns) return toast('No audio session: start FM on this receiver first');
  return report(await post(`/api/v1/fm?action=volume&volume=${v}`, 'fm-control'), v ? 'Volume ' + v : 'Muted');
}
function report(r, what) { if (!r.ok) toast(what + ': ' + errText(r)); return r.ok; }
let MODE_IS_MOCK = MOCK;

// Actions that never reach the node: display preferences and screens not ported yet.
function clientAction(name, value) {
  switch (name) {
    case 'step_size_up': case 'step_size_down': {
      let i = STEPS.indexOf(S.step_hz); if (i < 0) i = STEPS.findIndex(s => s >= S.step_hz);
      S.step_hz = STEPS[Math.max(0, Math.min(STEPS.length - 1, i + (name === 'step_size_up' ? 1 : -1)))]; return;
    }
    case 'filter_set':
      if (M._m5_screen() === 2) { S.amBw = value; Chan.setMode('AM', value); return; }
      S.filter_bandwidth_hz = value; return toast('Filter is shown only; the node has no channel filter control yet');
    case 'filter_standard': return toast('Filter standards are not applied by the node yet');
    case 'filter_edges': S.edges = !S.edges; return;
    case 'preset_recall': { const hz = M._m5_am_preset(value); if (hz) return tuneTo(hz); return; }
    case 'preset_save': M._m5_am_save_preset(); return;
    case 'preset_replace': M._m5_am_replace_preset(value); return;
    case 'preset_delete': M._m5_am_delete_preset(value); return;
    case 'scan_spacing_toggle': M._m5_am_toggle_spacing(); return;
    case 'scan_toggle': return toast('Band scan needs a node-side sweep of the AM band (later)');
    case 'export_log': offerExports(); return;
    case 'step_cycle': case 'step_cycle_previous': {
      if (M._m5_screen() === 2) { M._m5_am_cycle_step(); return; }
      const steps = [50000, 100000, 200000]; const i = steps.indexOf(S.fmStep);
      S.fmStep = steps[(i + (name === 'step_cycle' ? 1 : steps.length - 1) + steps.length) % steps.length]; return;
    }
    case 'save_preset': {
      const i = S.presets.indexOf(S.frequency_hz);
      if (i >= 0) { S.presets.splice(i, 1); toast('Preset removed'); }
      else if (S.presets.length >= 10) toast('Ten presets is the limit');
      else { S.presets.push(S.frequency_hz); toast('Preset saved (kept in this browser)'); }
      savePresets(); return;
    }
    case 'seek_up': case 'seek_down': {
      const p = [...S.presets].sort((a, b) => a - b);
      const next = name === 'seek_up' ? p.find(x => x > S.frequency_hz) : [...p].reverse().find(x => x < S.frequency_hz);
      if (next === undefined) return toast('Seek jumps between saved presets; the node has no station scan yet');
      return tuneTo(next);
    }
    case 'filter_up': case 'filter_down': return toast('The FM channel filter is fixed at 180 kHz on the node');
    case 'graphics_toggle': S.graphics = !S.graphics; return;
    case 'recording_toggle': return toast('Recording is not available from the browser yet');
    case 'scan_presets': return toast('Preset scanning needs a node-side station scan (later)');
    case 'mode_set': {   // OrcSDR's Home pins the demodulator for the band; a browser audio session follows it
      const m = value | 0;
      if (m < 0 || m > 5) return;
      return post(`/api/v1/home?action=mode_set&value=${m}`, 'home-action').then(r => {
        if (r.ok) { S.mode_choice = m; S.mode_pin_band = bandOf(S.frequency_hz)[2]; }
        return report(r, 'Mode ' + MODE_NAMES[m]);
      });
    }
    case 'open_dashboard': {
      const title = M.UTF8ToString(M._m5_dashboard_title(window.__dash || 0)) || 'That screen';
      // Weather, Marine and Satellite have no screen of their own on the device either: main.cpp's draw_sdr_screen() shows Home with the
      // receiver tuned to the band (162.400 MHz NOAA weather, 156.800 MHz marine channel 16, 137.500 MHz weather satellites).
      const home = { WEATHER: [162400000, 'NOAA weather radio'], MARINE: [156800000, 'marine channel 16'], SATELLITE: [137500000, 'the weather satellite band'] }[title];
      if (home) { toast('Home tuned to ' + home[1]); return tuneTo(home[0]); }   // Escape closes the dashboards list
      return toast(title + ' is not in this build yet');
    }
    case 'open_device_settings': return M._m5_open_settings();
    case 'waterfall_contrast_down': case 'waterfall_contrast_up': case 'waterfall_palette_next':
    case 'waterfall_speed_next': case 'gain_open': case 'gain_close': case 'filter_open': case 'filter_close':
    case 'mode_open': case 'mode_close': case 'keypad_open': case 'open_browser': case 'close_browser':
    case 'browser_previous': case 'browser_next': return;   // handled inside the UI
    default: return toast('Action not wired: ' + name);
  }
}

const Mock = {
  name: 'mock',
  t0: performance.now(),
  async refresh() {
    S.receivers = [{ index: 0, present: true }, { index: 1, present: true }];
    S.present = true; S.streaming = true; S.rxLabel = 'RTL ' + (S.rx ? 'Blog V4' : 'V3c'); S.net = true; S.ip = '192.168.1.117';
    S.tune_min_hz = 24000000; S.tune_max_hz = 1766000000; S.sps = S.span_hz;
    S.gain_available = S.gain_auto_available = S.rtl_agc_available = S.bias_available = true;
    S.gains = [0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254, 280, 297, 328, 338, 364, 372, 386, 402, 421, 434, 439, 445, 480, 496].map(x => x * 1);
    if (S.gain_auto) S.gain_tenth_db = 300;
    S.rf_dbfs = -42 + 3 * Math.sin((performance.now() - this.t0) / 700);
    S.fmPlaying = S.fmOwns = M._m5_screen() !== 2;
    S.fmStat = { queue_drops: 0, tx_queue_overflows: 0, dsp_us: 1300, block_us: 6000, audio_rms_dbfs: -22 + 4 * Math.sin((performance.now() - this.t0) / 300) };
    $('src').textContent = 'mock data (no node)';
  },
  async spectrum() {
    const t = (performance.now() - this.t0) / 1000, bins = new Uint8Array(BINS);
    if (M._m5_screen() === 3) {
      const lo0 = CB_CENTER - CB_RX_SPS / 2, hz = [27185000, 27065000, 27405000, 26965000, 27355000];
      for (let i = 0; i < BINS; i++) {
        const f = lo0 + (i + .5) * CB_RX_SPS / BINS;
        let db = -86 + 2 * Math.sin(i * 1.7 + t * 5) * Math.sin(i * .31);
        hz.forEach((c, k) => { if (Math.sin(t * (.5 + k * .3)) > (k ? .2 : -2)) db += 40 * Math.exp(-(((f - c) / 3000) ** 2)); });
        bins[i] = Math.max(1, Math.min(255, Math.round(1 + (db + 80) * 254 / 70)));
      }
      lastRow = { start: lo0, stop: lo0 + CB_RX_SPS, bins }; $('spec').textContent = 'synthetic'; return;
    }
    const lo = S.frequency_hz - S.span_hz / 2;
    for (let i = 0; i < BINS; i++) {
      const f = lo + (i + 0.5) * S.span_hz / BINS;
      let db = -86 + 2.5 * Math.sin(i * 1.7 + t * 5) * Math.sin(i * 0.31);
      for (const [c, w, a] of [[S.frequency_hz, 40000, 40], [98700000, 60000, 28], [99500000, 80000, 20], [97300000, 50000, 24]])
        db += a * Math.exp(-(((f - c) / w) ** 2));
      bins[i] = Math.max(1, Math.min(255, Math.round(1 + (db + 80) * 254 / 70)));
    }
    lastRow = { start: lo, stop: lo + S.span_hz, bins };
    $('spec').textContent = 'synthetic';
  },
  async do(name, value) {
    switch (name) {
      case 'tune_frequency': return tuneTo(value);
      case 'step_up': return tuneTo(S.frequency_hz + curStep());
      case 'step_down': return tuneTo(S.frequency_hz - curStep());
      case 'span_up': case 'span_down': S.span_hz = Math.max(225000, Math.min(3200000, M._m5_step_span(S.span_hz, name === 'span_up' ? 1 : -1))); return;
      case 'volume_up': case 'volume_down': return setVolume(S.volume + (name === 'volume_up' ? 5 : -5));
      case 'sound_toggle': return S.muted ? setVolume(S.prevVolume || 40) : (S.prevVolume = S.volume, setVolume(0));
      case 'gain_auto': S.gain_auto = !S.gain_auto; return;
      case 'gain_tenth_db': S.gain_auto = false; S.gain_tenth_db = value; return;
      case 'rtl_agc': S.rtl_agc = !!value; return;
      default: return clientAction(name, value);
    }
  },
};
const node = MOCK ? Mock : Live;

// ---- input ------------------------------------------------------------------------------------
async function dispatch(kind) {
  if (!kind) return;
  const name = M.UTF8ToString(M._m5_action_name()), value = M._m5_action_value();
  window.__dash = M._m5_action_dashboard();
  if (M._m5_screen() === 2 && (name === 'span_up' || name === 'span_down')) {   // AM scope zoom: display only
    const sp = [30000, 60000, 120000, 240000], i = Math.max(0, sp.indexOf(S.amSpan));
    S.amSpan = sp[Math.max(0, Math.min(3, i + (name === 'span_up' ? 1 : -1)))];
    return;
  }
  try { await node.do(name, value); } catch (e) { toast(name + ': ' + e.message); }
  await refreshSoon();
}
function pt(e) { const r = canvas.getBoundingClientRect(); return [Math.round((e.clientX - r.left) * W / r.width), Math.round((e.clientY - r.top) * H / r.height)]; }
let down = false;
canvas.addEventListener('pointerdown', e => { canvas.setPointerCapture(e.pointerId); canvas.focus(); Listen.prime(); down = true; const [x, y] = pt(e); dispatch(M._m5_touch(x, y, 1)); });
canvas.addEventListener('pointermove', e => {
  if (!down) return; const [x, y] = pt(e);
  if (M._m5_screen() !== 0) dispatch(M._m5_drag(x, y)); else M._m5_touch(x, y, 1);
});
canvas.addEventListener('pointerup', e => { if (!down) return; down = false; const [x, y] = pt(e); dispatch(M._m5_touch(x, y, 0)); });
canvas.addEventListener('pointercancel', () => { down = false; });
window.addEventListener('keydown', e => {
  if (e.target.tagName === 'SELECT') return;
  let ch = 0;
  if (/^[0-9.]$/.test(e.key) || (e.key.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey)) ch = e.key.charCodeAt(0);   // letters and punctuation reach the text editor
  else if (e.key === 'Enter') ch = 13; else if (e.key === 'Escape') ch = 27; else if (e.key === 'Backspace') ch = 8;
  if (!ch) return;
  e.preventDefault(); dispatch(M._m5_key(ch));
});

// ---- loops ------------------------------------------------------------------------------------
let refreshing = false, again = false;
async function refreshSoon() {
  if (refreshing) { again = true; return; }
  refreshing = true;
  try { await node.refresh(); $('err').style.display = 'none'; } catch (e) {
    const el = $('err'); el.style.display = 'flex'; el.textContent = 'Cannot reach the node: ' + e.message;
  }
  refreshing = false;
  if (again) { again = false; refreshSoon(); }
}

// ---- browser audio: the node's 48 kHz mono PCM over the port-81 WebSocket ----------------------
// Same packet format and playback scheduling as the classic page (OFA1 header, 24 bytes, int16 PCM).
class PcmPlayer {
  constructor(context) {
    this.context = context; this.gain = context.createGain(); this.gain.connect(context.destination);
    this.sources = new Set(); this.session = this.seq = this.first = null; this.stopped = false;
    this.nextTime = 0; this.underruns = 0; this.resyncs = 0; this.gaps = 0; this.frames = 0;
  }
  clear() { for (const s of this.sources) { try { s.stop(); } catch {} s.disconnect(); } this.sources.clear(); this.nextTime = 0; }
  reset() { this.clear(); this.session = this.seq = this.first = null; this.stopped = false; }
  volume(unit) { this.gain.gain.setValueAtTime(Math.max(0, Math.min(1, unit)), this.context.currentTime); }
  pushFloat(samples) {   // 48 kHz mono floats produced in the page (channel demodulator)
    const now = this.context.currentTime;
    if (this.nextTime && this.nextTime < now + .015) { this.clear(); ++this.underruns; }
    if (this.nextTime - now > .28) { this.clear(); ++this.resyncs; }
    if (!this.nextTime) this.nextTime = now + .12;
    const buffer = this.context.createBuffer(1, samples.length, 48000);
    buffer.copyToChannel(samples, 0);
    const src = this.context.createBufferSource(); src.buffer = buffer; src.connect(this.gain);
    this.sources.add(src); src.onended = () => { src.disconnect(); this.sources.delete(src); };
    src.start(this.nextTime); this.nextTime += samples.length / 48000; this.frames += samples.length;
  }
  accept(data) {
    if (!(data instanceof ArrayBuffer) || data.byteLength < 24) return false;
    const v = new DataView(data), frames = v.getUint16(20, true), flags = v.getUint16(22, true);
    if (v.getUint32(0, true) !== 0x3141464f || v.getUint32(16, true) !== 48000 || frames > 960 || flags > 1 || data.byteLength !== 24 + 2 * frames) return false;
    const session = v.getUint32(4, true), seq = v.getUint32(8, true), first = v.getUint32(12, true);
    if (this.session !== session) { this.clear(); this.session = session; this.seq = this.first = null; this.stopped = false; }
    if (!flags) { this.clear(); this.stopped = true; return false; }
    if (!frames || this.stopped) return false;
    const delta = this.seq === null ? 1 : (seq - this.seq) >>> 0;
    if (!delta || delta >= 0x80000000) return false;
    if (this.first !== null && first !== this.first) { this.gaps += delta - 1; this.clear(); ++this.resyncs; }
    const now = this.context.currentTime;
    if (this.nextTime && this.nextTime < now + .015) { this.clear(); ++this.underruns; }
    if (this.nextTime - now > .28) { this.clear(); ++this.resyncs; }
    if (!this.nextTime) this.nextTime = now + .12;
    const buffer = this.context.createBuffer(1, frames, 48000), pcm = buffer.getChannelData(0);
    for (let i = 0; i < frames; i++) pcm[i] = v.getInt16(24 + 2 * i, true) / 32768;
    const src = this.context.createBufferSource(); src.buffer = buffer; src.connect(this.gain);
    this.sources.add(src); src.onended = () => { src.disconnect(); this.sources.delete(src); };
    src.start(this.nextTime); this.nextTime += frames / 48000; this.seq = seq; this.first = (first + frames) >>> 0;
    this.frames += frames; return true;
  }
}
const Listen = {
  player: null, socket: null, wanted: false, retry: 0, packets: 0,
  async prime() {   // create/resume the audio context; must be reached from a user gesture the first time
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return false;
    if (!this.player) this.player = new PcmPlayer(new AC());
    try { await this.player.context.resume(); } catch {}
    return true;
  },
  url() { return (location.protocol === 'https:' ? 'wss://' : 'ws://') + (q.get('ws') || location.hostname) + ':81/api/v1/fm/audio'; },
  async toggle() {
    if (this.wanted) return this.stop();
    if (MOCK) return toast('Mock mode has no audio');
    if (Chan.active) return toast('A channel session is using the browser audio slot; leave the AM screen first');
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return toast('Web Audio is unavailable in this browser');
    if (!this.player) this.player = new PcmPlayer(new AC());
    await this.player.context.resume();     // allowed: this runs from a click
    this.wanted = true; this.setVolume(); this.open();
    $('listen').classList.add('on'); $('listen').textContent = 'Stop listening';
  },
  open() {
    if (!this.wanted || this.socket) return;
    const s = new WebSocket(this.url()); s.binaryType = 'arraybuffer'; this.socket = s;
    $('audiostat').textContent = 'connecting…';
    s.onmessage = e => {
      if (s !== this.socket) return;
      try {
        if (this.player.accept(e.data)) {
          if (++this.packets % 25 === 0) $('audiostat').textContent = 'playing · ' + Math.round(Math.max(0, this.player.nextTime - this.player.context.currentTime) * 1000) + ' ms buffered';
        } else if (this.player.stopped) $('audiostat').textContent = 'FM stopped';
      } catch { s.close(); }
    };
    s.onclose = () => { if (s !== this.socket) return; this.socket = null; this.player.reset(); if (this.wanted) { $('audiostat').textContent = 'retrying…'; this.retry = setTimeout(() => this.open(), 1500); } };
    s.onerror = () => { if (s === this.socket) $('audiostat').textContent = 'one browser listener at a time'; };
  },
  stop() {
    this.wanted = false; clearTimeout(this.retry);
    const s = this.socket; this.socket = null; if (s) s.close();
    if (this.player) this.player.reset();
    $('listen').classList.remove('on'); $('listen').textContent = 'Listen here'; $('audiostat').textContent = '';
  },
  // The node's PCM is sent at a fixed level, so the page applies the same volume (0-80) and mute the speaker uses.
  setVolume() { if (this.player) this.player.volume(S.muted ? 0 : Math.min(1, S.volume / 80)); },
};
$('listen').addEventListener('click', () => Listen.toggle());
$('stopfm').addEventListener('click', async () => { const r = await post('/api/v1/fm?action=stop', 'fm-control'); report(r, 'Stop FM'); refreshSoon(); });
window.addEventListener('pagehide', () => Listen.stop());

// ---- channel session: AM / NFM / SSB demodulated in the page from a ~300 kS/s IQ window ---------
// The node parks one receiver on a window around the wanted frequency (offset a sixth of the rate away from
// the DC spike) and streams raw CU8 over :81/api/v1/fm/iq. Everything below runs in the browser.
class Biquad {
  constructor(fs, fc, q) {
    const w = 2 * Math.PI * fc / fs, c = Math.cos(w), a = Math.sin(w) / (2 * q), n = 1 / (1 + a);
    this.b0 = (1 - c) * .5 * n; this.b1 = (1 - c) * n; this.b2 = this.b0; this.a1 = -2 * c * n; this.a2 = (1 - a) * n;
    this.z1 = this.z2 = 0;
  }
  step(x) { const y = this.b0 * x + this.z1; this.z1 = this.b1 * x - this.a1 * y + this.z2; this.z2 = this.b2 * x - this.a2 * y; return y; }
}
const BW_Q = [.5412, 1.3066];   // 4th-order Butterworth as two biquads

class ChannelDemod {
  constructor(rate, offsetHz, mode, bwHz, int16 = false) {
    this.rate = rate; this.offset = offsetHz; this.mode = mode; this.bw = bwHz; this.int16 = int16; this.clar = 0;
    this.decim = Math.max(1, Math.round(rate / 50000)); this.fs2 = rate / this.decim;
    const cut = mode === 'AM' ? bwHz / 2 : mode === 'NFM' ? Math.min(8000, bwHz * .65) : 3400;
    this.fi = BW_Q.map(q => new Biquad(rate, cut, q)); this.fq = BW_Q.map(q => new Biquad(rate, cut, q));
    const w = -2 * Math.PI * offsetHz / rate; this.wr = Math.cos(w); this.wi = Math.sin(w); this.pr = 1; this.pi = 0; this.n = 0;
    // second stage at fs2
    this.aud = [new Biquad(this.fs2, mode === 'NFM' ? 3500 : mode === 'AM' ? Math.min(5000, bwHz / 2) : 2800, .7071)];
    this.avg = 0; this.dc = 0; this.lastI = this.lastQ = 0; this.have = false; this.phase = 0; this.prev = 0; this.gain = 1;
    this.sw = { fc: 1500, pr: 1, pi: 0, wr: Math.cos(2 * Math.PI * 1500 / this.fs2), wi: Math.sin(2 * Math.PI * 1500 / this.fs2) };
    this.swI = [new Biquad(this.fs2, 1450, .5412), new Biquad(this.fs2, 1450, 1.3066)];
    this.swQ = [new Biquad(this.fs2, 1450, .5412), new Biquad(this.fs2, 1450, 1.3066)];
    this.power = 0; this.powerN = 0; this.cp = 0; this.cpN = 0;
  }
  setClarifier(hz) {   // SSB carrier fine-tune: moves the mixer a little
    this.clar = hz; const w = -2 * Math.PI * (this.offset + hz) / this.rate; this.wr = Math.cos(w); this.wi = Math.sin(w);
  }
  // iq: Uint8Array of interleaved CU8, or Int16Array of interleaved I,Q (1/128 count units) when int16; returns 48 kHz audio
  process(iq) {
    const out = new Float32Array(Math.ceil(iq.length / 2 / this.rate * 48000) + 8);
    let o = 0;
    const usb = this.mode === 'USB';
    for (let k = 0; k < iq.length; k += 2) {
      let i, q;
      if (this.int16) { i = iq[k] / 128; q = iq[k + 1] / 128; } else { i = iq[k] - 127.5; q = iq[k + 1] - 127.5; }
      this.power += i * i + q * q; this.powerN++;
      // mix the channel to DC
      const mi = i * this.pr - q * this.pi, mq = i * this.pi + q * this.pr;
      const nr = this.pr * this.wr - this.pi * this.wi; this.pi = this.pr * this.wi + this.pi * this.wr; this.pr = nr;
      if ((++this.n & 4095) === 0) { const m = Math.hypot(this.pr, this.pi); this.pr /= m; this.pi /= m; }
      i = this.fi[1].step(this.fi[0].step(mi)); q = this.fq[1].step(this.fq[0].step(mq));
      if (this.n % this.decim) continue;
      this.cp += i * i + q * q; this.cpN++;
      let a;
      if (this.mode === 'AM') {
        const env = Math.hypot(i, q);
        this.avg += (env - this.avg) * 0.0005;
        a = (env - this.avg) / Math.max(this.avg, 4);
      } else if (this.mode === 'NFM') {
        let d = 0;
        if (this.have) d = Math.atan2(q * this.lastI - i * this.lastQ, i * this.lastI + q * this.lastQ);
        this.lastI = i; this.lastQ = q; this.have = true;
        a = d / (2 * Math.PI * 4000 / this.fs2);
      } else {   // SSB by the Weaver method: shift the passband to be centred on DC, low-pass, shift back, take the real part
        const s = this.sw, sign = usb ? 1 : -1;
        const ci = i * s.pr + sign * q * s.pi, cq = q * s.pr - sign * i * s.pi;   // multiply by e^{-j*sign*2*pi*fc*t}
        const nr = s.pr * s.wr - s.pi * s.wi; s.pi = s.pr * s.wi + s.pi * s.wr; s.pr = nr;
        if ((this.n & 8191) === 0) { const m = Math.hypot(s.pr, s.pi); s.pr /= m; s.pi /= m; }
        const li = this.swI[1].step(this.swI[0].step(ci)), lq = this.swQ[1].step(this.swQ[0].step(cq));
        this.sagc = (this.sagc || 0) * .9995 + Math.hypot(li, lq) * .0005;
        a = (li * s.pr - sign * lq * s.pi) / Math.max(this.sagc, 8);   // back-shift, real part, AGC
      }
      a = this.aud[0].step(a);
      // resample fs2 -> 48 kHz (linear)
      this.phase += 48000;
      while (this.phase >= this.fs2) {
        this.phase -= this.fs2;
        const f = this.phase / 48000;   // how far past the output instant the newest sample is
        out[o++] = Math.max(-1, Math.min(1, (this.prev + (a - this.prev) * (1 - f)) * this.gain));
      }
      this.prev = a;
    }
    return out.subarray(0, o);
  }
  // Level inside the channel filter only (what the squelch and S-meter should follow), relative to CU8 full scale.
  takeChannelDb() { const p = this.cpN ? this.cp / this.cpN : 0; this.cp = 0; this.cpN = 0; return p > 0 ? 10 * Math.log10(p / (127.5 * 127.5 * 2)) : -90; }
  takePowerDb() { const p = this.powerN ? this.power / this.powerN : 0; this.power = 0; this.powerN = 0; return p > 0 ? 10 * Math.log10(p / (127.5 * 127.5 * 2)) : -90; }
}

const Chan = {
  active: false, starting: false, ws: null, demod: null, session: null, seq: null, params: null, mode: 'AM', bw: 10000,
  gainTrim: 1.0, packets: 0, gaps: 0,
  wsUrl() { return (location.protocol === 'https:' ? 'wss://' : 'ws://') + (q.get('ws') || location.hostname) + ':81/api/v1/fm/iq'; },
  async start(hz, opts = {}) {
    if (this.starting) { this.pending = { hz, opts }; return false; }
    this.starting = true;
    try {
      if (MOCK) { this.active = true; return true; }
      Listen.stop();                                   // the node has one IQ/audio listener slot
      const wide = opts.center ? `&center=${opts.center}&rx_sps=${opts.rx_sps}` : opts.sps ? `&sps=${opts.sps}` : '';
      const r = await post(`/api/v1/channel?index=${S.rx}&hz=${hz}${wide}`, 'channel');
      if (!r.ok) { toast('Channel: ' + errText(r)); return false; }
      this.params = r.body; this.active = true; this.session = null; this.seq = null;
      this.demod = this.iqOnly ? null : new ChannelDemod(r.body.sps, r.body.offset_hz, this.mode, this.bw);   // a decoder taking IQ needs no audio
      S.chanHz = r.body.hz;
      await Listen.prime();
      this.openSocket();
      $('audiostat').textContent = this.mode + ' via browser';
      return true;
    } finally {
      this.starting = false;
      if (this.pending) { const q2 = this.pending; this.pending = null; queueMicrotask(() => this.start(q2.hz, q2.opts)); }
    }
  },
  openSocket() {
    if (this.ws || !this.active) return;
    const s = new WebSocket(this.wsUrl()); s.binaryType = 'arraybuffer'; this.ws = s;
    s.onmessage = e => this.packet(e.data);
    s.onclose = () => { if (this.ws === s) this.ws = null; if (this.active) setTimeout(() => this.openSocket(), 1500); };
  },
  packet(d) {
    if ((!this.demod && !this.iqOnly) || !(d instanceof ArrayBuffer) || d.byteLength < 28) return;
    const v = new DataView(d);
    if (v.getUint32(0, true) !== 0x3151494f) return;
    const frames = v.getUint16(20, true), playing = v.getUint16(22, true), session = v.getUint32(4, true), seq = v.getUint32(8, true);
    if (!playing || d.byteLength !== 28 + frames * 2) return;
    if (session !== this.session) { this.session = session; this.seq = null; this.rebuild(); }
    if (this.seq !== null && ((seq - this.seq) >>> 0) !== 1) { this.gaps++; }
    this.seq = seq; this.packets++;
    if (this.rawTap && v.getUint32(24, true) !== 16) this.rawTap(new Uint8Array(d, 28));   // RF Lab measures the raw 8-bit IQ
    if (this.iqOnly) return;
    const audio = this.demod.process(v.getUint32(24, true) === 16 ? new Int16Array(d, 28, frames) : new Uint8Array(d, 28));
    if (this.tap && audio.length) this.tap(audio);   // the browser-side decoders take the audio before any listening filter
    if (this.gate) audio.fill(0);   // squelch
    if (S.swScreen && audio.length) this.shortwaveDsp(audio);
    else if (S.abScreen && audio.length) this.airbandFilter(audio);
    if (audio.length && Listen.player) Listen.player.pushFloat(audio);
    if (this.packets % 8 === 0) { const ch = this.demod.takeChannelDb(); S.chanDb = ch; if (!S.cbScreen) S.rf_dbfs = ch; }
  },
  airbandFilter(audio) {   // OrcSDR's 300 Hz - 3.2 kHz voice band-pass (compiled into core.wasm)
    const p = M._malloc(audio.length * 4);
    M.HEAPF32.set(audio, p >> 2); M._m5_ab_audio(p, audio.length);
    audio.set(M.HEAPF32.subarray(p >> 2, (p >> 2) + audio.length)); M._free(p);
  },
  shortwaveDsp(audio) {   // OrcSDR's shortwave noise reduction / notch / squelch (compiled into core.wasm), then the +12 dB boost
    const bytes = audio.length * 4, p = M._malloc(bytes);
    M.HEAPF32.set(audio, p >> 2);
    M._m5_sw_dsp(p, audio.length, S.chanDb !== undefined ? S.chanDb : -90);
    audio.set(M.HEAPF32.subarray(p >> 2, (p >> 2) + audio.length));
    M._free(p);
    if (this.boost > 1) for (let i = 0; i < audio.length; i++) audio[i] = Math.max(-1, Math.min(1, audio[i] * this.boost));
  },
  rebuild() { if (this.params && !this.iqOnly) { this.demod = new ChannelDemod(this.params.sps, this.params.offset_hz, this.mode, this.bw); this.demod.setClarifier(this.clar || 0); } },
  setMode(mode, bw) { if (mode === this.mode && bw === this.bw) return; this.mode = mode; this.bw = bw; this.rebuild(); },
  setClarifier(hz) { this.clar = hz; if (this.demod) this.demod.setClarifier(hz); },
  async stop() {
    if (!this.active) return;
    this.active = false;
    const s = this.ws; this.ws = null; if (s) s.close();
    if (Listen.player) Listen.player.reset();
    if (!MOCK) { try { await post('/api/v1/channel/stop', 'channel'); } catch {} }
    $('audiostat').textContent = '';
  },
};

let lastScreen = 0;
function watchScreen() {
  const sc = M._m5_screen();
  if (window.OrcDataScreens) window.OrcDataScreens.tick(sc);   // decoder dashboards (screen 6) are driven from decoders.js
  if (sc === lastScreen) {
    if (sc === 2 && !Chan.active && !Chan.starting && !S.amTried) { S.amTried = true; Chan.start(S.frequency_hz); }
    return;
  }
  if (lastScreen === 2 || lastScreen === 3 || lastScreen === 4 || lastScreen === 5) { Chan.stop(); Chan.gate = false; Chan.boost = 1; S.amTried = false; S.chanDb = undefined; }
  if (sc === 2) {
    S.frequency_hz = M._m5_am_saved_frequency(); S.amTried = true; S.sps = 300000;
    if (S.frequency_hz < 520000 || S.frequency_hz > 1710000) S.frequency_hz = 1000000;
    Chan.start(S.frequency_hz);
  }
  if (sc === 5) { S.sps = 300000; if (MOCK) Chan.active = true; M._m5_ab_audio_reset(); }
  if (sc === 4) { S.sps = 300000; S.frequency_hz = M._m5_sw_freq(); if (MOCK) Chan.active = true; }
  if (sc === 3) {
    S.sps = CB_RX_SPS; S.frequency_hz = M._m5_cb_freq();
    // The dashboard queues its saved channel as a tune; the first one starts the wide session.
    if (MOCK) Chan.active = true;
  }
  lastScreen = sc;
}
function fillSelector() {
  $('stopfm').style.display = S.fmOwns && !MOCK ? '' : 'none';
  $('abtools').style.display = S.abScreen ? '' : 'none';
  const sel = $('rx'); const n = S.receivers.length;
  if (sel.options.length !== n) {
    sel.innerHTML = '';
    for (let i = 0; i < n; i++) { const o = document.createElement('option'); o.value = i; sel.appendChild(o); }
  }
  for (let i = 0; i < n; i++) sel.options[i].textContent = '#' + i + (S.receivers[i].present ? ' ' + (S.receivers[i].label || 'RTL-SDR') : ' (empty)');
  sel.value = S.rx;
}
$('rx').addEventListener('change', e => { S.rx = +e.target.value; lastRow = null; refreshSoon(); });
if (API) $('classic').href = API + '/classic';

// The device's boot splash: the same image, a status strip, and the OK button once the page is ready (OrcSDR's
// orcsdr_splash). It shows once per browser tab session, goes away on a tap or Enter, and on its own a few seconds
// after it is ready so a reload never waits on a person.
const Splash = {
  el: $('splash'), done: false,
  status(t) { const s = $('splash-status'); if (s) s.textContent = t; },
  ready() {
    if (this.done) return;
    this.status('Ready');
    $('splash-go').style.display = 'block';
    this.timer = setTimeout(() => this.end(), 6000);
  },
  end() {
    if (this.done) return;
    this.done = true; clearTimeout(this.timer);
    this.el.classList.add('gone');
    try { sessionStorage.setItem('m5splash', '1'); } catch {}
    setTimeout(() => { this.el.style.display = 'none'; $('c').focus(); }, 400);
  },
};
try { if (sessionStorage.getItem('m5splash') || /nosplash/.test(location.hash)) { Splash.done = true; Splash.el.style.display = 'none'; } } catch {}
$('splash-go').onclick = () => Splash.end();
addEventListener('keydown', e => { if (!Splash.done && (e.key === 'Enter' || e.key === ' ')) { e.preventDefault(); Splash.end(); } }, true);

// Basemap: the board serves the PMTiles archive from its SD card in signed chunks (apps/orcsdr-p4-core/main/web_maps.cpp). The page writes it
// into the WebAssembly virtual filesystem where the shared map code reads it, then asks the map service to rescan. Status is shown bottom left.
const MapSync = {
  el: null, done: false,
  say(text) {
    if (!this.el) {
      this.el = document.createElement('div');
      this.el.style.cssText = 'position:fixed;left:6px;bottom:6px;z-index:9000;opacity:.6;padding:2px 8px;font:12px sans-serif;background:#0b1620;color:#e5e7eb;border:1px solid #334155;border-radius:4px';
      document.body.appendChild(this.el);
    }
    this.el.textContent = text; this.el.style.display = text ? '' : 'none';
  },
  async run() {
    if (MOCK || this.done || !M.FS || !M._m5_maps_rescan) return;
    this.done = true;
    try {
      const st = await (await fetch('/api/v1/map/status', { method: 'POST' })).json();
      if (!st.ok) { this.say('Basemap: not available'); return; }
      if (!st.sd) { this.say('Basemap: no SD card in the board'); return; }
      if (!st.present) { this.say('Basemap: no basemap.pmtiles on the SD card'); return; }
      const CHUNK = 32768, data = new Uint8Array(st.bytes);
      for (let off = 0; off < st.bytes; off += CHUNK) {
        const len = Math.min(CHUNK, st.bytes - off);
        const r = await fetch('/api/v1/map/read?offset=' + off + '&len=' + len, { method: 'POST' });
        if (!r.ok) { this.say('Basemap: download failed (' + r.status + ')'); return; }
        const part = new Uint8Array(await r.arrayBuffer());
        if (part.length !== len) { this.say('Basemap: short read, not loaded'); return; }
        data.set(part, off);
        if (((off / CHUNK) & 15) === 0) this.say('Basemap: loading ' + Math.floor(100 * off / st.bytes) + '%');
      }
      try { M.FS.mkdir('/orcmaps'); } catch {}
      M.FS.writeFile('/orcmaps/browser.pmtiles', data);
      M._m5_maps_rescan();
      this.say('Basemap: loaded (' + (st.bytes / 1048576).toFixed(1) + ' MB)');
      setTimeout(() => this.say(''), 8000);
    } catch (e) { this.say('Basemap: ' + (e && e.message ? e.message : 'error')); }
  }
};

function start() {
  Splash.status('Loading…');
  MapSync.run();
  if (!M._m5_init()) { toast('Could not allocate the 1280x720 canvas'); return; }
  nvsImport();
  Promise.resolve(refreshSoon()).catch(() => {}).then(() => Splash.ready());
  setInterval(refreshSoon, 1000);
  let specBusy = false;
  setInterval(async () => {                 // 10 Hz: the UI's smoothing assumes 100 ms per row
    if (!specBusy) { specBusy = true; try { await node.spectrum(); } catch {} specBusy = false; }
    const peak = fillLevels();
    if (!MOCK && !S.fmOwns) S.rf_dbfs = peak;
    Listen.setVolume();
    watchScreen();
    nvsSave();
    fsSave();
    pushSnapshot();
    M._m5_draw_spectrum(viewFloor, 0);
  }, 100);
  const frame = () => { blit(); setTimeout(() => requestAnimationFrame(frame), 33); };
  frame();
  setInterval(fillSelector, 1000);
  window.m5 = { S, M, node, mock: MOCK };
  window.m5app = { MOCK, API, S, Chan, ChannelDemod, post, jget, toast };
  const decoders = document.createElement('script');   // after m5app exists: the decoder panel needs it
  decoders.src = '/m5/decoders.js';
  document.body.appendChild(decoders);
}

if (typeof M5Core !== 'function') { $('err').style.display = 'flex'; $('err').textContent = 'core.js did not load'; }
else M5Core().then(m => { M = m; start(); });
})();
