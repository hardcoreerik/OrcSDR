// Runs one orcdec decoder (components/orcdec, the same source as the firmware's) in a Web Worker.
//   in : {t:'init', kind, options}   {t:'feed', data: ArrayBuffer}   {t:'cmd', cmd, arg}   {t:'close'}
//   out: {t:'ready', version, rate, bytes}   {t:'events', lines: [json...], images: [{seq, width, height, data}]}   {t:'error', error}
// kind: 1 POCSAG (CU8 IQ 960 kS/s), 2 FT8 (s16 12 kS/s), 3 SSTV (s16 48 kS/s).
'use strict';
importScripts('orcdec.js');

let M = null, dec = 0, bytesPer = 2, buf = 0, bufCap = 0;
let lineBuf = 0, sizePtr = 0;
const queue = [];
let loading = null;

const cstr = s => { const n = M.lengthBytesUTF8(s) + 1, p = M._malloc(n); M.stringToUTF8(s, p, n); return p; };

function drain() {
  const lines = [], images = [];
  for (let n; (n = M._orcdec_poll(dec, lineBuf, 8192)) !== 0;) {
    if (n < 0) continue;
    const line = M.UTF8ToString(lineBuf);
    lines.push(line);
    if (line.includes('"type":"image"')) {
      const ev = JSON.parse(line), p = M._orcdec_attachment(dec, ev.seq, sizePtr);
      if (p) {
        const len = M.HEAPU32[sizePtr >> 2];
        images.push({ seq: ev.seq, width: ev.width, height: ev.height, data: M.HEAPU8.slice(p, p + len).buffer });
      }
    }
  }
  if (lines.length) postMessage({ t: 'events', lines, images }, images.map(i => i.data));
}

function handle(m) {
  if (m.t === 'init') {
    if (dec) { M._orcdec_destroy(dec); dec = 0; }
    const info = M._malloc(32);
    M._orcdec_info(m.kind, info);
    const rate = M.HEAP32[(info >> 2) + 2];
    bytesPer = M.HEAP32[(info >> 2) + 3];
    M._free(info);
    const opt = m.options ? cstr(m.options) : 0;
    dec = M._orcdec_create(m.kind, opt);
    if (opt) M._free(opt);
    if (!dec) { postMessage({ t: 'error', error: 'cannot create decoder (SSTV allows one at a time)' }); return; }
    postMessage({ t: 'ready', version: M.UTF8ToString(M._orcdec_version()), rate, bytes: bytesPer });
  } else if (!dec) {
    return;
  } else if (m.t === 'feed') {
    const u8 = new Uint8Array(m.data);
    for (let pos = 0; pos < u8.length;) {
      const n = Math.min(u8.length - pos, 65536);
      if (n > bufCap) { if (buf) M._free(buf); buf = M._malloc(n); bufCap = n; }
      M.HEAPU8.set(u8.subarray(pos, pos + n), buf);
      M._orcdec_feed(dec, buf, n - (n % bytesPer));
      pos += n;
      drain();
    }
  } else if (m.t === 'cmd') {
    const name = cstr(m.cmd), arg = m.arg || 0;
    M._orcdec_command(dec, name, arg % 4294967296 >>> 0, Math.floor(arg / 4294967296));   // uint64 as two 32-bit halves
    M._free(name);
    drain();
    postMessage({ t: 'cmd_done', cmd: m.cmd });
  } else if (m.t === 'close') {
    M._orcdec_destroy(dec); dec = 0;
    postMessage({ t: 'closed' });
  }
}

loading = OrcDec().then(m => {
  M = m;
  lineBuf = M._malloc(8192);
  sizePtr = M._malloc(4);
  for (const q of queue.splice(0)) handle(q);
}).catch(e => postMessage({ t: 'error', error: 'orcdec failed to load: ' + e }));

onmessage = e => { if (M) handle(e.data); else queue.push(e.data); };
