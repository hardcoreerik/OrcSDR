// Tests the page's mode chip end to end on a board: node tools/web_mode_chip_test.js <device-ip> [station-hz]
// With a browser audio session running, pins each of Home's modes through the signed Home route and checks that the audio socket stays open
// (one session id, consecutive sequence numbers, no restart), that Home reports the pin, and that the board's own volume does not move.
// It puts Home's pin back the way it found it.
const L = require('../apps/orcsdr-p4-hosyond-console/web/orclogin.js');
const ip = process.argv[2], hz = Number(process.argv[3] || 96100000);
let fails = 0;
const check = (name, ok, detail) => { console.log((ok ? 'PASS ' : 'FAIL ') + name + (ok ? '' : ' - ' + detail)); if (!ok) fails++; };
const t = s => L.enc.encode(s), base = 'http://' + ip;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const NAMES = ['AUTO', 'NFM', 'AM', 'WFM', 'USB', 'LSB'];
function openWs() {
  return new Promise(res => {
    const ws = new WebSocket('ws://' + ip + ':81/api/v1/fm/audio'); ws.binaryType = 'arraybuffer';
    const st = { packets: [], opened: false, closed: false, ws };
    ws.onopen = () => { st.opened = true; res(st); };
    ws.onmessage = e => st.packets.push(new Uint8Array(e.data));
    ws.onclose = () => { st.closed = true; res(st); };
    ws.onerror = () => { };
  });
}
(async () => {
  const get = async p => (await fetch(base + p, { headers: { Host: ip } })).json();
  const c = await get('/api/v1/auth/challenge'), key = L.pbkdf2(t('password'), L.unhex(c.salt), c.iterations), nonce = L.unhex(c.nonce);
  const lr = await (await fetch(base + '/api/v1/auth/login', { method: 'POST', body: 'nonce=' + c.nonce + '&proof=' + L.hex(L.loginProof(key, nonce)) })).json();
  check('login', lr.ok, JSON.stringify(lr));
  const sk = L.sessionKey(key, nonce); let seq = 0;
  const post = async target => { const s = ++seq; const x = await fetch(base + target, { method: 'POST', headers: { 'X-Orc-Session': lr.session, 'X-Orc-Seq': String(s), 'X-Orc-Sig': L.sign(sk, s, 'POST', target, new Uint8Array(0)) } }); return { status: x.status, body: await x.json() }; };
  const hdr = b => new DataView(b.buffer, b.byteOffset, b.byteLength);
  const rms = pk => { let s = 0, n = 0; for (const b of pk) { if (b.length !== 24 + 960 * 2) continue; const v = hdr(b); for (let i = 0; i < 960; i++) { const q = v.getInt16(24 + 2 * i, true); s += q * q; n++; } } return n ? Math.sqrt(s / n) : 0; };

  await post('/api/v1/fm?action=stop');
  const boardVolume0 = (await get('/api/v1/volume')).records[0].volume;
  const h0 = await get('/api/v1/home'), pin0 = h0.mode_choice;
  console.log('Home before: band', h0.band, 'mode', h0.mode, 'pin', pin0, 'selectable', h0.mode_selectable);
  check('Home allows a mode pick on this band', h0.mode_selectable === true, JSON.stringify(h0));

  let x = await post('/api/v1/fm?action=start&index=0&hz=' + hz + '&sps=2400000&volume=30&tau=75&mode=wfm');
  check('browser audio started', x.status === 200 && x.body.ok, JSON.stringify(x));
  if (!(x.status === 200 && x.body.ok)) { console.log(fails + ' failed'); process.exit(1); }
  const ws = await openWs(); check('the audio WebSocket opens', ws.opened);
  await sleep(1500);
  const sid = hdr(ws.packets[0]).getUint32(4, true);
  let mark = ws.packets.length;
  console.log('baseline rms', rms(ws.packets).toFixed(0));

  for (const m of [2, 1, 4, 5, 3, 0]) {
    x = await post('/api/v1/home?action=mode_set&value=' + m);
    check('pin ' + NAMES[m] + ' accepted', x.status === 200 && x.body.ok, JSON.stringify(x));
    await sleep(1500);
    const h = await get('/api/v1/home');
    check('Home reports the pin ' + NAMES[m], h.mode_choice === m, JSON.stringify([h.mode_choice, h.mode, h.mode_active]));
    if (m) check('the active mode is the pin ' + NAMES[m], h.mode_active === m && h.mode === NAMES[m], JSON.stringify([h.mode_active, h.mode]));
    const part = ws.packets.slice(mark); mark = ws.packets.length;
    console.log('  ' + NAMES[m] + ': ' + part.length + ' packets, rms ' + rms(part).toFixed(0));
    check('audio kept flowing after ' + NAMES[m], part.length >= 50, part.length + ' packets in 1.5 s');
  }

  const pk = ws.packets;
  let gaps = 0, jumps = 0;
  for (let i = 1; i < pk.length; i++) { if (((hdr(pk[i]).getUint32(8, true) - hdr(pk[i - 1]).getUint32(8, true)) >>> 0) !== 1) gaps++; if (((hdr(pk[i]).getUint32(12, true) - hdr(pk[i - 1]).getUint32(12, true)) >>> 0) !== 960) jumps++; }
  check('one session id across every mode switch', new Set(pk.map(b => hdr(b).getUint32(4, true))).size === 1 && hdr(pk[0]).getUint32(4, true) === sid);
  check('sequence numbers stayed consecutive across every mode switch', gaps === 0 && jumps === 0, gaps + ' gaps, ' + jumps + ' jumps');
  check('the WebSocket never closed', !ws.closed);
  const fm = await get('/api/v1/fm');
  check('the session is still playing, same station', fm.playing === true && fm.frequency_hz === hz, JSON.stringify(fm));
  const boardVolume1 = (await get('/api/v1/volume')).records[0].volume;
  check("the board's own volume did not move", boardVolume1 === boardVolume0, boardVolume0 + ' -> ' + boardVolume1);

  x = await post('/api/v1/home?action=mode_set&value=9'); check('an out-of-range pick is refused', x.status === 400 && x.body.error === 'bad_value', JSON.stringify(x));
  await post('/api/v1/home?action=mode_set&value=' + pin0); await sleep(500);
  x = await post('/api/v1/fm?action=stop'); check('stop accepted', x.status === 200, JSON.stringify(x));
  await post('/api/v1/auth/logout');
  console.log(fails + ' failed'); process.exit(fails ? 1 : 0);
})();
