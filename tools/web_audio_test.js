// Tests the node's browser audio (main/web_audio.cpp): node tools/web_audio_test.js <device-ip> [station-hz] [seconds] [sps] [mode]
// Logs in with the factory password, starts the page's "FM service", opens the port-81 audio WebSocket like the page does and checks the packets,
// the status the page reads, the page-only volume (the board's own volume must not move) and the stop.
const L = require('../apps/orcsdr-p4-hosyond-console/web/orclogin.js');
const ip = process.argv[2], hz = Number(process.argv[3] || 96100000), secs = Number(process.argv[4] || 6), sps = Number(process.argv[5] || 2400000), mode = process.argv[6] || 'wfm';
let fails = 0;
const check = (name, ok, detail) => { console.log((ok ? 'PASS ' : 'FAIL ') + name + (ok ? '' : ' - ' + detail)); if (!ok) fails++; };
const t = s => L.enc.encode(s), base = 'http://' + ip;
const sleep = ms => new Promise(r => setTimeout(r, ms));
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

  await post('/api/v1/fm?action=stop');   // start from nothing, whatever an earlier run left
  let x = await post('/api/v1/fm?action=volume&volume=20'); check('volume without a session is refused honestly', x.status === 501 && x.body.error === 'no_audio_session', JSON.stringify(x));
  const before = await openWs(); check('the audio WebSocket is refused before a signed start', !before.opened, 'it opened'); if (before.opened) before.ws.close();
  const boardVolume0 = (await get('/api/v1/volume')).records[0].volume;

  const t0 = Date.now();
  x = await post('/api/v1/fm?action=start&index=0&hz=' + hz + '&sps=' + sps + '&volume=30&tau=75&mode=' + mode);
  console.log('start reply (' + (Date.now() - t0) + ' ms)', JSON.stringify(x.body));
  check('start accepted, announcing the rate really used', x.status === 200 && x.body.ok && x.body.hz === hz && x.body.audio_sps === 48000 && x.body.input_sps >= 960000 && x.body.input_sps <= 2400000 && x.body.input_sps % 240000 === 0, JSON.stringify(x));
  if (!(x.status === 200 && x.body.ok)) { console.log(fails + ' failed'); process.exit(1); }
  let fm = await get('/api/v1/fm');
  check('the page-facing status says it is playing, on this receiver, with browser audio available', fm.playing === true && fm.receiver === 0 && fm.frequency_hz === hz && fm.input_sps === x.body.input_sps && fm.browser_audio.available === true, JSON.stringify(fm));
  const rx = (await get('/api/v1/receivers')).receivers[0];
  check('the receiver really runs at the announced rate and station', rx.sample_rate === x.body.input_sps && rx.center_hz === hz, JSON.stringify([rx.sample_rate, rx.center_hz]));

  const ws = await openWs();
  check('the audio WebSocket opens after the signed start', ws.opened);
  await sleep(secs * 1000);
  const pk = ws.packets, n = pk.length, hdr = b => new DataView(b.buffer, b.byteOffset, b.byteLength);
  console.log('packets received:', n, 'in', secs, 's =', (n / secs).toFixed(1), '/s');
  const ok = pk.filter(b => b.length === 24 + 960 * 2 && hdr(b).getUint32(0, true) === 0x3141464f && hdr(b).getUint32(16, true) === 48000 && hdr(b).getUint16(20, true) === 960 && hdr(b).getUint16(22, true) === 1);
  check('every packet is a well-formed OFA1 frame: 960 samples at 48 kHz', n > 0 && ok.length === n, ok.length + ' of ' + n);
  const expected = 50 * secs;
  check('the packet rate is real time, 50 per second (within 5 %)', Math.abs(n - expected) < 0.05 * expected, n + ' vs ' + expected);
  let gaps = 0, jumps = 0;
  for (let i = 1; i < n; i++) { if (((hdr(pk[i]).getUint32(8, true) - hdr(pk[i - 1]).getUint32(8, true)) >>> 0) !== 1) gaps++; if (((hdr(pk[i]).getUint32(12, true) - hdr(pk[i - 1]).getUint32(12, true)) >>> 0) !== 960) jumps++; }
  check('sequence numbers are consecutive and the sample counter advances by one frame each time', gaps === 0 && jumps === 0, gaps + ' gaps, ' + jumps + ' jumps');
  check('one session id for the whole stream', new Set(pk.map(b => hdr(b).getUint32(4, true))).size === 1);
  let sum = 0, peak = 0, cnt = 0, clipped = 0;
  for (const b of pk) { const v = hdr(b); for (let i = 0; i < 960; i++) { const s = v.getInt16(24 + 2 * i, true); sum += s * s; cnt++; if (Math.abs(s) > peak) peak = Math.abs(s); if (Math.abs(s) >= 32000) clipped++; } }
  const rms = Math.sqrt(sum / Math.max(cnt, 1));
  console.log('audio: rms %s, peak %d of 32767, %d clipped samples', rms.toFixed(0), peak, clipped);
  check('the audio is not silent', rms > 30, 'rms ' + rms.toFixed(1));
  check('the audio is not hard-clipped', clipped < cnt / 100, clipped + ' of ' + cnt);
  fm = await get('/api/v1/fm'); check('a browser is counted as connected', fm.browser_audio.clients === 1, JSON.stringify(fm.browser_audio));
  console.log('dsp %d us per %d us block, rf %s dBFS', fm.dsp_us, fm.block_us, fm.rf_dbfs);
  check('the demodulator keeps up with the stream', fm.block_us > 0 && fm.dsp_us < fm.block_us, fm.dsp_us + ' vs ' + fm.block_us);

  x = await post('/api/v1/fm?action=volume&volume=44'); check('the page volume is accepted', x.status === 200 && x.body.ok, JSON.stringify(x));
  fm = await get('/api/v1/fm'); check('the status shows the page volume', fm.volume === 44, JSON.stringify(fm.volume));
  x = await post('/api/v1/fm?action=volume&volume=999'); check('an out-of-range page volume is refused', x.status === 400, JSON.stringify(x));
  const boardVolume1 = (await get('/api/v1/volume')).records[0].volume;
  check("the board's own volume did not move", boardVolume1 === boardVolume0, boardVolume0 + ' -> ' + boardVolume1);

  const before2 = pk.length; await sleep(500);
  x = await post('/api/v1/fm?action=stop'); check('stop accepted', x.status === 200 && x.body.ok, JSON.stringify(x));
  await sleep(800);
  check('the WebSocket is closed after stop', ws.closed, 'still open');
  const last = ws.packets[ws.packets.length - 1];
  check('the last packet tells the page the audio stopped', last && last.length === 24 && hdr(last).getUint16(22, true) === 0, last ? last.length : 'none');
  fm = await get('/api/v1/fm'); check('the status says it is not playing', fm.playing === false && fm.receiver === -1, JSON.stringify(fm));
  const again = await openWs(); check('the audio WebSocket is refused again after stop', !again.opened, 'it opened'); if (again.opened) again.ws.close();
  x = await post('/api/v1/auth/logout'); check('logout', x.status === 200, JSON.stringify(x));
  console.log(fails + ' failed'); process.exit(fails ? 1 : 0);
})();
