// Tests basemap delivery (main/web_maps.cpp): node tools/web_maps_test.js <device-ip> [password]
// Always checks the signing and range rules. If the board has /sd/orcmaps/basemap.pmtiles it also reads the whole file in chunks and
// checks the PMTiles magic and that the length adds up; if not (no SD card, or no file) it checks the absent-path answers and says so.
const L = require('../apps/orcsdr-p4-core/web/orclogin.js');
const ip = process.argv[2], pw = process.argv[3] || 'password', base = 'http://' + ip;
let fails = 0;
const check = (name, ok, detail) => { console.log((ok ? 'PASS ' : 'FAIL ') + name + (ok ? '' : ' - ' + detail)); if (!ok) fails++; };
const t = s => L.enc.encode(s);
(async () => {
  const get = async p => (await fetch(base + p, { headers: { Host: ip } })).json();
  const c = await get('/api/v1/auth/challenge'), key = L.pbkdf2(t(pw), L.unhex(c.salt), c.iterations), nonce = L.unhex(c.nonce);
  const lr = await (await fetch(base + '/api/v1/auth/login', { method: 'POST', body: 'nonce=' + c.nonce + '&proof=' + L.hex(L.loginProof(key, nonce)) })).json();
  check('login', lr.ok, JSON.stringify(lr));
  const sk = L.sessionKey(key, nonce); let seq = 0;
  const post = async (target, signedTarget) => {
    const s = ++seq;
    const x = await fetch(base + target, { method: 'POST', headers: { 'X-Orc-Session': lr.session, 'X-Orc-Seq': String(s), 'X-Orc-Sig': L.sign(sk, s, 'POST', signedTarget || target, new Uint8Array(0)) } });
    const ct = x.headers.get('content-type') || '';
    return { status: x.status, total: x.headers.get('x-orc-total'), json: ct.includes('json') ? await x.json() : null, bytes: ct.includes('json') ? null : new Uint8Array(await x.arrayBuffer()) };
  };
  let r = await fetch(base + '/api/v1/map/status', { method: 'POST' });
  check('an unsigned map/status is refused', r.status === 401, r.status);
  r = await fetch(base + '/api/v1/map/read?offset=0&len=16', { method: 'GET' });
  check('map/read over GET is not a route', r.status === 404, r.status);

  const st = await post('/api/v1/map/status');
  check('map/status answers with sd, present and bytes', st.status === 200 && st.json.ok && typeof st.json.sd === 'boolean' && typeof st.json.present === 'boolean' && typeof st.json.bytes === 'number', JSON.stringify(st));
  console.log('status:', JSON.stringify(st.json));

  let x = await post('/api/v1/map/read?offset=0&len=999999');
  check('a chunk over the limit is refused', x.status === 400 || x.status === 404, JSON.stringify(x.json));
  x = await post('/api/v1/map/read?offset=abc&len=16');
  check('a bad offset is refused', x.status === 400 || x.status === 404, JSON.stringify(x.json));
  x = await post('/api/v1/map/read?offset=0&len=16', '/api/v1/map/read?offset=0&len=17');
  check('changing the range after signing is refused', x.status === 401, JSON.stringify(x.json));

  if (!st.json.present) {
    x = await post('/api/v1/map/read?offset=0&len=16');
    check('without a basemap, map/read says no_basemap (absent path only: no file on this board\'s SD card)', x.status === 404 && x.json.error === 'no_basemap', JSON.stringify(x.json));
    console.log('NOTE: no basemap on the SD card (sd=' + st.json.sd + '); the delivery of real data was NOT exercised.');
  } else {
    const total = st.json.bytes, CH = 32768, got = new Uint8Array(total);
    let ok = true;
    for (let off = 0; off < total; off += CH) {
      const len = Math.min(CH, total - off), y = await post('/api/v1/map/read?offset=' + off + '&len=' + len);
      if (y.status !== 200 || y.bytes.length !== len || y.total !== String(total)) { ok = false; console.log('chunk failed at', off, y.status, y.bytes && y.bytes.length); break; }
      got.set(y.bytes, off);
    }
    check('the whole basemap reads back in chunks', ok, '');
    check('it starts with the PMTiles magic', String.fromCharCode(...got.slice(0, 7)) === 'PMTiles', String.fromCharCode(...got.slice(0, 7)));
    x = await post('/api/v1/map/read?offset=' + total + '&len=16');
    check('reading at the end is refused (416)', x.status === 416, JSON.stringify(x.json));
    x = await post('/api/v1/map/read?offset=' + (total - 8) + '&len=64');
    check('a read past the end is clamped to the file', x.status === 200 && x.bytes.length === 8, JSON.stringify([x.status, x.bytes && x.bytes.length]));
  }
  const lo = await post('/api/v1/auth/logout'); check('logout', lo.status === 200, JSON.stringify(lo.json));
  console.log(fails + ' failed'); process.exit(fails);
})().catch(e => { console.log('FAIL ' + e.message); process.exit(1); });
