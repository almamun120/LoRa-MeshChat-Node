// The two pages served by the relay: the chat page ("/") and the setup page ("/setup").
#pragma once
#include <pgmspace.h>

static const char CHAT_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="bn"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MeshChat Relay</title>
<style>
:root{--bg:#f4f6fb;--card:#fff;--ink:#14213d;--mut:#6b7691;--pri:#2563eb;--mine:#dbeafe;--line:#e3e8f3;--ok:#16a34a;--bad:#dc2626}
@media(prefers-color-scheme:dark){:root{--bg:#0e1424;--card:#172036;--ink:#e8edf8;--mut:#93a0bd;--pri:#5b8def;--mine:#1e3a6e;--line:#26314d}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,"Noto Sans Bengali",sans-serif;display:flex;flex-direction:column;height:100dvh}
header{padding:10px 14px;background:var(--card);border-bottom:1px solid var(--line);display:flex;gap:10px;align-items:center;flex-wrap:wrap}
header b{font-size:17px}.chip{font-size:12px;padding:2px 8px;border-radius:99px;background:var(--bg);color:var(--mut);border:1px solid var(--line)}
header a{margin-left:auto;color:var(--pri);text-decoration:none;font-size:14px}
#wx{display:none;gap:8px;padding:8px 14px;overflow-x:auto;background:var(--card);border-bottom:1px solid var(--line)}
.w{flex:0 0 auto;min-width:132px;padding:6px 10px;border-radius:12px;background:var(--bg);border:1px solid var(--line)}
.w b{display:block;font-size:13px;white-space:nowrap}.w .v{font-size:18px;font-weight:600}.w small{display:block;color:var(--mut);font-size:11px}
#sos{display:none;background:var(--bad);color:#fff;padding:8px 14px;font-weight:600}
#tabs{display:flex;gap:8px;padding:8px 14px;background:var(--card);border-bottom:1px solid var(--line)}
#tabs select,#tabs input{flex:1;min-width:0;padding:8px;border-radius:10px;border:1px solid var(--line);background:var(--bg);color:var(--ink);font:inherit}
#log{flex:1;overflow:auto;padding:12px 14px;display:flex;flex-direction:column;gap:8px}
.m{max-width:86%;padding:8px 12px;border-radius:14px;background:var(--card);border:1px solid var(--line);align-self:flex-start;word-wrap:break-word;white-space:pre-wrap}
.m.out{align-self:flex-end;background:var(--mine)}.m.sys{align-self:center;background:transparent;border:1px dashed var(--bad);color:var(--bad);font-weight:600}
.m small{display:block;color:var(--mut);font-size:12px}.m .tag{font-size:11px;color:var(--pri)}
form{display:flex;gap:8px;padding:10px 14px;background:var(--card);border-top:1px solid var(--line)}
form textarea{flex:1;resize:none;height:44px;padding:10px;border-radius:12px;border:1px solid var(--line);background:var(--bg);color:var(--ink);font:inherit}
form button{padding:0 18px;border:0;border-radius:12px;background:var(--pri);color:#fff;font:600 16px system-ui}
#err{color:var(--bad);font-size:13px;padding:0 14px 6px;min-height:0}
</style></head><body>
<header><b>MeshChat Relay</b><span class="chip" id="id">…</span><span class="chip" id="st">…</span><a href="/setup">⚙ সেটআপ / Setup</a></header>
<div id="sos"></div>
<div id="wx"></div>
<div id="tabs"><input id="nick" placeholder="আপনার নাম / Your name" maxlength="24"><select id="to"><option value="pub">📢 সবার জন্য (Public board)</option></select></div>
<div id="log"></div><div id="err"></div>
<form id="f"><textarea id="t" placeholder="মেসেজ লিখুন… / Type a message…" maxlength="500"></textarea><button>পাঠান</button></form>
<script>
const $=s=>document.querySelector(s);let since=0,known='';
$('#nick').value=localStorage.getItem('mc_nick')||'';
$('#nick').oninput=e=>localStorage.setItem('mc_nick',e.target.value);
const st=['⏳','✓','✓✓','⚠'];
function add(m){const d=document.createElement('div');d.className='m '+(m.dir==1?'out':m.dir==2?'sys':'');
 const who=m.dir==1?'আমি':m.name;let tag=m.pub?'📢 Public':(m.dir==1?'→ '+m.toName:'🔒 Private');
 const s=document.createElement('small');s.textContent=who+' · '+tag+' · '+new Date(m.ts).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'})+(m.dir==1?' '+st[m.status]:'')+(m.via==2?' · LoRa':'');
 const p=document.createElement('div');p.textContent=m.text;d.append(s,p);m.el=d;return d}
let els={};
function ago(s){return s<90?'এইমাত্র':s<5400?Math.round(s/60)+' মি আগে':Math.round(s/3600)+' ঘণ্টা আগে'}
function drawWx(w){const box=$('#wx');if(!w||!w.stations.length){box.style.display='none';return}
 box.style.display='flex';box.textContent='';
 w.stations.forEach(s=>{const d=document.createElement('div');d.className='w';
  const n=document.createElement('b');n.textContent='🌤 '+s.name+(s.local?' (এই নোড)':'');
  const v=document.createElement('div');v.className='v';v.textContent=s.t.toFixed(1)+' °C · '+s.p.toFixed(1)+' hPa';
  const a=document.createElement('small');a.textContent=ago(s.age)+(s.via==2?' · LoRa':s.via==1?' · BLE':'');
  d.append(n,v,a);box.append(d)})}
async function poll(){try{const r=await fetch('/api/state?since='+since);const j=await r.json();
 $('#id').textContent=j.id;$('#st').textContent='BLE '+j.ble+' · LoRa '+(j.lora?'✓':'✗')+' · '+j.contacts.length+' জন';
 const sel=$('#to'),cur=sel.value,key=JSON.stringify(j.contacts);
 const wt=c=>c.t===undefined?'':' · '+c.t.toFixed(1)+'°C '+c.p.toFixed(1)+'hPa';
 if(key!==known){known=key;sel.innerHTML='<option value="pub">📢 সবার জন্য (Public board)</option>';j.contacts.forEach(c=>{const o=document.createElement('option');o.value=c.id;o.textContent='🔒 '+c.name+wt(c);sel.append(o)});sel.value=cur}
 const log=$('#log'),bottom=log.scrollTop+log.clientHeight>=log.scrollHeight-40;
 j.msgs.forEach(m=>{if(els[m.seq]){els[m.seq].querySelector('small').textContent=els[m.seq].querySelector('small').textContent.replace(/[⏳✓⚠]+$/,st[m.status])}else{const e=add(m);els[m.seq]=e;log.append(e)}since=Math.max(since,m.seq)});
 if(j.msgs.length&&bottom)log.scrollTop=log.scrollHeight;
 drawWx(j.wx);
 const a=j.alerts.filter(x=>x.active);$('#sos').style.display=a.length?'block':'none';$('#sos').textContent=a.map(x=>'🆘 SOS: '+x.name+(x.loc?' ('+x.loc+')':'')).join(' | ');
 }catch(e){}}
$('#f').onsubmit=async e=>{e.preventDefault();const t=$('#t').value.trim();if(!t)return;
 const b=new URLSearchParams({kind:$('#to').value==='pub'?'public':'private',to:$('#to').value,name:$('#nick').value.trim(),text:t});
 const r=await fetch('/api/send',{method:'POST',body:b});const j=await r.json();
 if(j.ok){$('#t').value='';$('#err').textContent=''}else $('#err').textContent=j.error;poll()};
$('#t').onkeydown=e=>{if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();$('#f').requestSubmit()}};
fetch('/api/time',{method:'POST',body:new URLSearchParams({t:Date.now()})});
poll();setInterval(poll,2000);
</script></body></html>)HTML";

static const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="bn"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Relay Setup</title>
<style>
:root{--bg:#f4f6fb;--card:#fff;--ink:#14213d;--mut:#6b7691;--pri:#2563eb;--line:#e3e8f3;--bad:#dc2626}
@media(prefers-color-scheme:dark){:root{--bg:#0e1424;--card:#172036;--ink:#e8edf8;--mut:#93a0bd;--pri:#5b8def;--line:#26314d}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,"Noto Sans Bengali",sans-serif;padding:14px;max-width:640px;margin:auto}
h1{font-size:20px}h2{font-size:15px;color:var(--mut);margin:22px 0 6px;text-transform:uppercase;letter-spacing:.04em}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:6px 14px}
label{display:block;padding:10px 0;border-bottom:1px solid var(--line)}label:last-child{border:0}
label span{display:block;font-size:13px;color:var(--mut);margin-bottom:4px}
input,select{width:100%;padding:9px;border-radius:10px;border:1px solid var(--line);background:var(--bg);color:var(--ink);font:inherit}
input[type=checkbox]{width:auto}
button{padding:11px 18px;border:0;border-radius:12px;background:var(--pri);color:#fff;font:600 15px system-ui;margin:6px 6px 0 0}button.red{background:var(--bad)}button.gray{background:var(--mut)}
a{color:var(--pri)}.info{font:13px/1.6 ui-monospace,monospace;color:var(--mut);padding:10px 0}#msg{margin-top:10px;font-weight:600}
</style></head><body>
<p><a href="/">← চ্যাটে ফিরুন / Back to chat</a></p>
<h1>⚙ রিলে সেটআপ / Relay setup</h1>
<div class="info" id="info"></div>
<form id="f">
<h2>চ্যাট পরিচয় / Chat identity</h2><div class="card">
<label><span>এই ডিভাইসের নাম (অন্যদের ফোনে দেখায়) / Device name</span><input name="name" maxlength="24"></label></div>
<h2>Wi-Fi (এই ডিভাইসের হটস্পট)</h2><div class="card">
<label><span>Wi-Fi নাম / SSID</span><input name="apSsid" maxlength="31"></label>
<label><span>Wi-Fi পাসওয়ার্ড (৮+ অক্ষর, ফাঁকা = খোলা নেটওয়ার্ক) / Password</span><input name="apPass" maxlength="63"></label>
<label><span>Wi-Fi চ্যানেল / Channel (1-11)</span><input name="wifiCh" type="number" min="1" max="11"></label>
<label><span>নতুন সেটআপ পাসওয়ার্ড (ফাঁকা = আগেরটাই থাকবে) / New admin password</span><input name="adminPass" type="password" maxlength="31" autocomplete="new-password"></label></div>
<h2>LoRa</h2><div class="card">
<label><span><input type="checkbox" name="loraOn"> LoRa রিলে চালু / LoRa forwarding on</span></label>
<label><span>ফ্রিকোয়েন্সি / Frequency (MHz, 410–525). সব রিলেতে একই হতে হবে। আপনার দেশের নিয়ম মেনে বেছে নিন।</span><input name="freq" type="number" step="0.001" min="410" max="525"></label>
<label><span>Spreading factor (7 দ্রুত/কম দূরত্ব … 12 ধীর/বেশি দূরত্ব)</span><input name="sf" type="number" min="7" max="12"></label>
<label><span>ব্যান্ডউইথ / Bandwidth (kHz)</span><select name="bw"><option>62.5</option><option>125</option><option>250</option><option>500</option></select></label>
<label><span>Coding rate (5 = 4/5 … 8 = 4/8)</span><input name="cr" type="number" min="5" max="8"></label>
<label><span>ট্রান্সমিট পাওয়ার / TX power (dBm, 2–17)</span><input name="power" type="number" min="2" max="17"></label>
<label><span>Sync word (hex, যেমন 12) — একই নেটওয়ার্কের সব রিলেতে এক</span><input name="sync" maxlength="2"></label>
<label><span>ডিউটি সাইকেল / Duty cycle (% সময় ট্রান্সমিট করতে পারবে)</span><input name="duty" type="number" min="1" max="100"></label>
<label><span>আইডেন্টিটি বীকন / Identity beacon (মিনিট). চ্যানেল ব্যস্ত থাকলে নিজে থেকে ধীর হয়।</span><input name="beaconMin" type="number" min="5" max="120"></label>
<label><span>LoRa-তে পাবলিক পোস্ট/পরিচয়ের সর্বোচ্চ হপ / Max LoRa hops (2–16). কম = চ্যানেল ফাঁকা থাকে, বেশি = দূরে যায়।</span><input name="loraTtl" type="number" min="2" max="16"></label></div>
<h2>আলো ও শব্দ / LED and speaker</h2><div class="card">
<label><span><input type="checkbox" name="alertsOn"> নতুন মেসেজ/SOS এলে LED ও শব্দ চালু / Alerts on</span></label>
<label><span><input type="checkbox" name="alertSound"> শব্দ চালু (বন্ধ = শুধু LED, রাতের জন্য) / Sound</span></label>
<label><span>LED পিন / LED GPIO (2 = ডেভকিটের নীল LED; ফাঁকা = নেই)</span><input name="ledPin" type="number" min="-1" max="39"></label>
<label><span>বাজার/স্পিকার পিন / Buzzer GPIO (17 ডিফল্ট; ফাঁকা = নেই)</span><input name="buzPin" type="number" min="-1" max="39"></label></div>
<h2>সেন্সর / BMP280</h2><div class="card">
<label><span>BMP280 I2C ঠিকানা (hex): 0 = নিজে খুঁজবে, নয়তো 76 বা 77 (রিবুট লাগবে)</span><input name="wxAddr" maxlength="2"></label></div>
<h2>ডিসপ্লে / LCD</h2><div class="card">
<label><span>I2C ঠিকানা / I2C address (hex, সাধারণত 27 বা 3F)</span><input name="lcdAddr" maxlength="2"></label></div>
<button>💾 সেভ করুন (রিবুট হবে)</button>
</form>
<h2>🌤 আবহাওয়া স্টেশন / Weather station</h2><div class="card" id="wxc">
<label><span><input type="checkbox" id="wxOn"> তাপমাত্রা (°C) ও চাপ (hPa) নিজের নামসহ ব্রডকাস্ট করুন / Broadcast</span></label>
<label><span>কত মিনিট পরপর পাঠাবে / Interval (1–1440 মিনিট)। ক্ষণিকের জন্য সেভ হয়, রিবুট লাগে না।</span><input id="wxMin" type="number" min="1" max="1440"></label>
<div><button type="button" id="wxSave">💾 সময় সেভ</button><button type="button" class="gray" id="wxNow">📡 এখনই পাঠান</button></div></div>
<h2>অন্যান্য / Other</h2>
<button class="gray" id="reboot">🔄 রিবুট</button>
<button class="red" id="newid">🆔 নতুন Node ID তৈরি</button>
<button class="red" id="factory">🧹 ফ্যাক্টরি রিসেট</button>
<div id="msg"></div>
<script>
const $=s=>document.querySelector(s),f=$('#f');
function say(t){$('#msg').textContent=t}
async function load(){const j=await (await fetch('/api/settings')).json();
 for(const k in j.s){const e=f.elements[k];if(!e)continue;if(e.type==='checkbox')e.checked=!!j.s[k];else if(k==='sync'||k==='lcdAddr'||k==='wxAddr')e.value=Number(j.s[k]).toString(16).toUpperCase();else e.value=j.s[k]}
 $('#wxOn').checked=!!j.s.wxOn;$('#wxMin').value=j.s.wxMin;
 $('#info').innerHTML=j.info.map(x=>x.replace(/</g,'&lt;')).join('<br>')}
f.onsubmit=async e=>{e.preventDefault();const b=new URLSearchParams();
 for(const el of f.elements){if(!el.name)continue;b.append(el.name,el.type==='checkbox'?(el.checked?'1':'0'):el.value)}
 const r=await fetch('/api/settings',{method:'POST',body:b});const j=await r.json();say(j.ok?'✅ সেভ হয়েছে, রিবুট হচ্ছে… ১০ সেকেন্ড পর আবার ঢুকুন।':'❌ '+j.error)};
async function act(path,ask){if(ask&&!confirm(ask))return;const j=await (await fetch(path,{method:'POST'})).json();say(j.ok?'✅ হয়েছে, রিবুট হচ্ছে…':'❌ '+j.error)}
$('#wxSave').onclick=async()=>{const b=new URLSearchParams({on:$('#wxOn').checked?'1':'0',min:$('#wxMin').value});
 const j=await (await fetch('/api/wx',{method:'POST',body:b})).json();say(j.ok?'✅ আবহাওয়ার সময় সেভ হয়েছে।':'❌ '+j.error)};
$('#wxNow').onclick=async()=>{const j=await (await fetch('/api/wx/send',{method:'POST'})).json();say(j.ok?'✅ কয়েক সেকেন্ডের মধ্যে পাঠানো হবে।':'❌ '+j.error)};
$('#reboot').onclick=()=>act('/api/reboot');
$('#newid').onclick=()=>act('/api/newid','নতুন Node ID হলে পুরোনো চ্যাট পরিচয় হারাবে। নিশ্চিত?');
$('#factory').onclick=()=>act('/api/factory','সব সেটিংস ও পরিচয় মুছে যাবে। নিশ্চিত?');
load();
</script></body></html>)HTML";
