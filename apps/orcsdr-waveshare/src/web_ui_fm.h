#pragma once

/* FM radio station UI: PCM audio + live scope + RF waterfall. */
static const char kWebUiFmHtml[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>OrcSDR FM Radio</title>
<style>
:root{
  --bg:#07090c; --panel:#101820; --panel2:#0c141c; --line:#243040;
  --cyan:#3de0ff; --amber:#ffb020; --lime:#8cff2e;
  --text:#e8eef6; --muted:#8b97a8; --danger:#ff4d6a;
}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;background:
  radial-gradient(1200px 600px at 10% -10%,#123 0%,transparent 50%),
  radial-gradient(900px 500px at 100% 0%,#1a1020 0%,transparent 45%),
  var(--bg);color:var(--text);
  font-family:"Segoe UI",Inter,Roboto,Helvetica,Arial,sans-serif}
a{color:var(--cyan);text-decoration:none}
.shell{max-width:1200px;margin:0 auto;padding:18px 18px 40px}
.top{display:flex;flex-wrap:wrap;gap:12px 20px;align-items:center;margin-bottom:16px}
.brand{font-size:28px;font-weight:800;letter-spacing:.04em}
.brand span{color:var(--cyan)}
.pill{border:1px solid var(--line);background:var(--panel);border-radius:999px;
  padding:6px 12px;color:var(--muted);font-size:13px}
.pill.on{color:var(--lime);border-color:#3a6}
.grid{display:grid;grid-template-columns:1.35fr .85fr;gap:16px}
@media(max-width:960px){.grid{grid-template-columns:1fr}}
.card{background:linear-gradient(180deg,var(--panel),var(--panel2));
  border:1px solid var(--line);border-radius:18px;padding:18px}
h2{margin:0 0 10px;font-size:12px;letter-spacing:.14em;color:var(--muted);font-weight:700}
.freq{font-size:clamp(42px,8vw,72px);font-weight:800;line-height:1;
  font-variant-numeric:tabular-nums;text-shadow:0 0 30px rgba(61,224,255,.35)}
.freq small{font-size:.4em;color:var(--cyan);margin-left:8px}
.meta{display:flex;flex-wrap:wrap;gap:10px 18px;margin:10px 0 14px;color:var(--muted)}
.meta b{color:var(--text);font-weight:600}
.dial-row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
button,input[type=range]{accent-color:var(--cyan)}
button{border:1px solid var(--line);background:#15202b;color:var(--text);
  border-radius:12px;padding:10px 14px;font:inherit;cursor:pointer}
button.primary{background:linear-gradient(135deg,#1b6,#0a4);border-color:#5d5}
button.danger{background:linear-gradient(135deg,#622,#300);border-color:var(--danger)}
button:disabled{opacity:.5;cursor:wait}
input[type=number]{background:#0b121a;border:1px solid var(--line);color:var(--text);
  border-radius:10px;padding:10px 12px;width:140px;font-size:16px}
.stations{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:8px;max-height:360px;overflow:auto}
.station{text-align:left;padding:12px;border-radius:12px;background:#0d1620;border:1px solid var(--line)}
.station.active{border-color:var(--cyan);box-shadow:inset 0 0 0 1px var(--cyan)}
.station .mhz{font-size:18px;font-weight:700;color:var(--cyan)}
.station .name{font-size:12px;color:var(--muted);margin-top:4px}
.viz{display:grid;gap:10px;margin-top:8px}
canvas{width:100%;display:block;background:#060a10;border-radius:12px;border:1px solid var(--line)}
#scope{height:110px}
#waterfall{height:180px}
.eq{display:grid;gap:10px}
.eq label{display:flex;justify-content:space-between;font-size:13px;color:var(--muted)}
.eq input{width:100%}
.vu{height:14px;background:#0a1018;border-radius:8px;overflow:hidden;border:1px solid var(--line)}
.vu>i{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--lime),var(--amber),var(--danger));transition:width .08s linear}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.foot{margin-top:14px;color:var(--muted);font-size:12px}
#statusMsg{min-height:1.2em;color:var(--amber);font-size:13px;margin-top:8px}
</style>
</head>
<body>
<div class="shell">
  <div class="top">
    <div class="brand">OrcSDR <span>FM</span></div>
    <div id="livePill" class="pill">STREAM OFF</div>
    <div id="ipPill" class="pill">IP -</div>
    <div id="audioPill" class="pill">AUDIO OFF</div>
    <a class="pill" href="/">ADS-B DASHBOARD</a>
  </div>

  <div class="grid">
    <section class="card">
      <h2>ON AIR</h2>
      <div class="freq"><span id="freq">96.1</span><small>MHz</small></div>
      <div class="meta">
        <div>Station <b id="stationName">—</b></div>
        <div>Signal <b id="sig">-90.0</b> dBFS</div>
        <div>SPS <b id="sps">0</b></div>
        <div>PCM queue <b id="pcmQ">0</b></div>
        <div>Chunk <b id="buf">0</b> ms</div>
      </div>

      <div class="viz">
        <div>
          <h2>SCOPE (audio / RF level)</h2>
          <canvas id="scope"></canvas>
          <div class="vu" style="margin-top:8px"><i id="vuBar"></i></div>
        </div>
        <div>
          <h2>WATERFALL (RF spectrum history)</h2>
          <canvas id="waterfall"></canvas>
        </div>
      </div>

      <div class="dial-row" style="margin-top:16px">
        <button id="btnDown">− 0.2</button>
        <input id="freqIn" type="number" min="88" max="108" step="0.1" value="96.1"/>
        <button id="btnUp">+ 0.2</button>
        <button id="btnTune" class="primary">TUNE</button>
        <button id="btnPlay" class="primary">▶ PLAY</button>
        <button id="btnStop" class="danger">■ STOP</button>
      </div>
      <div id="statusMsg"></div>
      <div class="foot">Device demodulates FM to 48 kHz PCM over Ethernet. Browser plays audio and draws scope + waterfall.
      Click <b>PLAY</b> once (browsers require a user gesture for sound).</div>
    </section>

    <section class="card">
      <h2>STATIONS</h2>
      <div class="stations" id="stations"></div>
      <div class="row" style="margin-top:12px">
        <input id="newName" placeholder="Label" style="flex:1;min-width:100px;background:#0b121a;border:1px solid var(--line);color:var(--text);border-radius:10px;padding:10px"/>
        <button id="btnAdd">ADD CURRENT</button>
      </div>
    </section>
  </div>

  <section class="card" style="margin-top:16px">
    <h2>BROWSER DSP RACK</h2>
    <div class="grid" style="grid-template-columns:repeat(auto-fit,minmax(220px,1fr))">
      <div class="eq">
        <label>Volume <span id="volLbl">0.90</span></label>
        <input id="vol" type="range" min="0" max="1.5" step="0.01" value="0.9"/>
        <label>Bass <span id="bassLbl">2 dB</span></label>
        <input id="bass" type="range" min="-12" max="12" step="0.5" value="2"/>
        <label>Mid <span id="midLbl">0 dB</span></label>
        <input id="mid" type="range" min="-12" max="12" step="0.5" value="0"/>
        <label>Treble <span id="trebleLbl">1 dB</span></label>
        <input id="treble" type="range" min="-12" max="12" step="0.5" value="1"/>
      </div>
      <div class="eq">
        <label>Compressor threshold <span id="thrLbl">-24 dB</span></label>
        <input id="thr" type="range" min="-60" max="0" step="1" value="-24"/>
        <label>Presence (2.8 kHz) <span id="presLbl">3 dB</span></label>
        <input id="pres" type="range" min="-12" max="12" step="0.5" value="3"/>
        <label>Stereo width <span id="widthLbl">0.30</span></label>
        <input id="width" type="range" min="0" max="1" step="0.01" value="0.3"/>
        <label>Noise gate <span id="gateLbl">-55 dB</span></label>
        <input id="gate" type="range" min="-80" max="-20" step="1" value="-55"/>
      </div>
    </div>
  </section>
</div>

<script>
const DEFAULT_STATIONS = [
  {name:'Preset A', mhz:88.5},{name:'Preset B', mhz:91.7},
  {name:'Preset C', mhz:94.9},{name:'KZEL-ish', mhz:96.1},
  {name:'Preset D', mhz:98.5},{name:'Preset E', mhz:101.3},
  {name:'Preset F', mhz:104.7},{name:'Preset G', mhz:107.7},
];

const state = {
  playing: false,
  streaming: false,
  freqHz: 96100000,
  stations: JSON.parse(localStorage.getItem('orcsdr_fm_stations')||'null') || DEFAULT_STATIONS,
  audioCtx: null,
  nodes: null,
  nextTime: 0,
  audioTimer: null,
  uiTimer: null,
  lastSpectrum: new Array(64).fill(0),
  waterfall: null, // ImageData rows
  lastPcm: null,
};

const $ = id => document.getElementById(id);
const mhzOf = hz => (hz/1e6).toFixed(1);
const saveStations = () => localStorage.setItem('orcsdr_fm_stations', JSON.stringify(state.stations));
const setMsg = t => { $('statusMsg').textContent = t || ''; };

async function postJson(url, obj){
  const r = await fetch(url, {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body: JSON.stringify(obj)
  });
  if(!r.ok) throw new Error(url+' '+r.status);
  return r.json().catch(()=>({}));
}

function renderStations(){
  const root = $('stations');
  root.innerHTML = '';
  state.stations.forEach(s=>{
    const b = document.createElement('button');
    b.className = 'station' + (Math.abs(s.mhz*1e6 - state.freqHz)<1000 ? ' active':'');
    b.innerHTML = `<div class="mhz">${s.mhz.toFixed(1)}</div><div class="name">${s.name}</div>`;
    b.onclick = ()=> tuneTo(Math.round(s.mhz*1e6), s.name);
    root.appendChild(b);
  });
}

function ensureAudio(){
  if (state.audioCtx) return state.audioCtx;
  const AC = window.AudioContext || window.webkitAudioContext;
  const ctx = new AC({sampleRate:48000});
  const input = ctx.createGain();
  const bass = ctx.createBiquadFilter(); bass.type='lowshelf'; bass.frequency.value=120;
  const mid = ctx.createBiquadFilter(); mid.type='peaking'; mid.frequency.value=1000; mid.Q.value=0.9;
  const presence = ctx.createBiquadFilter(); presence.type='peaking'; presence.frequency.value=2800; presence.Q.value=1.1;
  const treble = ctx.createBiquadFilter(); treble.type='highshelf'; treble.frequency.value=6500;
  const comp = ctx.createDynamicsCompressor();
  comp.threshold.value=-24; comp.knee.value=18; comp.ratio.value=3.5;
  comp.attack.value=0.008; comp.release.value=0.18;
  const delay = ctx.createDelay(0.03); delay.delayTime.value=0.012;
  const width = ctx.createGain(); width.gain.value=0.3;
  const dry = ctx.createGain(); dry.gain.value=1;
  const master = ctx.createGain(); master.gain.value=0.9;
  const analyser = ctx.createAnalyser(); analyser.fftSize=2048;

  input.connect(bass); bass.connect(mid); mid.connect(presence); presence.connect(treble);
  treble.connect(comp); comp.connect(dry); dry.connect(master);
  treble.connect(delay); delay.connect(width); width.connect(master);
  master.connect(analyser); analyser.connect(ctx.destination);

  state.audioCtx = ctx;
  state.nodes = {input,bass,mid,presence,treble,comp,width,master,analyser,delay};
  applyEq();
  return ctx;
}

function applyEq(){
  if(!state.nodes) return;
  const n = state.nodes;
  n.master.gain.value = +$('vol').value;
  n.bass.gain.value = +$('bass').value;
  n.mid.gain.value = +$('mid').value;
  n.presence.gain.value = +$('pres').value;
  n.treble.gain.value = +$('treble').value;
  n.comp.threshold.value = +$('thr').value;
  n.width.gain.value = +$('width').value;
  $('volLbl').textContent = (+$('vol').value).toFixed(2);
  $('bassLbl').textContent = $('bass').value+' dB';
  $('midLbl').textContent = $('mid').value+' dB';
  $('trebleLbl').textContent = $('treble').value+' dB';
  $('presLbl').textContent = $('pres').value+' dB';
  $('thrLbl').textContent = $('thr').value+' dB';
  $('widthLbl').textContent = (+$('width').value).toFixed(2);
  $('gateLbl').textContent = $('gate').value+' dB';
}

function sizeCanvas(c, cssH){
  const dpr = devicePixelRatio || 1;
  const w = Math.max(100, c.clientWidth|0);
  const h = cssH || c.clientHeight || 110;
  if (c.width !== (w*dpr|0) || c.height !== (h*dpr|0)) {
    c.width = w*dpr|0;
    c.height = h*dpr|0;
  }
  const g = c.getContext('2d');
  g.setTransform(dpr,0,0,dpr,0,0);
  return {g, w, h};
}

function drawScopeFromPcm(int16){
  const {g,w,h} = sizeCanvas($('scope'), 110);
  g.fillStyle = '#060a10'; g.fillRect(0,0,w,h);
  g.strokeStyle = '#1b2a3a';
  for(let i=0;i<3;i++){ g.beginPath(); g.moveTo(0,h*(i+1)/4); g.lineTo(w,h*(i+1)/4); g.stroke(); }
  if(!int16 || !int16.length){
    g.fillStyle='#8b97a8'; g.font='12px sans-serif';
    g.fillText(state.streaming ? 'waiting for PCM…' : 'RF stream off — press PLAY', 12, 20);
    return;
  }
  g.strokeStyle = '#3de0ff'; g.lineWidth=2; g.beginPath();
  const step = Math.max(1, (int16.length / w)|0);
  for(let x=0,i=0; x<w; x++, i+=step){
    const v = int16[Math.min(i,int16.length-1)] / 32768;
    const y = h*0.5 - v*h*0.42;
    if(x===0) g.moveTo(x,y); else g.lineTo(x,y);
  }
  g.stroke();
  let peak=0; for(let i=0;i<int16.length;i++) peak=Math.max(peak, Math.abs(int16[i]));
  $('vuBar').style.width = Math.min(100, peak/32768*130)+'%';
}

function drawScopeFromSpectrum(bins){
  const {g,w,h} = sizeCanvas($('scope'), 110);
  g.fillStyle = '#060a10'; g.fillRect(0,0,w,h);
  if(!bins || !bins.length){
    g.fillStyle='#8b97a8'; g.font='12px sans-serif';
    g.fillText('no spectrum yet', 12, 20);
    return;
  }
  let peak = 1e-9; for(const v of bins) peak=Math.max(peak,v);
  const bw = w/bins.length;
  for(let i=0;i<bins.length;i++){
    const bh = (bins[i]/peak)*(h-6);
    g.fillStyle = i%2 ? '#3de0ff' : '#1faa66';
    g.fillRect(i*bw, h-bh, Math.max(1,bw-1), bh);
  }
  // if not playing, VU from spectrum energy
  if(!state.playing){
    const e = bins.reduce((a,b)=>a+b,0)/bins.length;
    $('vuBar').style.width = Math.min(100, Math.sqrt(e)*80)+'%';
  }
}

function colorMap(t){
  // t 0..1 -> blue/cyan/green/yellow/red
  const x = Math.max(0, Math.min(1, t));
  const r = Math.min(255, Math.floor(x*3*255));
  const g = Math.min(255, Math.floor((x>0.3? (x-0.3)*2.2 : 0)*255));
  const b = Math.min(255, Math.floor((1-x)*220 + 40));
  return [r,g,b,255];
}

function pushWaterfall(bins){
  const c = $('waterfall');
  const dpr = devicePixelRatio || 1;
  const w = Math.max(100, c.clientWidth|0);
  const h = 180;
  if (c.width !== (w*dpr|0) || c.height !== (h*dpr|0)) {
    c.width = w*dpr|0; c.height = h*dpr|0;
    state.waterfall = null;
  }
  const g = c.getContext('2d');
  // scroll up by 1 CSS pixel
  g.setTransform(dpr,0,0,dpr,0,0);
  g.drawImage(c, 0, dpr, c.width, c.height-dpr, 0, 0, w, h-1);

  if(!bins || !bins.length){
    g.fillStyle = '#060a10';
    g.fillRect(0, h-1, w, 1);
    return;
  }
  let peak = 1e-9; for(const v of bins) peak=Math.max(peak,v);
  // log-ish scale
  const row = g.createImageData(w, 1);
  for(let x=0;x<w;x++){
    const i = Math.min(bins.length-1, (x * bins.length / w)|0);
    const lin = bins[i] / peak;
    const t = Math.min(1, Math.log10(1 + lin*9));
    const [r,gv,b,a] = colorMap(t);
    const o = x*4;
    row.data[o]=r; row.data[o+1]=gv; row.data[o+2]=b; row.data[o+3]=a;
  }
  // putImageData ignores current transform — write in device pixels at bottom
  const bottom = c.height - dpr;
  // scale row to device width
  const big = g.createImageData(c.width, dpr);
  for(let y=0;y<dpr;y++){
    for(let x=0;x<c.width;x++){
      const src = ((x/dpr)|0)*4;
      const dst = (y*c.width + x)*4;
      big.data[dst]=row.data[src];
      big.data[dst+1]=row.data[src+1];
      big.data[dst+2]=row.data[src+2];
      big.data[dst+3]=255;
    }
  }
  g.setTransform(1,0,0,1,0,0);
  g.putImageData(big, 0, bottom);
}

async function ensureFmStream(){
  setMsg('Starting FM stream…');
  await postJson('/api/mode', {mode:'FM'});
  await postJson('/api/freq', {frequency_hz: state.freqHz});
  await postJson('/api/stream', {action:'start'});
  // wait until streaming + pcm
  for(let i=0;i<20;i++){
    await new Promise(r=>setTimeout(r,200));
    const j = await (await fetch('/api/state',{cache:'no-store'})).json();
    if(j.mode==='FM' && j.streaming){
      setMsg('FM stream live');
      return true;
    }
  }
  setMsg('FM stream did not start — check RTL USB (lower-left by Ethernet)');
  return false;
}

async function tuneTo(hz, name){
  hz = Math.max(88e6, Math.min(108e6, hz));
  state.freqHz = hz;
  $('freq').textContent = mhzOf(hz);
  $('freqIn').value = mhzOf(hz);
  if(name) $('stationName').textContent = name;
  else {
    const hit = state.stations.find(s => Math.abs(s.mhz*1e6 - hz)<1000);
    $('stationName').textContent = hit ? hit.name : 'Manual';
  }
  renderStations();
  try{
    await postJson('/api/mode', {mode:'FM'});
    await postJson('/api/freq', {frequency_hz: hz});
    if(!state.streaming) await postJson('/api/stream', {action:'start'});
    setMsg('Tuned '+mhzOf(hz)+' MHz');
  }catch(e){ setMsg('Tune failed: '+e.message); }
}

function schedulePcm(int16, rate){
  const ctx = ensureAudio();
  if(ctx.state === 'suspended') ctx.resume();
  const gateDb = +$('gate').value;
  const gateLin = Math.pow(10, gateDb/20);
  const f32 = new Float32Array(int16.length);
  for(let i=0;i<int16.length;i++){
    let v = int16[i]/32768;
    if (Math.abs(v) < gateLin) v *= 0.12;
    f32[i]=v;
  }
  const audioBuf = ctx.createBuffer(1, f32.length, rate||48000);
  audioBuf.copyToChannel(f32, 0);
  const src = ctx.createBufferSource();
  src.buffer = audioBuf;
  src.connect(state.nodes.input);
  const now = ctx.currentTime;
  // keep ~80–250 ms jitter buffer
  if(state.nextTime < now + 0.06) state.nextTime = now + 0.12;
  if(state.nextTime > now + 0.45){
    // too far ahead — drop this chunk
    return;
  }
  src.start(state.nextTime);
  state.nextTime += audioBuf.duration;
}

async function pullAudio(){
  if(!state.playing) return;
  try{
    const r = await fetch('/api/audio?max=4800', {cache:'no-store'});
    if(!r.ok){ setMsg('audio HTTP '+r.status); return; }
    const buf = await r.arrayBuffer();
    if(buf.byteLength < 16) return;
    const view = new DataView(buf);
    const magic = String.fromCharCode(view.getUint8(0),view.getUint8(1),view.getUint8(2),view.getUint8(3));
    if(magic !== 'PCM1'){ setMsg('bad audio magic'); return; }
    const rate = view.getUint32(4, true);
    const count = view.getUint32(12, true);
    if(count <= 0){
      $('buf').textContent = '0';
      return;
    }
    const samples = new Int16Array(buf, 16, count);
    state.lastPcm = samples;
    $('buf').textContent = Math.round(count/rate*1000);
    schedulePcm(samples, rate);
    drawScopeFromPcm(samples);
  }catch(e){
    setMsg('audio pull: '+e.message);
  }
}

async function pollState(){
  try{
    const r = await fetch('/api/state',{cache:'no-store'});
    const j = await r.json();
    $('ipPill').textContent = 'IP '+(j.ip||'-');
    $('sps').textContent = j.effective_sps||0;
    state.streaming = !!(j.streaming && j.mode==='FM');
    $('livePill').textContent = state.streaming ? 'FM LIVE' : ((j.mode||'IDLE')+' / RF OFF');
    $('livePill').classList.toggle('on', state.streaming);
    if(j.frequency_hz){
      state.freqHz = j.frequency_hz;
      $('freq').textContent = mhzOf(j.frequency_hz);
    }
    const fm = j.fm || {};
    $('sig').textContent = (fm.signal_dbfs!=null ? fm.signal_dbfs : -90).toFixed(1);
    $('pcmQ').textContent = fm.pcm_available||0;
    if(Array.isArray(fm.spectrum) && fm.spectrum.length){
      state.lastSpectrum = fm.spectrum;
      pushWaterfall(fm.spectrum);
      if(!state.playing) drawScopeFromSpectrum(fm.spectrum);
    }
  }catch(e){
    setMsg('state: '+e.message);
  }
}

async function startPlay(){
  $('btnPlay').disabled = true;
  try{
    ensureAudio();
    await state.audioCtx.resume();
    const ok = await ensureFmStream();
    if(!ok) return;
    state.playing = true;
    state.nextTime = 0;
    $('audioPill').textContent = 'AUDIO ON';
    $('audioPill').classList.add('on');
    $('btnPlay').textContent = '● PLAYING';
    if(state.audioTimer) clearInterval(state.audioTimer);
    state.audioTimer = setInterval(pullAudio, 35);
    setMsg('Playing — use volume if silent');
  }catch(e){
    setMsg('Play failed: '+e.message);
  }finally{
    $('btnPlay').disabled = false;
  }
}

function stopPlay(){
  state.playing = false;
  if(state.audioTimer){ clearInterval(state.audioTimer); state.audioTimer=null; }
  $('btnPlay').textContent = '▶ PLAY';
  $('audioPill').textContent = 'AUDIO OFF';
  $('audioPill').classList.remove('on');
  if(state.audioCtx) state.audioCtx.suspend();
  setMsg('Audio stopped (RF may still stream)');
}

$('btnPlay').onclick = startPlay;
$('btnStop').onclick = stopPlay;
$('btnTune').onclick = ()=> tuneTo(Math.round(parseFloat($('freqIn').value)*1e6));
$('btnUp').onclick = ()=> tuneTo(state.freqHz + 200000);
$('btnDown').onclick = ()=> tuneTo(state.freqHz - 200000);
$('btnAdd').onclick = ()=>{
  const name = $('newName').value.trim() || 'Preset';
  state.stations.push({name, mhz: state.freqHz/1e6});
  saveStations(); renderStations();
};
['vol','bass','mid','treble','pres','thr','width','gate'].forEach(id=>{
  $(id).oninput = applyEq;
});
window.addEventListener('resize', ()=>{
  if(state.lastPcm && state.playing) drawScopeFromPcm(state.lastPcm);
  else drawScopeFromSpectrum(state.lastSpectrum);
});

renderStations();
// boot: enter FM + start RF (no audio until PLAY)
(async ()=>{
  try{
    await postJson('/api/mode', {mode:'FM'});
    await postJson('/api/freq', {frequency_hz: state.freqHz});
    await postJson('/api/stream', {action:'start'});
    setMsg('RF starting — press PLAY for browser audio');
  }catch(e){ setMsg(e.message); }
  pollState();
  state.uiTimer = setInterval(pollState, 200);
})();
</script>
</body>
</html>
)HTML";