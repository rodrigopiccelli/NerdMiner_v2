#pragma once
#ifdef GEEKMAGIC_S3

#include <Arduino.h>

// Single page configuration UI served at http://<device-ip>/
static const char GM_WEB_PAGE[] PROGMEM = R"GMPAGE(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>NerdMiner - GeekMagic S3</title>
<style>
:root{--bg:#2b2d31;--card:#36393e;--line:#4a4d53;--fg:#e8e8e8;--mute:#a0a3a8;--acc:#f7931a;--ok:#3ba55d;--err:#ed4245;--btn:#4e7cf0}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{display:flex;gap:16px;align-items:center;flex-wrap:wrap;padding:16px;max-width:900px;margin:0 auto}
header h1{font-size:20px;margin:0}header h1 b{color:var(--acc)}
.scr{position:relative;width:252px;height:252px;flex:none}
#shot{width:252px;height:252px;border-radius:10px;border:6px solid #111;background:#000;image-rendering:pixelated;display:block}
#shotGif{position:absolute;left:166px;top:166px;width:80px;height:80px;display:none;image-rendering:pixelated}
.stat{color:var(--mute);font-size:13px}.stat b{color:var(--fg);font-weight:600}
nav{display:flex;gap:4px;flex-wrap:wrap;max-width:900px;margin:0 auto;padding:0 16px}
nav button{background:none;border:0;color:var(--mute);padding:10px 14px;font-size:15px;cursor:pointer;border-bottom:2px solid transparent}
nav button.on{color:var(--fg);border-color:var(--acc)}
main{max-width:900px;margin:0 auto;padding:16px}
section{display:none}section.on{display:block}
.card{background:var(--card);border-radius:8px;padding:16px;margin-bottom:16px}
.card h2{font-size:16px;margin:0 0 12px;border-left:3px solid var(--acc);padding-left:8px}
.row{display:flex;gap:12px;flex-wrap:wrap;align-items:center;margin:8px 0}
.row label{min-width:150px;color:var(--mute)}
input[type=text],input[type=number],input[type=password],select{background:#202225;color:var(--fg);border:1px solid var(--line);border-radius:5px;padding:7px 9px;font-size:14px;min-width:0}
input[type=text],input[type=password]{width:280px;max-width:100%}
input[type=number]{width:90px}input[type=range]{width:260px;max-width:100%}
input[type=color]{width:52px;height:32px;border:0;background:none;padding:0}
button.b{background:var(--btn);color:#fff;border:0;border-radius:5px;padding:8px 16px;font-size:14px;cursor:pointer}
button.b.red{background:var(--err)}button.b.gray{background:#5c6067}
.hint{color:var(--mute);font-size:13px}
.screens{display:flex;gap:8px;flex-wrap:wrap}
.screens button.cur{outline:2px solid var(--acc)}
table{width:100%;border-collapse:collapse}td,th{padding:6px;border-bottom:1px solid var(--line);text-align:left;font-size:14px}
#toast{position:fixed;left:50%;top:16px;transform:translateX(-50%);padding:10px 18px;border-radius:6px;color:#fff;opacity:0;transition:opacity .3s;pointer-events:none;z-index:9}
.gifs{display:flex;gap:10px;flex-wrap:wrap}
.gif{width:96px;padding:6px;border-radius:8px;background:#000;border:2px solid transparent;cursor:pointer;text-align:center;font-size:12px;color:var(--mute)}
.gif.cur{border-color:var(--acc);color:var(--fg)}.gif img{width:80px;height:80px;display:block;margin:0 auto 4px;image-rendering:pixelated}
.gif .coin{width:80px;height:80px;margin:0 auto 4px;border-radius:50%;background:#f7931a;color:#fff;font:bold 44px/80px sans-serif}
.bar{height:8px;background:#202225;border-radius:4px;overflow:hidden;width:260px}.bar div{height:100%;background:var(--acc);width:0}
</style></head><body>
<div id="toast"></div>
<header>
  <div class="scr"><img id="shot" alt="screen" src="/screenshot.bmp"><img id="shotGif" alt=""></div>
  <div>
    <h1><b>NerdMiner</b> &middot; GeekMagic S3</h1>
    <div class="stat">IP <b id="s_ip">-</b> &middot; WiFi <b id="s_ssid">-</b> (<b id="s_rssi">-</b> dBm)</div>
    <div class="stat">Hashrate <b id="s_khs">-</b> KH/s &middot; Shares <b id="s_sh">-</b> &middot; Uptime <b id="s_up">-</b></div>
    <div class="stat">Weather <b id="s_w">-</b></div>
    <div class="row"><label style="min-width:0"><input type="checkbox" id="live" checked> Live preview</label>
      <a id="dl" class="hint" href="/screenshot.bmp" download="nerdminer.bmp">Download screenshot</a></div>
  </div>
</header>
<nav id="tabs">
  <button data-t="screens" class="on">Screens</button><button data-t="weather">Weather</button><button data-t="time">Time</button>
  <button data-t="network">Network</button><button data-t="miner">Miner</button><button data-t="system">System</button>
</nav>
<main>
<section id="t_screens" class="on">
  <div class="card"><h2>Show screen</h2>
    <div class="screens" id="scrBtns"></div>
    <p class="hint">Touch button: tap = next screen, double tap = Web Config on/off (on at boot; when turned on by double tap it switches off after 15 min without access), triple tap = display on/off, hold 6&nbsp;s = reset miner config.</p></div>
  <div class="card"><h2>Auto switch</h2>
    <div id="scrMask"></div>
    <div class="row"><label>Enable</label><input type="checkbox" id="autoSwitch"></div>
    <div class="row"><label>Interval (seconds)</label><input type="number" id="switchSec" min="5" max="3600"></div>
    <button class="b" onclick="saveScreens()">Save</button></div>
  <div class="card"><h2>Display</h2>
    <div class="row"><label>Status</label><b id="dispState">-</b><button class="b" id="dispBtn" onclick="toggleDisplay()">-</button></div>
    <p class="hint">Same as a triple tap on the touch button. When the display is off, any tap turns it back on.</p></div>
  <div class="card"><h2>Brightness</h2>
    <div class="row"><label>Brightness</label><input type="range" id="brightness" min="0" max="100" oninput="bv.textContent=this.value" onchange="post('/api/cfg',{brightness:this.value})"><span id="bv"></span>%</div></div>
  <div class="card"><h2>Night mode</h2>
    <div class="row"><label>Enable</label><input type="checkbox" id="nightEn"></div>
    <div class="row"><label>Start</label><select id="nightStart"></select><label style="min-width:0">End</label><select id="nightEnd"></select></div>
    <div class="row"><label>Night brightness</label><input type="range" id="nightBrightness" min="0" max="100" oninput="nbv.textContent=this.value"><span id="nbv"></span>%</div>
    <button class="b" onclick="saveNight()">Save</button></div>
</section>

<section id="t_weather">
  <div class="card"><h2>Location</h2>
    <div class="row"><label>City</label><input type="text" id="city" placeholder="Santo Andre,BR"></div>
    <p class="hint">Use <i>City</i> or <i>City,CC</i> (ISO country code, e.g. <i>Santo Andre,BR</i>). A numeric OpenWeatherMap city id (e.g. <i>3449701</i>) also works, with or without an API key. If the OpenWeatherMap key is invalid or rate limited, Open-Meteo is used automatically.</p>
    <div class="row"><label>Status</label><span id="wStatus" class="hint">-</span></div>
    <button class="b" onclick="saveWeather()">Save</button> <button class="b gray" onclick="post('/api/weather/refresh',{})">Refresh now</button></div>
  <div class="card"><h2>Units &amp; language</h2>
    <div class="row"><label>Temperature</label><select id="tempUnit"><option value="0">&deg;C</option><option value="1">&deg;F</option></select></div>
    <div class="row"><label>Wind speed</label><select id="windUnit"><option value="0">km/h</option><option value="1">m/s</option><option value="2">mph</option><option value="3">knots</option></select></div>
    <div class="row"><label>Pressure</label><select id="pressUnit"><option value="0">hPa</option><option value="1">mmHg</option><option value="2">inHg</option></select></div>
    <div class="row"><label>Screen language</label><select id="lang"><option value="1">English</option><option value="0">Portugu&ecirc;s</option></select></div>
    <button class="b" onclick="saveUnits()">Save</button></div>
  <div class="card"><h2>Update interval &amp; API key</h2>
    <div class="row"><label>Interval (minutes)</label><input type="number" id="weatherMin" min="5" max="1440"></div>
    <div class="row"><label>OpenWeatherMap key</label><input type="text" id="owmKey" placeholder="optional"></div>
    <p class="hint">Without a key the free <a href="https://open-meteo.com" target="_blank" style="color:var(--acc)">Open-Meteo</a> service is used. Get an OpenWeatherMap key <a href="https://home.openweathermap.org/api_keys" target="_blank" style="color:var(--acc)">here</a> (new keys may take 1-2 h to activate).</p>
    <button class="b" onclick="saveApi()">Save</button></div>
  <div class="card"><h2>GIF on the weather screen</h2>
    <p class="hint">80x80 px animated GIF shown in the bottom-right corner. Pick a built-in one, upload your own, or show a Bitcoin coin with the current hashrate.</p>
    <div class="gifs" id="gifGallery"></div>
    <h2 style="margin-top:16px">Uploaded GIFs</h2>
    <div class="row"><input type="file" id="gifFile" accept="image/gif"><button class="b" onclick="uploadGif()">Upload</button></div>
    <div class="bar" id="gifBar" style="display:none"><div></div></div>
    <table><thead><tr><th></th><th>File</th><th>Size</th><th></th></tr></thead><tbody id="gifList"></tbody></table>
    <div class="row"><span class="hint" id="fsInfo"></span></div></div>
</section>

<section id="t_time">
  <div class="card"><h2>Time zone</h2>
    <div class="row"><label>Mode</label><select id="tzAuto"><option value="1">Automatic (from weather location)</option><option value="0">Manual</option></select></div>
    <div class="row"><label>UTC offset</label><select id="tzOffsetMin"></select></div>
    <div class="row"><label>NTP server</label><input type="text" id="ntp" placeholder="pool.ntp.org (default)"></div>
    <button class="b" onclick="saveTz()">Save</button></div>
  <div class="card"><h2>Clock style</h2>
    <div class="row"><label>Hours color</label><input type="color" id="hourColor"><label style="min-width:0">Minutes</label><input type="color" id="minColor"><label style="min-width:0">Seconds</label><input type="color" id="secColor"></div>
    <div class="row"><label>Font</label><select id="font"><option value="0">GeekMagic (default)</option><option value="1">Condensed</option><option value="2">Rounded</option><option value="3">Digital</option></select></div>
    <div class="row"><label>12-hour format</label><input type="checkbox" id="hour12"></div>
    <div class="row"><label>Blinking colon</label><input type="checkbox" id="colonBlink"></div>
    <div class="row"><label>Date format</label><select id="dateFmt"><option value="0">D/M/YYYY</option><option value="1">DD/MM/YYYY</option><option value="2">M/D/YYYY</option><option value="3">MM/DD/YYYY</option><option value="4">YYYY/M/D</option><option value="5">YYYY-MM-DD</option></select></div>
    <button class="b" onclick="saveClock()">Save</button></div>
</section>

<section id="t_network">
  <div class="card"><h2>WiFi</h2>
    <p class="hint">Connected to <b id="n_ssid">-</b>. 2.4 GHz networks only. The device restarts after saving; if it cannot connect it opens the <b>NerdMinerAP</b> setup portal again.</p>
    <div class="row"><button class="b gray" onclick="scan()">Scan</button><span id="scanMsg" class="hint"></span></div>
    <table><tbody id="nets"></tbody></table>
    <div class="row"><label>SSID</label><input type="text" id="wSsid"></div>
    <div class="row"><label>Password</label><input type="password" id="wPass"></div>
    <button class="b" onclick="saveWifi()">Save &amp; restart</button></div>
</section>

<section id="t_miner">
  <div class="card"><h2>Mining pool</h2>
    <div class="row"><label>Pool URL</label><input type="text" id="pool"></div>
    <div class="row"><label>Pool port</label><input type="number" id="port" min="1" max="65535"></div>
    <div class="row"><label>BTC address</label><input type="text" id="wallet"></div>
    <div class="row"><label>Pool password</label><input type="text" id="pass"></div>
    <div class="row"><label>Save stats to flash</label><input type="checkbox" id="saveStats"></div>
    <button class="b" onclick="saveMiner()">Save &amp; restart</button></div>
</section>

<section id="t_system">
  <div class="card"><h2>Device</h2><table><tbody id="sysInfo"></tbody></table></div>
  <div class="card"><h2>Firmware update</h2>
    <p class="hint">Upload <i>firmware.bin</i> built for the GeekMagic-S3 environment.</p>
    <div class="row"><input type="file" id="fwFile" accept=".bin"><button class="b" onclick="uploadFw()">Update</button></div>
    <div class="bar" id="fwBar" style="display:none"><div></div></div></div>
  <div class="card"><h2>Restart / reset</h2>
    <div class="row"><button class="b gray" onclick="if(confirm('Restart the device?'))post('/api/reboot',{})">Restart</button>
    <button class="b red" onclick="if(confirm('Erase ALL settings (WiFi, pool, wallet, screens) and restart?'))post('/api/reset',{})">Factory reset</button></div></div>
</section>
</main>
<script>
const SCREENS=['Weather Clock','Miner','Clock','Global stats','BTC price'];
const $=id=>document.getElementById(id);
let S=null;
function toast(m,ok=true){const t=$('toast');t.textContent=m;t.style.background=ok?'var(--ok)':'var(--err)';t.style.opacity=1;setTimeout(()=>t.style.opacity=0,1800)}
async function post(url,data){try{const r=await fetch(url,{method:'POST',body:new URLSearchParams(data)});const j=await r.json();if(j.ok){toast('Saved');setTimeout(load,600)}else toast(j.error||'Error',false);return j}catch(e){toast('Connection lost',false)}}
document.querySelectorAll('#tabs button').forEach(b=>b.onclick=()=>{document.querySelectorAll('#tabs button').forEach(x=>x.classList.toggle('on',x==b));document.querySelectorAll('section').forEach(s=>s.classList.toggle('on',s.id=='t_'+b.dataset.t));if(b.dataset.t=='weather')loadGifs()});
function opts(sel,items){$(sel).innerHTML=items.map(([v,t])=>`<option value="${v}">${t}</option>`).join('')}
opts('nightStart',[...Array(24).keys()].map(h=>[h,String(h).padStart(2,'0')+':00']));
opts('nightEnd',[...Array(24).keys()].map(h=>[h,String(h).padStart(2,'0')+':00']));
{let a=[];for(let m=-720;m<=840;m+=15){const s=m<0?'-':'+',x=Math.abs(m);a.push([m,'UTC'+s+String(Math.floor(x/60)).padStart(2,'0')+':'+String(x%60).padStart(2,'0')])}opts('tzOffsetMin',a)}
$('scrBtns').innerHTML=SCREENS.map((n,i)=>`<button class="b gray" id="sb${i}" onclick="post('/api/screen',{s:${i}})">${n}</button>`).join('');
$('scrMask').innerHTML=SCREENS.map((n,i)=>`<div class="row"><label>${n}</label><input type="checkbox" id="m${i}"></div>`).join('');
function fmtUp(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'d ':'')+h+'h '+m+'m'}
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
async function load(){
  try{S=await (await fetch('/api/state')).json()}catch(e){return}
  const c=S.cfg,y=S.sys,w=S.weather,m=S.miner;
  updateGifOverlay();
  $('s_ip').textContent=y.ip;$('s_ssid').textContent=y.ssid;$('s_rssi').textContent=y.rssi;$('s_khs').textContent=y.khs;$('s_sh').textContent=y.shares;$('s_up').textContent=fmtUp(y.uptime);
  $('s_w').textContent=w.valid?`${w.city} ${w.cc} ${w.temp.toFixed(1)}°C ${w.hum}% (${w.source}, ${Math.round(w.age/60)} min ago)`:w.status;
  $('wStatus').textContent=w.status;$('n_ssid').textContent=y.ssid;
  for(const k of ['city','owmKey','ntp','hourColor','minColor','secColor'])$(k).value=c[k];
  for(const k of ['tempUnit','windUnit','pressUnit','lang','weatherMin','switchSec','font','dateFmt','tzOffsetMin','nightStart','nightEnd','brightness','nightBrightness'])$(k).value=c[k];
  for(const k of ['autoSwitch','hour12','colonBlink','nightEn'])$(k).checked=c[k];
  $('tzAuto').value=c.tzAuto?1:0;
  $('dispState').textContent=y.displayOn?'On':'Off';$('dispState').style.color=y.displayOn?'var(--ok)':'var(--err)';$('dispBtn').textContent=y.displayOn?'Turn display off':'Turn display on';$('dispBtn').classList.toggle('red',!!y.displayOn);$('bv').textContent=c.brightness;$('nbv').textContent=c.nightBrightness;
  SCREENS.forEach((n,i)=>{$('m'+i).checked=!!(c.screenMask&(1<<i));$('sb'+i).classList.toggle('cur',i==y.screen)});
  $('pool').value=m.pool;$('port').value=m.port;$('wallet').value=m.wallet;$('pass').value=m.pass;$('saveStats').checked=m.saveStats;
  const off=y.utcOffset/3600;
  $('sysInfo').innerHTML=[['Firmware',y.version],['MAC',y.mac],['IP',y.ip],['Free heap',(y.heap/1024).toFixed(0)+' KB'],['Free PSRAM',(y.psram/1024).toFixed(0)+' KB'],['Clock',y.timeValid?new Date(y.time*1000).toISOString().replace('T',' ').substr(0,19)+' (UTC'+(off>=0?'+':'')+off+')':'not synced'],['Valid blocks',y.valids],['Touch raw / baseline',y.touchRaw+' / '+y.touchBase]].map(r=>`<tr><td>${r[0]}</td><td>${esc(r[1])}</td></tr>`).join('');
}
function b(id){return $(id).checked?1:0}
function saveScreens(){let mask=0;SCREENS.forEach((n,i)=>{if($('m'+i).checked)mask|=1<<i});if(!mask)return toast('Select at least one screen',false);if($('autoSwitch').checked&&(mask&(mask-1))==0)return toast('Select at least two screens for auto switch',false);post('/api/cfg',{screenMask:mask,autoSwitch:b('autoSwitch'),switchSec:$('switchSec').value})}
function toggleDisplay(){post('/api/display',{on:S&&S.sys.displayOn?0:1})}
function saveNight(){post('/api/cfg',{nightEn:b('nightEn'),nightStart:$('nightStart').value,nightEnd:$('nightEnd').value,nightBrightness:$('nightBrightness').value})}
function saveWeather(){post('/api/cfg',{city:$('city').value})}
function saveUnits(){post('/api/cfg',{tempUnit:$('tempUnit').value,windUnit:$('windUnit').value,pressUnit:$('pressUnit').value,lang:$('lang').value})}
function saveApi(){post('/api/cfg',{weatherMin:$('weatherMin').value,owmKey:$('owmKey').value.replace(/\s/g,'')})}
function saveTz(){post('/api/cfg',{tzAuto:$('tzAuto').value,tzOffsetMin:$('tzOffsetMin').value,ntp:$('ntp').value})}
function saveClock(){post('/api/cfg',{hourColor:$('hourColor').value,minColor:$('minColor').value,secColor:$('secColor').value,font:$('font').value,hour12:b('hour12'),colonBlink:b('colonBlink'),dateFmt:$('dateFmt').value})}
function saveMiner(){if(confirm('Save pool settings and restart?'))post('/api/miner',{pool:$('pool').value,port:$('port').value,wallet:$('wallet').value,pass:$('pass').value,saveStats:b('saveStats')})}
function saveWifi(){if(!$('wSsid').value)return toast('Enter the SSID',false);if(confirm('Connect to '+$('wSsid').value+' and restart?'))post('/api/wifi',{ssid:$('wSsid').value,pass:$('wPass').value})}
async function scan(){$('scanMsg').textContent='Scanning...';for(let i=0;i<20;i++){const j=await (await fetch('/api/scan')).json();if(!j.scanning){$('scanMsg').textContent=j.nets.length+' networks';$('nets').innerHTML=j.nets.sort((a,b)=>b.rssi-a.rssi).map(n=>`<tr><td>${esc(n.ssid)}</td><td>${n.rssi} dBm</td><td>ch ${n.ch}${n.open?' (open)':''}</td><td><button class="b gray" onclick="$('wSsid').value=this.dataset.s;$('wPass').focus()" data-s="${esc(n.ssid)}">Select</button></td></tr>`).join('');return}await new Promise(r=>setTimeout(r,1000))}$('scanMsg').textContent='Scan timeout'}
async function loadGifs(){try{const j=await (await fetch('/api/gifs')).json();
const tile=(v,label,img)=>`<div class="gif${v==j.selected?' cur':''}" onclick="selGif(this.dataset.f)" data-f="${esc(v)}">${img?`<img src="/api/gif/raw?f=${encodeURIComponent(v)}">`:'<div class="coin">&#8383;</div>'}<span>${esc(label)}</span></div>`;
$('gifGallery').innerHTML=j.builtin.map(n=>tile('*'+n,n,true)).join('')+tile('','Bitcoin coin',false);
$('gifList').innerHTML=j.files.map(f=>`<tr><td><img src="/api/gif/raw?f=${encodeURIComponent(f.name)}" width="40" height="40"></td><td>${esc(f.name)}${f.name==j.selected?' <b style="color:var(--acc)">(in use)</b>':''}</td><td>${(f.size/1024).toFixed(0)} KB</td><td><button class="b gray" onclick="selGif(this.dataset.f)" data-f="${esc(f.name)}">Use</button> <button class="b red" onclick="delGif(this.dataset.f)" data-f="${esc(f.name)}">Delete</button></td></tr>`).join('')||'<tr><td colspan=4 class="hint">No GIFs uploaded</td></tr>';$('fsInfo').textContent=`Storage: ${(j.used/1024).toFixed(0)} / ${(j.total/1024).toFixed(0)} KB`}catch(e){}}
function selGif(f){post('/api/cfg',{gif:f}).then(loadGifs)}
async function delGif(f){if(confirm('Delete '+f+'?')){await post('/api/gif/delete?f='+encodeURIComponent(f),{});loadGifs()}}
function upload(url,file,bar,done){const x=new XMLHttpRequest(),fd=new FormData();fd.append('file',file);$(bar).style.display='block';x.upload.onprogress=e=>{$(bar).firstChild.style.width=(e.loaded/e.total*100)+'%'};x.onload=()=>{$(bar).style.display='none';let j={};try{j=JSON.parse(x.responseText)}catch(e){}j.ok?done():toast(j.error||'Upload failed',false)};x.onerror=()=>toast('Upload failed',false);x.open('POST',url);x.send(fd)}
function uploadGif(){const f=$('gifFile').files[0];if(!f)return toast('Choose a GIF file',false);if(f.size>400*1024)return toast('GIF too large (max 400 KB)',false);upload('/api/gif/upload',f,'gifBar',()=>{toast('Uploaded');loadGifs()})}
function uploadFw(){const f=$('fwFile').files[0];if(!f)return toast('Choose firmware.bin',false);if(!confirm('Flash '+f.name+'?'))return;upload('/update',f,'fwBar',()=>{toast('Updated, restarting...');setTimeout(()=>location.reload(),15000)})}
// Live preview: fetch a new screenshot every second, swap it in only when fully loaded (no flicker).
// On the weather screen the GIF slot is overlaid with the real animated GIF so it plays smoothly in the browser.
let shotBusy=false;
function refreshShot(){if(shotBusy||!$('live').checked||document.hidden)return;shotBusy=true;const im=new Image();im.onload=()=>{$('shot').src=im.src;shotBusy=false};im.onerror=()=>{shotBusy=false};im.src='/screenshot.bmp?'+Date.now()}
setInterval(refreshShot,1000);
function updateGifOverlay(){const g=$('shotGif');if(!S||S.sys.screen!=0||!S.cfg.gif||!$('live').checked){g.style.display='none';return}const src='/api/gif/raw?f='+encodeURIComponent(S.cfg.gif);if(g.dataset.src!==src){g.dataset.src=src;g.src=src}g.style.display='block'}
$('live').onchange=updateGifOverlay;
setInterval(()=>{if(!document.hidden)load()},10000);
load();
</script></body></html>)GMPAGE";

#endif
