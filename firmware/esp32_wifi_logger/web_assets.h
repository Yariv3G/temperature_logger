#pragma once

#include <Arduino.h>

const char WEB_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ATE Temperature Logger</title><style>
:root{color-scheme:dark;--bg:#0b1220;--panel:#151f32;--line:#2d3b55;--text:#e8eef9;--muted:#9badc7;--ok:#3ddc97;--bad:#ff5d73}
*{box-sizing:border-box}body{margin:0;font:14px system-ui;background:var(--bg);color:var(--text)}main{max-width:1150px;margin:auto;padding:20px}
h1{margin:0 0 4px;font-size:25px}.sub,.meta{color:var(--muted)}.sub{margin-bottom:18px}.grid{display:grid;grid-template-columns:2fr 1fr;gap:16px}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:16px}.row{display:flex;gap:8px;align-items:end;flex-wrap:wrap}
label{display:grid;gap:5px;color:var(--muted)}input,select,button{font:inherit;color:var(--text);background:#0d1728;border:1px solid #405171;border-radius:6px;padding:9px}
button{cursor:pointer;font-weight:650}button.primary{background:#1769d2;border-color:#438ee8}button.stop{background:#87263a;border-color:#c64a62}
#alarm{padding:10px;border-radius:6px;background:#123a2d;color:var(--ok);font-weight:700;margin-bottom:10px}#alarm.bad{background:#491b25;color:#ff9dab}
.chartbar{display:flex;gap:7px;align-items:center;flex-wrap:wrap;margin-bottom:4px}.chartbar button{padding:6px 9px}.chartbar .meta{margin-left:auto}
canvas{display:block;width:100%;height:370px}.cards{display:grid;gap:10px;margin-top:12px}.card{border-top:1px solid var(--line);padding-top:9px}
.cardhead{display:flex;justify-content:space-between;align-items:start}.temp{font-size:19px;font-weight:700}.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:7px;margin-top:8px}
.stat{background:#0d1728;border-radius:6px;padding:7px}.stat b{display:block;margin-top:2px}#msg{min-height:20px;color:var(--muted);margin-top:10px}
@media(max-width:760px){.grid{grid-template-columns:1fr}canvas{height:280px}.chartbar .meta{width:100%;margin:0}}
</style></head><body><main>
<h1>ATE Temperature Logger</h1><div class="sub">ESP32 Wi-Fi · DS18B20 · DHCP</div>
<div class="grid"><section class="panel"><div id="alarm">ALARM OUTPUT LOW · IN RANGE</div>
<div class="chartbar"><button id="refresh">Refresh</button><button id="zoomIn">Zoom In</button>
<button id="zoomOut">Zoom Out</button><button id="fit">Fit All / Cumulative</button>
<span id="viewState" class="meta">Cumulative view</span></div>
<canvas id="plot"></canvas><div id="cards" class="cards"></div></section>
<section class="panel"><h2>Acquisition</h2><div class="row">
<button id="start" class="primary">Start</button><button id="stop" class="stop">Stop</button>
<a href="/api/csv?sensor=0"><button>Download CSV</button></a></div>
<h2>Limits and rate</h2><form id="cfg"><div class="row">
<label>Lower °C<input id="lower" name="lower" type="number" step=".1" required></label>
<label>Upper °C<input id="upper" name="upper" type="number" step=".1" required></label>
<label>Sample seconds<select id="preset"><option>1</option><option selected>2</option><option>5</option><option>10</option><option value="custom">Custom</option></select></label>
<label>Custom seconds<input id="interval" name="interval" type="number" min="1" max="3600" value="2" required></label>
</div><p><button class="primary" type="submit">Apply configuration</button></p></form>
<div id="state" class="meta">Connecting…</div><div id="msg"></div>
</section></div></main><script>
const $=id=>document.getElementById(id),colors=['#62a7ff','#3ddc97','#ffc857','#ff7eb6','#b89cff','#52d6d3','#ff8b66','#d4e157'];
const series={},seen={},cv=$('plot'),ctx=cv.getContext('2d');let configured=false,lastSeq=-1,zoomSpan=null,lastStatus=null;
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function ft(s){s=Math.max(0,Math.round(s));let h=(s/3600)|0,m=((s%3600)/60)|0,q=s%60;return h?h+':'+String(m).padStart(2,'0')+':'+String(q).padStart(2,'0'):m+':'+String(q).padStart(2,'0')}
function value(v){return v==null?'—':Number(v).toFixed(2)+' °C'}
function mergePoint(sensor,x,y){if(y==null)return;let known=(seen[sensor]??=new Set());if(known.has(x))return;known.add(x);(series[sensor]??=[]).push({x,y})}
function decimate(points,limit){if(points.length<=limit*2)return points;let result=[],size=points.length/limit;for(let bucket=0;bucket<limit;bucket++){let part=points.slice(Math.floor(bucket*size),Math.floor((bucket+1)*size));if(!part.length)continue;let low=part.reduce((a,b)=>a.y<b.y?a:b),high=part.reduce((a,b)=>a.y>b.y?a:b);result.push(...(low.x<high.x?[low,high]:high.x<low.x?[high,low]:[low]))}return result}
function bounds(){let origin=Infinity,latest=-Infinity;Object.values(series).forEach(points=>points.forEach(p=>{origin=Math.min(origin,p.x);latest=Math.max(latest,p.x)}));if(!isFinite(origin)){origin=0;latest=60}let full=Math.max(1,latest-origin);if(zoomSpan==null)return{origin,xmin:origin,xmax:Math.max(origin+1,latest),full};return{origin,xmin:Math.max(origin,latest-zoomSpan),xmax:Math.max(origin+1,latest),full}}
function draw(){const dpr=devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;if(cv.width!=w*dpr||cv.height!=h*dpr){cv.width=w*dpr;cv.height=h*dpr}ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);
 let b=bounds(),lo=Infinity,hi=-Infinity;Object.values(series).forEach(points=>points.forEach(p=>{if(p.x>=b.xmin&&p.x<=b.xmax){lo=Math.min(lo,p.y);hi=Math.max(hi,p.y)}}));if(!isFinite(lo)){lo=0;hi=50}else{lo=Math.floor(lo-2);hi=Math.ceil(hi+2)}if(hi<=lo)hi=lo+1;
 ctx.font='11px system-ui';for(let i=0;i<=5;i++){let y=10+(h-38)*i/5;ctx.strokeStyle='#2d3b55';ctx.beginPath();ctx.moveTo(38,y);ctx.lineTo(w-8,y);ctx.stroke();ctx.fillStyle='#9badc7';ctx.textAlign='left';ctx.fillText((hi-(hi-lo)*i/5).toFixed(1),2,y+4)}
 ctx.textAlign='center';for(let i=0;i<=5;i++){let x=38+(w-46)*i/5;ctx.strokeStyle='#2d3b55';ctx.beginPath();ctx.moveTo(x,10);ctx.lineTo(x,h-38);ctx.stroke();ctx.fillStyle='#9badc7';ctx.fillText(ft((b.xmin-b.origin)+(b.xmax-b.xmin)*i/5),x,h-23)}
 Object.keys(series).forEach((key,n)=>{let points=series[key].filter(p=>p.x>=b.xmin&&p.x<=b.xmax),shown=decimate(points,Math.max(1,((w-46)/2)|0));ctx.strokeStyle=colors[n%colors.length];ctx.lineWidth=2;ctx.beginPath();shown.forEach((p,i)=>{let x=38+(w-46)*(p.x-b.xmin)/(b.xmax-b.xmin),y=10+(h-48)*(hi-p.y)/(hi-lo);i?ctx.lineTo(x,y):ctx.moveTo(x,y)});ctx.stroke()});
 ctx.fillStyle='#9badc7';ctx.textAlign='center';ctx.fillText('Elapsed time',w/2,h-3);$('viewState').textContent=zoomSpan==null?'Cumulative view':'Following latest · '+ft(zoomSpan)+' window'}
function renderStatus(s){lastStatus=s;if(!configured){$('lower').value=s.lower;$('upper').value=s.upper;$('interval').value=s.interval;configured=true}
 $('state').textContent=(s.running?'RUNNING':'STOPPED')+' · '+s.sensorCount+' sensor(s) · '+s.ip+' · Wi-Fi '+(s.wifi?'connected':'offline')+' · sample '+s.interval+' s';
 $('alarm').className=s.alarm?'bad':'';$('alarm').textContent=s.alarm?'ALARM OUTPUT HIGH · LIMIT/FAULT':'ALARM OUTPUT LOW · IN RANGE';
 $('cards').innerHTML=s.sensors.map((x,i)=>'<div class="card"><div class="cardhead"><span><b>Sensor '+i+'</b><br><span class="meta">'+esc(x.address)+' · '+x.sampleCount+' samples</span></span><span><span class="meta">Current</span><br><span class="temp" style="color:'+colors[i%colors.length]+'">'+(x.valid?x.c.toFixed(2)+' °C':'FAULT')+'</span></span></div><div class="stats"><div class="stat"><span class="meta">Minimum</span><b>'+value(x.min)+'</b></div><div class="stat"><span class="meta">Maximum</span><b>'+value(x.max)+'</b></div><div class="stat"><span class="meta">Average</span><b>'+value(x.average)+'</b></div></div></div>').join('')}
async function getStatus(){let r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);return r.json()}
async function post(path,data){let r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});if(!r.ok)throw Error(await r.text());return r.json()}
async function poll(){try{let s=await getStatus();renderStatus(s);if(s.running&&s.seq!==lastSeq){lastSeq=s.seq;s.sensors.forEach((x,i)=>{if(x.valid)mergePoint(i,s.time,x.c)});draw()}$('msg').textContent=''}catch(e){$('msg').textContent='Connection error: '+e.message}}
async function syncHistory(){try{$('msg').textContent='Synchronizing retained history…';let s=await getStatus();let histories=await Promise.all(s.sensors.map((_,i)=>fetch('/api/history?sensor='+i,{cache:'no-store'}).then(r=>{if(!r.ok)throw Error('History HTTP '+r.status);return r.json()})));histories.forEach(h=>{h.points.forEach(p=>mergePoint(h.sensor,p[0],p[1]));series[h.sensor]?.sort((a,b)=>a.x-b.x)});lastSeq=s.seq;renderStatus(s);draw();$('msg').textContent='History synchronized'}catch(e){$('msg').textContent='Refresh failed: '+e.message}}
$('start').onclick=()=>post('/api/control',{action:'start'}).then(()=>{Object.keys(series).forEach(k=>delete series[k]);Object.keys(seen).forEach(k=>delete seen[k]);lastSeq=-1;zoomSpan=null;draw();return poll()}).catch(e=>$('msg').textContent=e.message);
$('stop').onclick=()=>post('/api/control',{action:'stop'}).then(poll).catch(e=>$('msg').textContent=e.message);
$('refresh').onclick=syncHistory;$('zoomIn').onclick=()=>{let b=bounds();zoomSpan=Math.max(10,(zoomSpan??b.full)/2);draw()};$('zoomOut').onclick=()=>{let b=bounds();zoomSpan=zoomSpan==null?null:zoomSpan*2;if(zoomSpan>=b.full)zoomSpan=null;draw()};$('fit').onclick=()=>{zoomSpan=null;draw()};
$('preset').onchange=()=>{if($('preset').value!='custom')$('interval').value=$('preset').value;$('interval').focus()};
$('cfg').onsubmit=e=>{e.preventDefault();post('/api/config',{lower:$('lower').value,upper:$('upper').value,interval:$('interval').value}).then(()=>{configured=false;poll()}).catch(e=>$('msg').textContent=e.message)};
addEventListener('resize',draw);setInterval(poll,1000);syncHistory();draw();
</script></body></html>
)HTML";
