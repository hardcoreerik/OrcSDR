// Tests the browser channel IQ stream (main/web_iq.cpp) against a node: node tools/web_iq_test.js <device-ip> [hz] [seconds]
const FRAME_PAIRS = 2048;
// Logs in with the factory password, asks for a channel, opens the port-81 WebSocket like the page does and checks the packets.
const L = require('../apps/orcsdr-p4-core/web/orclogin.js');
const ip = process.argv[2], hz = Number(process.argv[3] || 96100000), secs = Number(process.argv[4] || 6), wideSps = Number(process.argv[5] || 0);   // wideSps: ask for the wide window the decoders use (centred on hz)
let fails = 0;
const check = (name, ok, detail) => { console.log((ok ? 'PASS ' : 'FAIL ') + name + (ok ? '' : ' - ' + detail)); if (!ok) fails++; };
const t = s => L.enc.encode(s), base = 'http://' + ip;
const sleep = ms => new Promise(r => setTimeout(r, ms));
function openWs() {
  return new Promise(res => {
    const ws = new WebSocket('ws://' + ip + ':81/api/v1/fm/iq'); ws.binaryType = 'arraybuffer';
    const st = { packets: [], opened: false, closed: false, ws };
    ws.onopen = () => { st.opened = true; res(st); };
    ws.onmessage = e => st.packets.push(new Uint8Array(e.data));
    ws.onclose = () => { st.closed = true; res(st); };
    ws.onerror = () => { };
  });
}
function fftPeak(pairs) {   // crude 1024-point spectrum: returns [bin of the peak, level ratio peak/median]
  const N = 1024, re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; i++) { const w = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N); re[i] = (pairs[2 * i] - 127.5) * w; im[i] = (pairs[2 * i + 1] - 127.5) * w; }
  for (let i = 1, j = 0; i < N; i++) { let bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; } }
  for (let len = 2; len <= N; len <<= 1) { const a = -2 * Math.PI / len; for (let i = 0; i < N; i += len) for (let k = 0; k < len / 2; k++) { const wr = Math.cos(a * k), wi = Math.sin(a * k), ur = re[i + k], ui = im[i + k], xr = re[i + k + len / 2], xi = im[i + k + len / 2], vr = xr * wr - xi * wi, vi = xr * wi + xi * wr; re[i + k] = ur + vr; im[i + k] = ui + vi; re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi; } }
  const p = Array.from(re, (r, i) => r * r + im[i] * im[i]), sorted = [...p].sort((a, b) => a - b);
  let pk = 0; for (let i = 1; i < N; i++) if (p[i] > p[pk]) pk = i;
  return [pk > N / 2 ? pk - N : pk, p[pk] / Math.max(sorted[N >> 1], 1e-9)];
}
(async () => {
  const get = async p => (await fetch(base + p, { headers: { Host: ip } })).json();
  const c = await get('/api/v1/auth/challenge'), key = L.pbkdf2(t('password'), L.unhex(c.salt), c.iterations), nonce = L.unhex(c.nonce);
  const lr = await (await fetch(base + '/api/v1/auth/login', { method: 'POST', body: 'nonce=' + c.nonce + '&proof=' + L.hex(L.loginProof(key, nonce)) })).json();
  check('login', lr.ok, JSON.stringify(lr));
  const sk = L.sessionKey(key, nonce); let seq = 0;
  const post = async target => { const s = ++seq; const x = await fetch(base + target, { method: 'POST', headers: { 'X-Orc-Session': lr.session, 'X-Orc-Seq': String(s), 'X-Orc-Sig': L.sign(sk, s, 'POST', target, new Uint8Array(0)) } }); return { status: x.status, body: await x.json() }; };

  const before = await openWs();
  check('the WebSocket is refused before a signed channel request', !before.opened, 'it opened'); if (before.opened) before.ws.close();

  const r = await post('/api/v1/channel?index=0&hz=' + hz + (wideSps ? '&center=' + hz + '&rx_sps=' + wideSps : ''));
  console.log('channel reply', JSON.stringify(r.body));
  check('channel accepted with its parameters', r.status === 200 && r.body.ok && r.body.sps >= 300000 && r.body.bits === 8 && (wideSps ? r.body.sps === wideSps && r.body.offset_hz === 0 : r.body.offset_hz > 0), JSON.stringify(r));
  if (!(r.status === 200 && r.body.ok)) { console.log(fails + ' failed'); process.exit(1); }
  const ws = await openWs();
  check('the WebSocket opens after the signed request', ws.opened);
  await sleep(secs * 1000);
  const pk = ws.packets, n = pk.length;
  console.log('packets received:', n, 'in', secs, 's =', (n / secs).toFixed(0), '/s');
  const hdr = b => new DataView(b.buffer, b.byteOffset, b.byteLength);
  const ok = pk.filter(b => b.length === 28 + FRAME_PAIRS * 2 && hdr(b).getUint32(0, true) === 0x3151494f && hdr(b).getUint16(20, true) === FRAME_PAIRS && hdr(b).getUint16(22, true) === 1 && hdr(b).getUint32(24, true) === 8 && hdr(b).getUint32(16, true) === r.body.sps);
  check('every packet is a well-formed 2048-pair 8-bit IQ frame at the announced rate', n > 0 && ok.length === n, ok.length + ' of ' + n);
  const expected = r.body.sps / FRAME_PAIRS * secs;
  check('the packet rate matches the sample rate (within 5 %)', Math.abs(n - expected) < 0.05 * expected, n + ' vs ' + expected.toFixed(0));
  let gaps = 0; for (let i = 1; i < n; i++) if (((hdr(pk[i]).getUint32(8, true) - hdr(pk[i - 1]).getUint32(8, true)) >>> 0) !== 1) gaps++;
  check('sequence numbers are consecutive', gaps === 0, gaps + ' gaps');
  check('one session id for the whole stream', new Set(pk.map(b => hdr(b).getUint32(4, true))).size === 1);
  if (n > 8) {
    const pairs = new Uint8Array(2048); pairs.set(pk[n >> 1].subarray(28, 28 + 2048));
    const [bin, ratio] = fftPeak(pairs), hzOff = bin * r.body.sps / 1024;
    console.log('spectral peak at %d Hz from the window centre (the channel sits at +%d Hz), peak/median %s', hzOff.toFixed(0), r.body.offset_hz, ratio.toFixed(1));
  }
  const second = await openWs();
  check('the ticket is single use: a second WebSocket is refused', !second.opened); if (second.opened) second.ws.close();
  const st = await post('/api/v1/channel/stop');
  check('channel/stop accepted', st.status === 200 && st.body.ok, JSON.stringify(st));
  for (let i = 0; i < 40 && !ws.closed; i++) await sleep(250);   // the close arrives a moment after the reply
  const after = ws.packets.length; await sleep(1000); console.log('readyState', ws.ws.readyState, 'packets after stop', ws.packets.length - after);
  check('the WebSocket is closed after channel/stop', ws.closed);
  const lo = await post('/api/v1/auth/logout'); check('logout', lo.status === 200, JSON.stringify(lo));
  console.log(fails + ' failed'); process.exit(fails);
})().catch(e => { console.log('error', e); process.exit(1); });
