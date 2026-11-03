// Tests the browser login script (apps/orcsdr-p4-hosyond-console/web/orclogin.js) under Node.
//   node tools/orclogin_test.js               known-answer tests for SHA-256, HMAC and PBKDF2 only
//   node tools/orclogin_test.js <device-ip>   also logs in to a node and sends signed control requests (retunes to the frequency it read, sets volume back)
const L = require('../apps/orcsdr-p4-hosyond-console/web/orclogin.js');
let fails = 0;
const check = (name, ok, detail) => { console.log((ok ? 'PASS ' : 'FAIL ') + name + (ok ? '' : ' - ' + detail)); if (!ok) fails++; };
const t = s => L.enc.encode(s);

check('SHA-256 "abc"', L.hex(L.sha256(t('abc'))) === 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
check('SHA-256 of 1,000 bytes crosses block edges', L.hex(L.sha256(new Uint8Array(1000).fill(97))) === '41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3');
check('HMAC RFC 4231 case 2', L.hex(L.hmac(t('Jefe'), t('what do ya want for nothing?'))) === '5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843');
check('HMAC with a key longer than a block', L.hex(L.hmac(new Uint8Array(131).fill(0xaa), t('Test Using Larger Than Block-Size Key - Hash Key First'))) === '60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54');
check('PBKDF2 RFC 7914 vector, 1 iteration', L.hex(L.pbkdf2(t('passwd'), t('salt'), 1)) === '55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc');
const t0 = Date.now(); L.pbkdf2(t('password'), new Uint8Array(16), 10000); console.log('PBKDF2 10,000 iterations: ' + (Date.now() - t0) + ' ms');

async function device(ip) {
  const base = 'http://' + ip, get = async p => (await fetch(base + p, { headers: { Host: ip } })).json();
  const c = await get('/api/v1/auth/challenge'), key = L.pbkdf2(t('password'), L.unhex(c.salt), c.iterations), nonce = L.unhex(c.nonce);
  const r = await fetch(base + '/api/v1/auth/login', { method: 'POST', body: 'nonce=' + c.nonce + '&proof=' + L.hex(L.loginProof(key, nonce)) });
  const j = await r.json();
  check('login from the JavaScript implementation', r.ok && j.ok, JSON.stringify(j));
  check('device proof verifies in JavaScript', j.device === L.hex(L.deviceProof(key, nonce)));
  const sk = L.sessionKey(key, nonce); let seq = 0;
  const post = async target => { const s = ++seq, h = { 'X-Orc-Session': j.session, 'X-Orc-Seq': String(s), 'X-Orc-Sig': L.sign(sk, s, 'POST', target, new Uint8Array(0)) }; const x = await fetch(base + target, { method: 'POST', headers: h }); return { status: x.status, body: await x.json() }; };
  const freq = (await get('/api/v1/freq')).records[0].frequency_hz, vol = (await get('/api/v1/volume')).records[0].volume;
  let x = await post('/api/v1/receivers/tune?index=0&hz=' + freq); check('receivers/tune accepted', x.status === 200 && x.body.ok, JSON.stringify(x));
  x = await post('/api/v1/receivers/tune?index=0&hz=5'); check('receivers/tune out of range refused', x.status === 400, JSON.stringify(x));
  let rate0; for (let i = 0; i < 40 && !rate0; i++) { rate0 = (await get('/api/v1/receivers')).receivers[0].sample_rate; if (!rate0) await new Promise(r => setTimeout(r, 500)); }   // a receiver that is restarting has no rate yet
  x = await post('/api/v1/receivers/rate?index=0&sps=1000000'); check('receivers/rate applies what the driver quantises to', x.status === 200 && x.body.ok && x.body.applied_sps >= 900001 && x.body.applied_sps <= 1000000, JSON.stringify(x));
  x = await post('/api/v1/receivers/rate?index=0&sps=100000'); check('a rate the driver cannot run is refused', x.status === 400 && x.body.error === 'sps_not_supported_by_the_driver', JSON.stringify(x));
  await new Promise(r => setTimeout(r, 6000));
  const rate1 = (await get('/api/v1/receivers')).receivers[0].sample_rate;
  check('the receiver really moved to the quantised rate (the Home screen does not take it back)', rate1 === 1000000 || (rate1 >= 900001 && rate1 <= 1000000), 'receiver reads ' + rate1);
  x = await post('/api/v1/receivers/rate?index=0&sps=0'); check('sps=0 gives the rate back to the console', x.status === 200 && x.body.ok && x.body.applied_sps === 0, JSON.stringify(x));
  await new Promise(r => setTimeout(r, 6000));
  const rate2 = (await get('/api/v1/receivers')).receivers[0].sample_rate;
  check('the console\'s own rate comes back (' + rate0 + ')', rate2 === rate0, 'receiver reads ' + rate2);
  x = await post('/api/v1/fm?action=volume&volume=' + vol); check('fm volume is the page\'s own and needs an audio session (tools/web_audio_test.js covers it)', x.status === 501 && x.body.error === 'no_audio_session', JSON.stringify(x));
  x = await post('/api/v1/tuner/gain?index=0&mode=auto'); check('tuner/gain auto accepted', x.status === 200 || x.status === 501, JSON.stringify(x));
  x = await post('/api/v1/channel?index=0&center=96000000&rx_sps=2400000&hz=96000000'); check('a wide channel is accepted or refused honestly', (x.status === 200 && x.body.ok) || (x.status === 501 && x.body.ok === false), JSON.stringify(x));
  if (x.status === 200) await post('/api/v1/channel/stop');
  x = await post('/api/v1/time?epoch=' + Math.floor(Date.now() / 1000)); check('time accepted', x.status === 200 && x.body.ok, JSON.stringify(x));
  x = await post('/api/v1/receivers/nothing'); check('unknown route is not a control route', x.status === 404, JSON.stringify(x));
  x = await post('/api/v1/auth/logout'); check('logout', x.status === 200, JSON.stringify(x));
}
(process.argv[2] ? device(process.argv[2]) : Promise.resolve()).then(() => { console.log(fails + ' failed'); process.exit(fails); }, e => { console.log('error', e); process.exit(1); });
