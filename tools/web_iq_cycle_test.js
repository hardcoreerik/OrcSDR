// Starts and stops the browser channel many times and watches the node's memory: node tools/web_iq_cycle_test.js <device-ip> [cycles] [hz]
const L = require('../apps/orcsdr-p4-core/web/orclogin.js');
const ip = process.argv[2], cycles = Number(process.argv[3] || 100), hz = Number(process.argv[4] || 96100000);
const t = s => L.enc.encode(s), base = 'http://' + ip, sleep = ms => new Promise(r => setTimeout(r, ms));
const get = async p => (await fetch(base + p, { headers: { Host: ip } })).json();
const mem = async () => (await get('/api/v1/mem')).records[0];
(async () => {
  const c = await get('/api/v1/auth/challenge'), key = L.pbkdf2(t('password'), L.unhex(c.salt), c.iterations), nonce = L.unhex(c.nonce);
  const lr = await (await fetch(base + '/api/v1/auth/login', { method: 'POST', body: 'nonce=' + c.nonce + '&proof=' + L.hex(L.loginProof(key, nonce)) })).json();
  if (!lr.ok) { console.log('login failed', lr); process.exit(1); }
  const sk = L.sessionKey(key, nonce); let seq = 0;
  const post = async target => { const s = ++seq; const x = await fetch(base + target, { method: 'POST', headers: { 'X-Orc-Session': lr.session, 'X-Orc-Seq': String(s), 'X-Orc-Sig': L.sign(sk, s, 'POST', target, new Uint8Array(0)) } }); return { status: x.status, body: await x.json() }; };
  const m0 = await mem(); let okc = 0, frames = 0, bad = 0; const opened = [];
  for (let i = 0; i < cycles; i++) {
    const r = await post('/api/v1/channel?index=0&hz=' + hz);
    if (r.status !== 200) { bad++; console.log('cycle', i, 'channel', JSON.stringify(r)); await sleep(500); continue; }
    const n = await new Promise(res => { let k = 0; const ws = new WebSocket('ws://' + ip + ':81/api/v1/fm/iq'); ws.binaryType = 'arraybuffer'; ws.onmessage = () => k++; ws.onerror = () => { }; ws.onclose = () => res(k); const t0 = Date.now(); ws.onopen = () => { opened.push(Date.now() - t0); setTimeout(() => { post('/api/v1/channel/stop').then(() => setTimeout(() => ws.close(), 400)); }, 1200); }; setTimeout(() => ws.close(), 5000); });   // the 0.4 s window starts when the socket is open
    frames += n; if (n > 10) okc++; else { bad++; console.log('cycle', i, 'only', n, 'frames'); }
    if (i % 25 === 24) { const m = await mem(); console.log('after', i + 1, 'cycles: internal_free', m.internal_free, 'largest', m.internal_largest, 'psram_free', m.psram_free); }
  }
  await post('/api/v1/auth/logout');
  await sleep(1500);
  const m1 = await mem();
  console.log('cycles ok', okc, 'bad', bad, 'frames', frames, '| WebSocket open time ms: median', opened.sort((x, y) => x - y)[opened.length >> 1], 'max', opened[opened.length - 1]);
  console.log('internal_free before', m0.internal_free, 'after', m1.internal_free, '| psram_free before', m0.psram_free, 'after', m1.psram_free);
  process.exit(bad ? 1 : 0);
})().catch(e => { console.log('error', e); process.exit(1); });
