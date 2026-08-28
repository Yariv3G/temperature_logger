#pragma once

#include <Arduino.h>

const char WEB_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ATE Temperature Logger</title><style>
:root{color-scheme:dark;--bg:#0b1220;--panel:#151f32;--line:#2d3b55;--text:#e8eef9;--muted:#9badc7;--ok:#3ddc97;--bad:#ff5d73;--accent:#62a7ff}
*{box-sizing:border-box}body{margin:0;font:14px system-ui;background:var(--bg);color:var(--text)}main{max-width:1100px;margin:auto;padding:20px}
h1{margin:0 0 4px;font-size:25px}.sub{color:var(--muted);margin-bottom:18px}.grid{display:grid;grid-template-columns:2fr 1fr;gap:16px}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:16px}.row{display:flex;gap:10px;align-items:end;flex-wrap:wrap}
label{display:grid;gap:5px;color:var(--muted)}input,select,button{font:inherit;color:var(--text);background:#0d1728;border:1px solid #405171;border-radius:6px;padding:9px}
button{cursor:pointer;font-weight:650}button.primary{background:#1769d2;border-color:#438ee8}button.stop{background:#87263a;border-color:#c64a62}
#alarm{padding:10px;border-radius:6px;background:#123a2d;color:var(--ok);font-weight:700;margin-bottom:12px}#alarm.bad{background:#491b25;color:#ff9dab}
canvas{display:block;width:100%;height:370px}.cards{display:grid;gap:8px;margin-top:14px}.card{display:flex;justify-content:space-between;border-bottom:1px solid var(--line);padding:7px 0}
.temp{font-size:18px;font-weight:700}.meta{color:var(--muted);font-size:12px}#msg{min-height:20px;color:var(--muted);margin-top:10px}
@media(max-width:760px){.grid{grid-template-columns:1fr}canvas{height:280px}}
</style></head><body><main>
<h1>ATE Temperature Logger</h1><div class="sub">ESP32 Wi-Fi · DS18B20 · 10.100.102.247</div>
<div class="grid"><section class="panel"><div id="alarm">ALARM OUTPUT LOW · IN RANGE</div>
<canvas id="plot"></canvas><div id="cards" class="cards"></div></section>
<section class="panel"><h2>Acquisition</h2><div class="row">
<button id="start" class="primary">Start</button><button id="stop" class="stop">Stop</button>
<a href="/api/csv"><button>Download CSV</button></a></div>
<h2>Limits and rate</h2><form id="cfg"><div class="row">
<label>Lower °C<input id="lower" name="lower" type="number" step=".1" required></label>
<label>Upper °C<input id="upper" name="upper" type="number" step=".1" required></label>
<label>Sample seconds<select id="preset"><option>1</option><option selected>2</option><option>5</option><option>10</option><option value="custom">Custom</option></select></label>
<label>Custom seconds<input id="interval" name="interval" type="number" min="1" max="3600" value="2" required></label>
</div><p><button class="primary" type="submit">Apply configuration</button></p></form>
<div id="state" class="meta">Connecting…</div><div id="msg"></div>
</section></div></main><script>
const colors=['#62a7ff','#3ddc97','#ffc857','#ff7eb6','#b89cff','#52d6d3','#ff8b66','#d4e157'];
const series={},maxPoints=300,cv=document.querySelector('#plot'),ctx=cv.getContext('2d');let configured=false,lastSeq=-1;
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function draw(){const dpr=devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;if(cv.width!=w*dpr||cv.height!=h*dpr){cv.width=w*dpr;cv.height=h*dpr}ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);ctx.strokeStyle='#2d3b55';ctx.fillStyle='#9badc7';ctx.font='11px system-ui';
let vals=Object.values(series).flatMap(x=>x.map(p=>p.y)),lo=vals.length?Math.floor(Math.min(...vals)-2):0,hi=vals.length?Math.ceil(Math.max(...vals)+2):50;if(hi<=lo)hi=lo+1;
for(let i=0;i<=5;i++){let y=10+(h-30)*i/5;ctx.beginPath();ctx.moveTo(38,y);ctx.lineTo(w-8,y);ctx.stroke();ctx.fillText((hi-(hi-lo)*i/5).toFixed(1),2,y+4)}
let all=Object.values(series).flat(),xmin=all.length?Math.min(...all.map(p=>p.x)):0,xmax=all.length?Math.max(...all.map(p=>p.x)):60;if(xmax<=xmin)xmax=xmin+60;
Object.keys(series).forEach((k,n)=>{ctx.strokeStyle=colors[n%colors.length];ctx.lineWidth=2;ctx.beginPath();series[k].forEach((p,i)=>{let x=38+(w-46)*(p.x-xmin)/(xmax-xmin),y=10+(h-30)*(hi-p.y)/(hi-lo);i?ctx.lineTo(x,y):ctx.moveTo(x,y)});ctx.stroke()});ctx.fillStyle='#9badc7';ctx.fillText('Elapsed time (s)',Math.max(40,w/2-40),h-3)}
async function post(path,data){let r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});if(!r.ok)throw Error(await r.text());return r.json()}
async function poll(){try{let r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);let s=await r.json();
if(!configured){lower.value=s.lower;upper.value=s.upper;interval.value=s.interval;configured=true}
state.textContent=(s.running?'RUNNING':'STOPPED')+' · '+s.sensorCount+' sensor(s) · Wi-Fi '+(s.wifi?'connected':'offline')+' · sample '+s.interval+' s';
alarm.className=s.alarm?'bad':'';alarm.textContent=s.alarm?'ALARM OUTPUT HIGH · LIMIT/FAULT':'ALARM OUTPUT LOW · IN RANGE';
cards.innerHTML=s.sensors.map((x,i)=>'<div class="card"><span><b>Sensor '+i+'</b><br><span class="meta">'+esc(x.address)+'</span></span><span class="temp" style="color:'+colors[i%colors.length]+'">'+(x.valid?x.c.toFixed(2)+' °C':'FAULT')+'</span></div>').join('');
if(s.running&&s.seq!==lastSeq){lastSeq=s.seq;s.sensors.forEach((x,i)=>{if(!x.valid)return;(series[i]??=[]).push({x:s.time,y:x.c});if(series[i].length>maxPoints)series[i].shift()});draw()}msg.textContent=''}catch(e){msg.textContent='Connection error: '+e.message}}
start.onclick=()=>post('/api/control',{action:'start'}).then(poll).catch(e=>msg.textContent=e.message);
stop.onclick=()=>post('/api/control',{action:'stop'}).then(poll).catch(e=>msg.textContent=e.message);
preset.onchange=()=>{if(preset.value!='custom')interval.value=preset.value;interval.focus()};
cfg.onsubmit=e=>{e.preventDefault();post('/api/config',{lower:lower.value,upper:upper.value,interval:interval.value}).then(()=>{configured=false;poll()}).catch(e=>msg.textContent=e.message)};
addEventListener('resize',draw);setInterval(poll,1000);poll();draw();
</script></body></html>

)HTML";
