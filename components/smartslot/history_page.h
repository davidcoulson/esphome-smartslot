// SPDX-License-Identifier: GPL-2.0-or-later
// /history: a self-contained page (no external assets, works offline) that charts /history.json.
#pragma once

namespace esphome {
namespace smartslot {

static const char HISTORY_PAGE[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>UPS history</title>
<style>
body{font:14px system-ui,sans-serif;margin:0;padding:12px;background:#111;color:#ddd}
h1{font-size:18px;margin:0 0 8px}a{color:#8cf}button{margin:0 4px 8px 0;padding:4px 10px;background:#333;color:#ddd;border:1px solid #555;border-radius:4px;cursor:pointer}
button.on{background:#48a;border-color:#6ad}canvas{width:100%;height:280px;display:block;background:#181818;border-radius:4px;margin-bottom:8px}
#bars{height:140px}#stats,#legend{color:#999;font-size:12px}#legend span{margin-right:12px}
</style></head><body>
<h1>UPS history <a href="/" style="font-size:13px;font-weight:normal">&larr; live view</a></h1>
<div id="ranges"><button data-h="24">24 h</button><button data-h="168">7 d</button><button data-h="720">30 d</button><button data-h="2160">90 d</button></div>
<div id="legend"><span style="color:#4c4">&#9644; charge %</span><span style="color:#fc4">&#9644; load %</span><span style="color:#c8f">&#9644; temp &deg;C</span>
<span style="color:#4cf">&#9644; input V</span><span style="color:#f84">&#9644; output V</span><span style="color:#c44">&#9644; on battery</span></div>
<canvas id="chart"></canvas>
<div id="legend">Minutes on battery per day</div>
<canvas id="bars"></canvas>
<div id="stats">loading&hellip;</div>
<script>
const C=[['#4c4',1,0],['#fc4',5,0],['#c8f',6,0],['#4cf',3,1],['#f84',4,1]];// [colour, column, axis]; row: ts,charge,runtime,vin,vout,load,temp,onbatt
let hours=24;
function px(c){const r=c.getBoundingClientRect();c.width=r.width*devicePixelRatio;c.height=r.height*devicePixelRatio;const g=c.getContext('2d');g.scale(devicePixelRatio,devicePixelRatio);return [g,r.width,r.height];}
function fmt(t){const d=new Date(t*1000);return hours>48?d.toLocaleDateString(undefined,{month:'short',day:'numeric'}):d.toLocaleTimeString(undefined,{hour:'2-digit',minute:'2-digit'});}
function chart(d){const [g,W,H]=px(document.getElementById('chart'));const L=36,R=40,T=8,B=22,w=W-L-R,h=H-T-B;
 const t0=d.from,t1=d.to,x=t=>L+(t-t0)/(t1-t0)*w;
 let vmin=Infinity,vmax=-Infinity;for(const r of d.rows)for(const c of [3,4])if(r[c]!=null){vmin=Math.min(vmin,r[c]);vmax=Math.max(vmax,r[c]);}
 if(vmin===Infinity){vmin=0;vmax=1;}vmin=Math.floor(vmin/10)*10-10;vmax=Math.ceil(vmax/10)*10+10;
 const y0=v=>T+h-v/100*h,y1=v=>T+h-(v-vmin)/(vmax-vmin)*h;
 g.fillStyle='rgba(200,60,60,.25)';for(const r of d.rows)if(r[7]>0)g.fillRect(x(r[0]),T,Math.max(1,x(r[0]+d.bucket)-x(r[0])),h);
 g.strokeStyle='#333';g.fillStyle='#888';g.font='11px sans-serif';g.textAlign='right';
 for(let v=0;v<=100;v+=25){const y=y0(v);g.beginPath();g.moveTo(L,y);g.lineTo(L+w,y);g.stroke();g.fillText(v,L-4,y+4);}
 g.textAlign='left';for(let v=vmin;v<=vmax;v+=(vmax-vmin)/4)g.fillText(v.toFixed(0),L+w+4,y1(v)+4);
 g.textAlign='center';for(let i=0;i<=6;i++){const t=t0+(t1-t0)*i/6;g.fillText(fmt(t),x(t),H-6);}
 for(const [col,c,ax] of C){g.strokeStyle=col;g.lineWidth=1.5;g.beginPath();let pen=false;
  for(const r of d.rows){if(r[c]==null){pen=false;continue;}const X=x(r[0]),Y=ax?y1(r[c]):y0(r[c]);pen?g.lineTo(X,Y):g.moveTo(X,Y);pen=true;}g.stroke();}}
function bars(d){const [g,W,H]=px(document.getElementById('bars'));const days={};
 for(const r of d.rows)if(r[7]!=null){const k=new Date(r[0]*1000).toDateString();days[k]=(days[k]||0)+r[7]/100*d.bucket/60;}
 const keys=Object.keys(days),max=Math.max(1,...keys.map(k=>days[k])),L=30,T=6,B=18,bw=(W-L)/Math.max(keys.length,1);
 g.fillStyle='#888';g.font='11px sans-serif';g.textAlign='right';g.fillText(max.toFixed(0),L-4,T+8);g.fillText('0',L-4,H-B);
 keys.forEach((k,i)=>{const v=days[k],bh=v/max*(H-T-B);g.fillStyle=v>0?'#c44':'#2a2a2a';g.fillRect(L+i*bw+1,H-B-bh,Math.max(1,bw-2),bh);
  if(keys.length<=31||i%7==0){g.fillStyle='#888';g.textAlign='center';g.fillText(new Date(k).getDate(),L+i*bw+bw/2,H-4);}});}
function stats(d){const s=d.stats,span=s.newest>s.oldest?((s.newest-s.oldest)/86400).toFixed(1)+' days':'-';
 document.getElementById('stats').textContent=`${s.records} of ${s.capacity} rows (${span}), every ${d.interval/60} min, bucket ${d.bucket/60} min; `+
 `${s.writes} writes, ${s.evictions} evictions; file ${(s.file_bytes/1024).toFixed(0)} KB, partition ${(s.fs_used/1024).toFixed(0)} / ${(s.fs_total/1024).toFixed(0)} KB used`;}
async function load(){document.querySelectorAll('#ranges button').forEach(b=>b.classList.toggle('on',+b.dataset.h===hours));
 const to=Math.floor(Date.now()/1000);try{const r=await fetch(`/history.json?from=${to-hours*3600}&to=${to}`);const d=await r.json();
  if(d.error){document.getElementById('stats').textContent='History unavailable: '+d.error;return;}chart(d);bars(d);stats(d);}
 catch(e){document.getElementById('stats').textContent='Could not load history: '+e;}}
document.querySelectorAll('#ranges button').forEach(b=>b.onclick=()=>{hours=+b.dataset.h;load();});
load();setInterval(load,60000);
</script></body></html>
)HTML";

}  // namespace smartslot
}  // namespace esphome
