#pragma once

/* Tab5 ADS-B dashboard look-alike served by OrcSDR Waveshare HTTP backend. */
static const char kWebUiIndexHtml[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>OrcSDR ADS-B</title>
<style>
:root{
  --bg:#000000;
  --panel:#0b1420;
  --border:#2a3545;
  --blue:#00a0ff;
  --green:#6fe820;
  --muted:#9ca3af;
  --text:#f3f4f6;
  --navy:#0b1f3a;
  --maroon:#5a1020;
  --card-radius:12px;
}
*{box-sizing:border-box}
html,body{margin:0;padding:0;background:var(--bg);color:var(--text);
  font-family:Inter,Segoe UI,Roboto,Helvetica,Arial,sans-serif;min-height:100%}
body{display:flex;flex-direction:column;min-height:100vh}
button{font:inherit;cursor:pointer;border:1px solid #9ca3af;border-radius:8px;
  background:#1f2937;color:#fff;padding:8px 14px}
button.active,button.primary{background:#14532d;border-color:#6fe820}
button.danger{background:var(--maroon)}
button.nav{background:transparent;border:none;color:#d1d5db;min-width:100px;padding:18px 12px}
button.nav.active{background:#0b1f3a;color:var(--blue)}
.header{display:flex;align-items:center;gap:18px;padding:14px 20px;
  border-bottom:1px solid var(--border);flex-wrap:wrap}
.header h1{margin:0;font-size:28px;font-weight:700;letter-spacing:.02em}
.badge-live{display:inline-flex;align-items:center;gap:8px}
.dot{width:12px;height:12px;border-radius:50%;background:var(--green);box-shadow:0 0 8px var(--green)}
.dot.off{background:#6b7280;box-shadow:none}
.muted{color:var(--muted)}
.blue{color:var(--blue)}
.green{color:var(--green)}
.main{flex:1;padding:16px 18px 88px;display:grid;gap:16px}
.tabs{position:fixed;left:0;right:0;bottom:0;display:grid;grid-template-columns:repeat(5,1fr);
  background:#000;border-top:1px solid var(--border);z-index:20}
.card{background:var(--panel);border:1px solid var(--border);border-radius:var(--card-radius);padding:16px}
.layout-radar{display:grid;grid-template-columns:minmax(280px,1.2fr) minmax(260px,.9fr);gap:16px}
.layout-list{display:grid;grid-template-columns:minmax(320px,1.4fr) minmax(240px,.8fr);gap:16px}
.layout-stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px}
.layout-target,.layout-settings{display:grid;gap:16px}
@media (max-width:900px){
  .layout-radar,.layout-list{grid-template-columns:1fr}
  .header h1{font-size:22px}
}
#radar{width:100%;height:min(62vh,560px);background:#071018;border-radius:10px;display:block}
.summary h2{margin:0 0 8px;color:var(--green);font-size:28px}
.summary .id{color:var(--muted);margin-bottom:12px}
.kv{display:grid;grid-template-columns:1fr 1fr;gap:10px 16px;margin-top:12px}
.kv label{display:block;color:var(--blue);font-size:12px;letter-spacing:.04em}
.kv span{font-size:20px}
.row{display:flex;align-items:center;gap:12px;padding:12px;border-radius:8px;cursor:pointer}
.row:hover{background:#0f1a28}
.row.selected{background:var(--navy)}
.row .call{color:var(--green);font-weight:700}
.plane-ico{width:18px;height:18px;color:var(--green)}
.metric-big{font-size:34px;margin:8px 0 0}
.bars{display:flex;align-items:flex-end;gap:6px;height:80px;margin-top:14px}
.bars i{display:block;width:18px;background:var(--green);border-radius:2px 2px 0 0}
.form-row{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin:10px 0}
input{background:#0b1f3a;border:1px solid var(--border);color:#fff;border-radius:8px;
  padding:10px 12px;min-width:160px}
.hidden{display:none !important}
.foot-note{color:var(--muted);font-size:13px;margin-top:8px}
</style>
</head>
<body>
<header class="header">
  <h1>OrcSDR</h1>
  <div class="blue" style="font-size:22px;font-weight:600">ADS-B 1090</div>
  <div class="badge-live"><span id="liveDot" class="dot off"></span>
    <button id="liveBtn" class="danger">DEMO</button></div>
  <div><span id="acCount" style="font-size:18px">0 AIRCRAFT</span></div>
  <div class="muted">MSG RATE <span id="msgRate" class="green">0.0/s</span></div>
  <div class="muted">SPS <span id="sps">0</span></div>
  <div class="muted">IP <span id="ip">-</span></div>
  <div class="muted" id="statusLine">booting</div>
  <a href="/fm" style="color:var(--green);margin-left:auto">FM RADIO →</a>
</header>

<main class="main">
  <section id="view-radar" class="layout-radar">
    <div class="card">
      <canvas id="radar"></canvas>
      <div class="foot-note" id="radarHint">Set receiver location in SETTINGS for range/bearing.</div>
    </div>
    <div class="card summary" id="radarSummary"></div>
  </section>

  <section id="view-list" class="layout-list hidden">
    <div class="card" id="listBody"></div>
    <div class="card summary" id="listSummary"></div>
  </section>

  <section id="view-target" class="layout-target hidden">
    <div class="card summary" id="targetBody"></div>
  </section>

  <section id="view-stats" class="layout-stats hidden">
    <div class="card"><div class="blue">SIGNAL STRENGTH</div>
      <div class="metric-big" id="statSig">-90.0 dBFS</div>
      <div class="bars" id="sigBars"></div></div>
    <div class="card"><div class="blue">MESSAGE RATE</div>
      <div class="metric-big green" id="statRate">0.0 msg/sec</div></div>
    <div class="card"><div class="blue">MODE-S ACTIVITY</div>
      <div class="metric-big" id="statMsg">0</div>
      <div class="muted">total CRC-OK frames</div></div>
    <div class="card"><div class="blue">AIRCRAFT</div>
      <div class="metric-big" id="statAc">0</div></div>
    <div class="card"><div class="blue">STREAM</div>
      <div class="metric-big" id="statSps">0</div>
      <div class="muted">effective SPS · drops <span id="statDrops">0</span></div></div>
  </section>

  <section id="view-settings" class="layout-settings hidden">
    <div class="card">
      <h2 class="blue" style="margin-top:0">ADS-B SETTINGS</h2>
      <div class="form-row"><label class="muted">Receiver latitude</label>
        <input id="lat" type="number" step="0.0000001" placeholder="e.g. 37.7749"/></div>
      <div class="form-row"><label class="muted">Receiver longitude</label>
        <input id="lon" type="number" step="0.0000001" placeholder="e.g. -122.4194"/></div>
      <div class="form-row"><label class="muted">Radar range (NM)</label>
        <input id="rangeNm" type="number" min="5" max="200" value="25"/>
        <button class="primary" id="saveLoc">SAVE LOCATION</button></div>
      <p class="foot-note">Location is stored in browser localStorage and sent to the device for radar geometry.
      RF gain remains automatic (read-only), matching Tab5 ADS-B.</p>
      <div class="form-row">
        <button id="modeAdsb" class="primary">MODE ADSB</button>
        <button id="modeFm">MODE FM</button>
        <button id="modeWx">MODE WX</button>
      </div>
    </div>
    <div class="card">
      <div id="settingsSide" class="green" style="font-size:28px">LIVE RECEIVER</div>
      <p class="muted" id="settingsDetail">1090 MHz Mode-S capture</p>
      <p class="muted">USB host: lower-left Type-A next to Ethernet on Waveshare P4.</p>
    </div>
  </section>
</main>

<nav class="tabs">
  <button class="nav active" data-view="radar">RADAR</button>
  <button class="nav" data-view="list">LIST</button>
  <button class="nav" data-view="target">TARGET</button>
  <button class="nav" data-view="stats">STATS</button>
  <button class="nav" data-view="settings">SETTINGS</button>
</nav>

<script>
const state = {
  view: 'radar',
  selected: 0,
  lockedIcao: 0,
  data: null,
};

const views = ['radar','list','target','stats','settings'];
const deg2rad = d => d * Math.PI / 180;

function $(id){ return document.getElementById(id); }

function loadLoc(){
  try {
    const j = JSON.parse(localStorage.getItem('orcsdr_rx')||'{}');
    if (j.lat!=null) $('lat').value = j.lat;
    if (j.lon!=null) $('lon').value = j.lon;
    if (j.range!=null) $('rangeNm').value = j.range;
  } catch(e){}
}

function haversineNm(lat1, lon1, lat2, lon2){
  const R = 3440.065;
  const dlat = deg2rad(lat2-lat1), dlon = deg2rad(lon2-lon1);
  const a = Math.sin(dlat/2)**2 + Math.cos(deg2rad(lat1))*Math.cos(deg2rad(lat2))*Math.sin(dlon/2)**2;
  return 2*R*Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
}
function bearingDeg(lat1, lon1, lat2, lon2){
  const y = Math.sin(deg2rad(lon2-lon1))*Math.cos(deg2rad(lat2));
  const x = Math.cos(deg2rad(lat1))*Math.sin(deg2rad(lat2)) -
            Math.sin(deg2rad(lat1))*Math.cos(deg2rad(lat2))*Math.cos(deg2rad(lon2-lon1));
  return (Math.atan2(y,x)*180/Math.PI + 360) % 360;
}

function enrich(ac){
  const lat = parseFloat($('lat').value);
  const lon = parseFloat($('lon').value);
  const rangeNm = parseFloat($('rangeNm').value)||25;
  const locOk = Number.isFinite(lat) && Number.isFinite(lon);
  if (ac.has_position && locOk){
    ac.range_nm = haversineNm(lat, lon, ac.latitude, ac.longitude);
    ac.bearing_deg = Math.round(bearingDeg(lat, lon, ac.latitude, ac.longitude));
    ac.geometry = true;
  } else {
    ac.range_nm = null;
    ac.bearing_deg = null;
    ac.geometry = false;
  }
  ac.radar_range_nm = rangeNm;
  return ac;
}

function aircraftList(){
  const d = state.data;
  if (!d || !d.aircraft) return [];
  return d.aircraft.map(enrich);
}

function selectedAircraft(){
  const list = aircraftList();
  if (!list.length) return null;
  if (state.lockedIcao){
    const hit = list.find(a => a.icao === state.lockedIcao);
    if (hit) return hit;
  }
  state.selected = Math.min(state.selected, list.length-1);
  return list[state.selected];
}

function fmtIcao(v){ return (v>>>0).toString(16).toUpperCase().padStart(6,'0'); }
function callOf(a){
  if (a.has_callsign && a.callsign) return a.callsign;
  if (a.registration) return a.registration;
  return fmtIcao(a.icao);
}

function summaryHtml(a){
  if (!a){
    return `<h2 class="blue">SEARCHING</h2><p class="muted">No aircraft in the last 60 seconds</p>`;
  }
  const locked = state.lockedIcao === a.icao;
  return `
    <h2>${callOf(a)}</h2>
    <div class="id">${a.registration||'--'} | ${fmtIcao(a.icao)}</div>
    <div class="muted">${a.type||'--'}</div>
    <div class="muted">${a.owner||''}</div>
    <div style="margin:12px 0">
      <button id="lockBtn" class="${locked?'primary':''}">${locked?'LOCKED':'LOCK'}</button>
    </div>
    <div class="kv">
      <div><label>ALTITUDE</label><span>${a.has_altitude? a.altitude_ft+' ft':'--'}</span></div>
      <div><label>SPEED</label><span>${a.has_speed? a.speed_kts+' kts':'--'}</span></div>
      <div><label>RANGE / BEARING</label><span>${a.geometry? a.range_nm.toFixed(0)+' NM  '+String(a.bearing_deg).padStart(3,'0')+' deg':'--'}</span></div>
      <div><label>HEADING</label><span>${a.has_heading? String(a.heading_deg).padStart(3,'0')+' deg':'--'}</span></div>
      <div><label>VERT RATE</label><span>${a.has_vertical_rate? ((a.vertical_rate_fpm>=0?'+':'')+a.vertical_rate_fpm+' ft/min'):'--'}</span></div>
      <div><label>SIGNAL</label><span>${a.signal_dbfs!=null? a.signal_dbfs.toFixed(1)+' dBFS':'--'}</span></div>
    </div>`;
}

function drawRadar(){
  const canvas = $('radar');
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio||1;
  canvas.width = Math.floor(rect.width*dpr);
  canvas.height = Math.floor(rect.height*dpr);
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr,0,0,dpr,0,0);
  const w = rect.width, h = rect.height;
  const cx = w/2, cy = h/2, radius = Math.min(w,h)*0.42;
  ctx.clearRect(0,0,w,h);
  ctx.fillStyle = '#071018';
  ctx.fillRect(0,0,w,h);
  ctx.strokeStyle = '#1f4d3a';
  ctx.lineWidth = 1;
  for (let r=1;r<=4;r++){
    ctx.beginPath();
    ctx.arc(cx,cy,radius*r/4,0,Math.PI*2);
    ctx.stroke();
  }
  ctx.beginPath(); ctx.moveTo(cx-radius,cy); ctx.lineTo(cx+radius,cy); ctx.stroke();
  ctx.beginPath(); ctx.moveTo(cx,cy-radius); ctx.lineTo(cx,cy+radius); ctx.stroke();
  ctx.fillStyle = '#fff';
  ctx.font = '14px sans-serif';
  ctx.textAlign='center'; ctx.fillText('N', cx, cy-radius-8);
  ctx.fillText('S', cx, cy+radius+16);
  ctx.fillText('W', cx-radius-14, cy+4);
  ctx.fillText('E', cx+radius+14, cy+4);

  // home
  drawPlane(ctx, cx, cy, 16, '#00a0ff');

  const list = aircraftList();
  const rangeNm = parseFloat($('rangeNm').value)||25;
  list.forEach((a,i)=>{
    if (!a.geometry) return;
    const dist = Math.min(a.range_nm / rangeNm, 1);
    const ang = deg2rad(a.bearing_deg - 90);
    const px = cx + Math.cos(ang) * dist * (radius-12);
    const py = cy + Math.sin(ang) * dist * (radius-12);
    const sel = (selectedAircraft() && selectedAircraft().icao===a.icao);
    drawPlane(ctx, px, py, sel?14:11, sel?'#00a0ff':'#6fe820');
    if (sel){
      ctx.strokeStyle = '#6fe820';
      ctx.strokeRect(px-18, py-18, 36, 36);
    }
    ctx.fillStyle = '#9ca3af';
    ctx.font = '12px sans-serif';
    ctx.fillText(callOf(a), px, py+24);
  });

  const locOk = Number.isFinite(parseFloat($('lat').value)) && Number.isFinite(parseFloat($('lon').value));
  $('radarHint').textContent = locOk
    ? `Radar range ${rangeNm} NM · ${list.filter(a=>a.geometry).length} positioned`
    : 'Set receiver location in SETTINGS for range / bearing.';
}

function drawPlane(ctx,x,y,s,color){
  ctx.save();
  ctx.translate(x,y);
  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.moveTo(0,-s);
  ctx.lineTo(s*0.35,-s*0.2);
  ctx.lineTo(s*0.15,-s*0.2);
  ctx.lineTo(s,s*0.35);
  ctx.lineTo(s*0.2,s*0.15);
  ctx.lineTo(0,s*0.55);
  ctx.lineTo(-s*0.2,s*0.15);
  ctx.lineTo(-s,s*0.35);
  ctx.lineTo(-s*0.15,-s*0.2);
  ctx.lineTo(-s*0.35,-s*0.2);
  ctx.closePath();
  ctx.fill();
  ctx.restore();
}

function renderList(){
  const list = aircraftList();
  let html = `<div class="blue" style="margin-bottom:10px">AIRCRAFT · ALTITUDE · SPEED · RANGE</div>`;
  if (!list.length) html += `<p class="muted">SEARCHING…</p>`;
  list.forEach((a,i)=>{
    const sel = selectedAircraft() && selectedAircraft().icao===a.icao;
    html += `<div class="row ${sel?'selected':''}" data-i="${i}">
      <span class="call">${callOf(a)}</span>
      <span class="muted">${fmtIcao(a.icao)}</span>
      <span>${a.has_altitude? a.altitude_ft+' ft':'--'}</span>
      <span>${a.has_speed? a.speed_kts+' kts':'--'}</span>
      <span>${a.geometry? a.range_nm.toFixed(0)+' NM':'--'}</span>
    </div>`;
  });
  $('listBody').innerHTML = html;
  $('listBody').querySelectorAll('.row').forEach(el=>{
    el.onclick = ()=>{ state.selected = +el.dataset.i; state.lockedIcao = 0; render(); };
  });
}

function renderTarget(){
  const a = selectedAircraft();
  if (!a){
    $('targetBody').innerHTML = `<h2 class="blue">SEARCHING FOR AIRCRAFT</h2>`;
    return;
  }
  $('targetBody').innerHTML = summaryHtml(a) + `
    <div class="kv" style="margin-top:20px">
      <div><label>LATITUDE</label><span>${a.has_position? a.latitude.toFixed(4):'--'}</span></div>
      <div><label>LONGITUDE</label><span>${a.has_position? a.longitude.toFixed(4):'--'}</span></div>
    </div>`;
  bindLock();
}

function renderStats(){
  const d = state.data || {};
  const adsb = d.adsb || {};
  $('statSig').textContent = (adsb.strongest_signal_dbfs??-90).toFixed(1)+' dBFS';
  $('statRate').textContent = (adsb.message_rate??0).toFixed(1)+' msg/sec';
  $('statMsg').textContent = adsb.total_crc_ok??0;
  $('statAc').textContent = adsb.aircraft_count??0;
  $('statSps').textContent = d.effective_sps??0;
  $('statDrops').textContent = d.iq_drops??0;
  const bars = $('sigBars');
  bars.innerHTML = '';
  const level = Math.max(0, Math.min(10, Math.round(((adsb.strongest_signal_dbfs??-90)+90)/6)));
  for (let i=0;i<10;i++){
    const el = document.createElement('i');
    el.style.height = (18+i*6)+'px';
    el.style.opacity = i<level? '1':'0.25';
    bars.appendChild(el);
  }
}

function bindLock(){
  const btn = $('lockBtn');
  if (!btn) return;
  btn.onclick = ()=>{
    const a = selectedAircraft();
    if (!a) return;
    state.lockedIcao = state.lockedIcao===a.icao ? 0 : a.icao;
    render();
  };
}

function render(){
  const d = state.data;
  const adsb = d?.adsb || {};
  const live = !!(adsb.live && d?.streaming);
  $('liveDot').classList.toggle('off', !live);
  $('liveBtn').textContent = live ? 'LIVE' : 'DEMO';
  $('liveBtn').className = live ? 'primary' : 'danger';
  $('acCount').textContent = (adsb.aircraft_count??0)+' AIRCRAFT';
  $('msgRate').textContent = (adsb.message_rate??0).toFixed(1)+'/s';
  $('sps').textContent = d?.effective_sps??0;
  $('ip').textContent = d?.ip || '-';
  $('statusLine').textContent = `${d?.mode||'-'} · ${d?.status||'-'} · ${d?.product||''}`;
  $('settingsSide').textContent = live ? 'LIVE RECEIVER' : 'WAITING FOR LIVE FRAME';
  $('settingsSide').className = live ? 'green' : '';
  $('settingsDetail').textContent = live
    ? '1090 MHz Mode-S capture active'
    : 'Waiting for CRC-valid frames · demo chrome until live';

  const sum = summaryHtml(selectedAircraft());
  $('radarSummary').innerHTML = sum;
  $('listSummary').innerHTML = sum;
  bindLock();
  if (state.view==='radar') drawRadar();
  if (state.view==='list') renderList();
  if (state.view==='target') renderTarget();
  if (state.view==='stats') renderStats();
}

function setView(name){
  state.view = name;
  views.forEach(v=>{
    $('view-'+v).classList.toggle('hidden', v!==name);
  });
  document.querySelectorAll('.nav').forEach(b=>{
    b.classList.toggle('active', b.dataset.view===name);
  });
  render();
}

async function poll(){
  try{
    const r = await fetch('/api/state', {cache:'no-store'});
    if (!r.ok) throw new Error('http '+r.status);
    state.data = await r.json();
    // rebuild aircraft array convenience
    state.data.aircraft = (state.data.adsb && state.data.adsb.aircraft) || [];
    render();
  }catch(e){
    $('statusLine').textContent = 'API offline: '+e.message;
  }
}

async function saveLocation(){
  const lat = parseFloat($('lat').value);
  const lon = parseFloat($('lon').value);
  const range = parseInt($('rangeNm').value,10)||25;
  localStorage.setItem('orcsdr_rx', JSON.stringify({lat,lon,range}));
  try{
    await fetch('/api/location', {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body: JSON.stringify({latitude:lat, longitude:lon, radar_range_nm:range})
    });
  }catch(e){}
  render();
}

async function setMode(mode){
  try{
    await fetch('/api/mode', {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body: JSON.stringify({mode})
    });
  }catch(e){}
}

document.querySelectorAll('.nav').forEach(b=>{
  b.onclick = ()=> setView(b.dataset.view);
});
$('saveLoc').onclick = saveLocation;
$('modeAdsb').onclick = ()=>setMode('ADSB');
$('modeFm').onclick = ()=>setMode('FM');
$('modeWx').onclick = ()=>setMode('WX');
window.addEventListener('resize', ()=>{ if(state.view==='radar') drawRadar(); });

loadLoc();
setView('radar');
poll();
setInterval(poll, 1000);
</script>
</body>
</html>
)HTML";
