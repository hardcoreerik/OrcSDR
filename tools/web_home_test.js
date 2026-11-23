// Tests the signed `home` route and the `home` status (main/home_app.cpp request_web_action, main/web_compat.cpp): node tools/web_home_test.js <device-ip>
// The route hands the same Home action a touch would reach to the UI task. Only VFO actions are allowed; volume, mute and screen changes must be refused,
// and the board's own volume must not move. The Home state it changes (mode pin, filter, step size, span) is put back at the end.
const L = require('../apps/orcsdr-p4-core/web/orclogin.js');
const ip = process.argv[2], base = 'http://' + ip;
let fails = 0;
const check = (name, ok, detail) => { console.log((ok ? 'PASS ' : 'FAIL ') + name + (ok ? '' : ' - ' + detail)); if (!ok) fails++; };
const t = s => L.enc.encode(s), sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const get = async p => (await fetch(base + p, { headers: { Host: ip } })).json();
  const c = await get('/api/v1/auth/challenge'), key = L.pbkdf2(t('password'), L.unhex(c.salt), c.iterations), nonce = L.unhex(c.nonce);
  const lr = await (await fetch(base + '/api/v1/auth/login', { method: 'POST', body: 'nonce=' + c.nonce + '&proof=' + L.hex(L.loginProof(key, nonce)) })).json();
  check('login', lr.ok, JSON.stringify(lr));
  const sk = L.sessionKey(key, nonce); let seq = 0;
  const post = async target => { const s = ++seq; const x = await fetch(base + target, { method: 'POST', headers: { 'X-Orc-Session': lr.session, 'X-Orc-Seq': String(s), 'X-Orc-Sig': L.sign(sk, s, 'POST', target, new Uint8Array(0)) } }); return { status: x.status, body: await x.json() }; };
  const home = async () => get('/api/v1/home');
  const act = async (a, v) => { const r = await post('/api/v1/home?action=' + a + (v === undefined ? '' : '&value=' + v)); await sleep(500); return r; };

  const h0 = await home(), vol0 = (await get('/api/v1/volume')).records[0].volume, f0 = (await get('/api/v1/freq')).records[0].frequency_hz;
  console.log('home status before', JSON.stringify(h0));
  check('home status has the VFO fields', ['frequency_hz', 'band', 'mode', 'step_hz', 'step_adjustable', 'span_hz', 'filter_bandwidth_hz', 'mode_choice', 'mode_active', 'mode_suggested', 'mode_selectable'].every(k => k in h0), JSON.stringify(h0));

  let x = await post('/api/v1/home?action=step_size_up'); check('a Home action needs no extra login step but is signed (accepted)', x.status === 200 && x.body.ok, JSON.stringify(x));
  await sleep(500); const h1 = await home();
  if (h0.step_adjustable) check('step_size_up changes the step the Home screen uses', h1.step_hz !== h0.step_hz, h0.step_hz + ' -> ' + h1.step_hz);
  else check('the step is fixed on this band, as Home says', h1.step_hz === h0.step_hz, h0.step_hz + ' -> ' + h1.step_hz);
  for (let i = 0; i < 8 && h0.step_adjustable; i++) { await post('/api/v1/home?action=step_size_down'); await sleep(200); if ((await home()).step_hz === h0.step_hz) break; }

  const pin0 = h0.mode_choice;
  x = await act('mode_set', 2); const h2 = await home(); check('mode_set pins AM (2)', x.status === 200 && h2.mode_choice === 2, JSON.stringify([x.status, h2.mode_choice, h2.mode]));
  check('the demodulator in use follows the pin', h2.mode_active === 2 || !h2.mode_selectable, JSON.stringify([h2.mode_active, h2.mode_selectable]));
  x = await act('mode_set', pin0); const h3 = await home(); check('mode_set puts the pin back', h3.mode_choice === pin0, JSON.stringify([pin0, h3.mode_choice]));
  x = await post('/api/v1/home?action=mode_set&value=9'); check('an out-of-range mode is refused', x.status === 400 && x.body.error === 'bad_value', JSON.stringify(x));

  x = await act('filter_set', 50000); const h4 = await home(); check('filter_set overrides the bandwidth', x.status === 200 && h4.filter_bandwidth_hz === 50000, JSON.stringify([x.status, h4.filter_bandwidth_hz]));
  x = await act('filter_set', 0); const h5 = await home(); check('filter_set 0 returns to the band standard', h5.filter_bandwidth_hz === h0.filter_bandwidth_hz, JSON.stringify([h5.filter_bandwidth_hz, h0.filter_bandwidth_hz]));

  x = await act('span_down'); const h6 = await home(); check('span_down is applied', x.status === 200 && h6.span_hz <= h0.span_hz, JSON.stringify([h0.span_hz, h6.span_hz]));
  x = await act('span_up'); const h7 = await home(); check('span_up is applied', x.status === 200 && h7.span_hz >= h6.span_hz, JSON.stringify([h6.span_hz, h7.span_hz]));
  for (let i = 0; i < 6 && h7.span_hz !== h0.span_hz; i++) { const cur = (await home()).span_hz; if (cur === h0.span_hz) break; await act(cur > h0.span_hz ? 'span_down' : 'span_up'); }

  for (const a of ['volume_up', 'volume_down', 'sound_toggle', 'open_dashboard', 'open_device_settings', 'open_clock']) {
    x = await post('/api/v1/home?action=' + a); check(a + ' is refused from the web', x.status === 501 && x.body.error === 'not_allowed_from_the_web', JSON.stringify(x));
  }
  x = await post('/api/v1/home?action=reboot'); check('an unknown action is refused', x.status === 400 && x.body.error === 'unknown_action', JSON.stringify(x));
  x = await post('/api/v1/home'); check('a missing action is refused', x.status === 400, JSON.stringify(x));
  const r = await fetch(base + '/api/v1/home?action=step_up', { method: 'POST' }); check('an unsigned Home action is refused', r.status === 401, String(r.status));

  const vol1 = (await get('/api/v1/volume')).records[0].volume, f1 = (await get('/api/v1/freq')).records[0].frequency_hz, h8 = await home();
  check("the board's own volume did not move", vol1 === vol0, vol0 + ' -> ' + vol1);
  check('the tuned frequency did not move', f1 === f0, f0 + ' -> ' + f1);
  check('mode, filter and step are back where they were', h8.mode_choice === h0.mode_choice && h8.filter_bandwidth_hz === h0.filter_bandwidth_hz && h8.step_hz === h0.step_hz, JSON.stringify(h8));
  x = await post('/api/v1/auth/logout'); check('logout', x.status === 200, JSON.stringify(x));
  console.log(fails + ' failed'); process.exit(fails ? 1 : 0);
})();
