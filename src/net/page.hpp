// The phone/browser viewer page (self-contained: no external resources).
#pragma once

namespace lk {

inline const char* kDeniedPage = R"LK(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Lookout</title>
<style>body{margin:0;min-height:100vh;display:grid;place-items:center;background:#111214;color:#ededed;
font:16px/1.5 system-ui,-apple-system,sans-serif;text-align:center;padding:24px;box-sizing:border-box}
p{color:#8a8f98;max-width:28em}</style></head><body><div><h1>Lookout</h1>
<p>This link is missing its access key or the key has changed. Scan the QR code on the Cardputer's screen
to open the camera.</p></div></body></html>)LK";

inline const char* kViewerPage = R"LK(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#111214"><meta name="referrer" content="no-referrer"><title>Lookout</title>
<style>
:root{--bg:#111214;--card:#1e2024;--line:#2c2f35;--text:#ededed;--dim:#8a8f98;--accent:#29c5f6;--live:#ff3d57}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,-apple-system,"Segoe UI",sans-serif}
header{display:flex;align-items:center;gap:8px;padding:10px 14px}
.dot{width:10px;height:10px;border-radius:50%;background:var(--dim);flex:none}
.live .dot{background:var(--live);animation:pulse 1.4s ease-in-out infinite}
@keyframes pulse{50%{opacity:.35}}
h1{font-size:17px;margin:0;font-weight:650}
#st{margin-left:auto;color:var(--dim);font-size:13px;text-align:right}
#stage{position:relative;background:#000;display:flex;align-items:center;justify-content:center;overflow:hidden;
aspect-ratio:16/9;max-height:78vh;width:100%}
#stage.max{position:fixed;inset:0;max-height:none;aspect-ratio:auto;z-index:5}
#v{width:100%;height:100%;object-fit:contain;transition:transform .2s}
#msg{position:absolute;color:var(--dim)}
#close{display:none;position:absolute;top:10px;right:10px;z-index:6}
#stage.max #close{display:block}
.bar{display:flex;flex-wrap:wrap;gap:8px;padding:12px 14px}
button,select{background:var(--card);color:var(--text);border:1px solid var(--line);border-radius:10px;
padding:10px 14px;font:inherit;min-height:44px;cursor:pointer}
button.on{border-color:var(--accent);color:var(--accent)}
button:disabled{opacity:.45;cursor:default}
footer{padding:0 14px 18px;color:var(--dim);font-size:12px}
</style></head><body>
<header id="hd"><span class="dot"></span><h1>Lookout</h1><span id="st">connecting…</span></header>
<div id="stage"><img id="v" alt="Live camera view"><div id="msg">Starting camera…</div>
<button id="close" aria-label="Exit full screen">✕</button></div>
<div class="bar">
<button id="listen">🔈 Listen</button>
<button id="snap">📷 Snapshot</button>
<button id="rot">⟳ Rotate</button>
<button id="full">⛶ Full screen</button>
<select id="q" aria-label="Video quality"><option value="0">Low 480p</option><option value="1">Medium 720p</option><option value="2">High 1080p</option></select>
<select id="g" aria-label="Microphone boost"><option value="0">Mic +12 dB</option><option value="1">Mic +24 dB</option><option value="2">Mic +36 dB</option></select>
</div>
<audio id="a" preload="none"></audio>
<footer>Live from your Cardputer Zero on this Wi-Fi network. Sound runs a second or two behind the picture.</footer>
<script>
const k=new URLSearchParams(location.search).get('k')||'';
const u=p=>p+(p.includes('?')?'&':'?')+'k='+encodeURIComponent(k);
const $=id=>document.getElementById(id);
const v=$('v'),msg=$('msg'),a=$('a'),stage=$('stage'),listen=$('listen');
let rot=0,listening=false,micOn=true;

function startVideo(){v.src=u('/video?t='+Date.now());}
v.addEventListener('load',()=>{msg.style.display='none'});
v.addEventListener('error',()=>{msg.style.display='';msg.textContent='Reconnecting…';setTimeout(startVideo,2000)});
startVideo();

function setListening(on){
  listening=on;listen.classList.toggle('on',on);listen.textContent=on?'🔊 Listening':'🔈 Listen';
  if(on){a.src=u('/audio?t='+Date.now());a.play().catch(()=>setListening(false));}
  else{a.pause();a.removeAttribute('src');a.load();}
}
listen.onclick=()=>setListening(!listening);
a.addEventListener('error',()=>{if(listening)setTimeout(()=>{if(listening)setListening(true)},2000)});

$('snap').onclick=async()=>{
  const r=await fetch(u('/snapshot.jpg'));if(!r.ok)return;
  const b=await r.blob(),l=document.createElement('a');
  l.href=URL.createObjectURL(b);l.download='lookout-'+new Date().toISOString().slice(0,19).replace(/[:T]/g,'-')+'.jpg';
  document.body.appendChild(l);l.click();l.remove();setTimeout(()=>URL.revokeObjectURL(l.href),5000);
};
$('rot').onclick=()=>{
  rot=(rot+90)%360;
  const sideways=rot%180!==0,s=sideways?Math.min(stage.clientHeight/stage.clientWidth,1):1;
  v.style.transform='rotate('+rot+'deg) scale('+s+')';
};
$('full').onclick=()=>{
  const fs=stage.requestFullscreen||stage.webkitRequestFullscreen;
  if(fs)fs.call(stage).catch(()=>stage.classList.add('max'));else stage.classList.add('max');
};
$('close').onclick=()=>stage.classList.remove('max');
$('q').onchange=e=>fetch(u('/set?quality='+e.target.value));
$('g').onchange=e=>fetch(u('/set?gain='+e.target.value));

async function poll(){
  try{
    const s=await (await fetch(u('/status'))).json();
    $('hd').classList.add('live');
    const parts=[s.res+' · '+s.fps.toFixed(0)+' fps'];
    if(s.battery>=0)parts.push('🔋'+s.battery+'%');
    parts.push(s.viewers+' watching');
    $('st').textContent=parts.join(' · ');
    if(document.activeElement!==$('q'))$('q').value=s.quality;
    if(document.activeElement!==$('g'))$('g').value=s.gain;
    micOn=s.mic;listen.disabled=!micOn;listen.title=micOn?'':'Microphone is turned off on the Cardputer';
    if(!micOn&&listening)setListening(false);
  }catch(e){$('hd').classList.remove('live');$('st').textContent='offline';}
}
poll();setInterval(poll,2000);
</script></body></html>)LK";

}  // namespace lk
