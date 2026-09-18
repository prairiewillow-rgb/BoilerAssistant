#ifndef DASHBOARD_HTML_H
#define DASHBOARD_HTML_H

#include <Arduino.h>

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Boiler Assistant</title>
<style>
:root{color-scheme:dark;--bg:#151515;--panel:#222;--card:#2b2b2b;--line:#444;--text:#f4f4f4;--muted:#aaa;--hot:#ff8b2b;--danger:#ef5350;--ok:#65c466}
*{box-sizing:border-box}body{margin:0;padding:16px;background:var(--bg);color:var(--text);font:16px system-ui,sans-serif}.wrap{max-width:960px;margin:auto}
header{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:16px;padding:16px;background:var(--panel);border:1px solid var(--line);border-radius:8px}h1{margin:0;color:var(--hot);font-size:1.35rem}.status{color:var(--muted);font-size:.9rem}
nav{display:flex;gap:8px;margin-bottom:12px}button{border:1px solid var(--line);border-radius:6px;padding:10px 13px;background:#303030;color:var(--text);cursor:pointer}button.active,button.primary{background:var(--hot);color:#111;border-color:var(--hot)}button.danger{background:var(--danger);border-color:var(--danger);color:#111}
section{display:none}section.active{display:block}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}.card{padding:14px;background:var(--card);border:1px solid var(--line);border-radius:8px}.label{color:var(--muted);font-size:.85rem;margin-bottom:6px}.value{font-size:1.45rem;font-weight:600}.alert{margin-bottom:12px;padding:12px;border-radius:6px;background:var(--danger);color:#111;display:none}.alert.show{display:flex;justify-content:space-between;align-items:center;gap:12px}.alert button{background:#111;color:var(--text);border-color:#111}.form{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:12px}.field label{display:block;color:var(--muted);font-size:.85rem;margin-bottom:5px}.field input,.field select{width:100%;padding:9px;background:#111;color:var(--text);border:1px solid var(--line);border-radius:5px}.actions{display:flex;gap:8px;align-items:center;margin-top:14px}.hint{color:var(--muted);font-size:.85rem}
</style></head><body><main class="wrap">
<header><h1 id="siteName">Boiler Assistant</h1><div class="status" id="connection">Connecting...</div></header>
<div id="alert" class="alert"><span id="alertText"></span><button id="alertReset">Reset alarm</button></div>
<nav><button class="tab active" data-target="state">Main</button><button class="tab" data-target="settings">Settings</button><button class="tab" data-target="sensors">Sensors</button></nav>
<section id="state" class="active"><div class="grid">
<div class="card"><div class="label">Burn state</div><div class="value" id="burnState">---</div></div>
<div class="card"><div class="label">Exhaust temperature</div><div class="value" id="exhaust">---</div></div>
<div class="card"><div class="label">Current fan</div><div class="value" id="fan">---</div></div>
<div class="card"><div class="label">Tank temperature</div><div class="value" id="tank">---</div></div>
<div class="card"><div class="label">Adaptive slope</div><div class="value" id="adaptiveSlope">---</div></div>
<div class="card"><div class="label">Tank high / low</div><div class="value" id="tankLimits">---</div></div>
<div class="card"><div class="label">Safety</div><div class="value" id="safety">---</div></div>
</div></section>
<section id="settings"><div class="card"><div class="form">
<div class="field"><label>Exhaust setpoint (F)</label><input id="exhaust_setpoint" type="number" min="200" max="900"></div>
<div class="field"><label>Deadband (F)</label><input id="deadband" type="number" min="1" max="100"></div>
<div class="field"><label>Boost time (seconds)</label><input id="boost_time" type="number" min="5" max="600"></div>
<div class="field"><label>Fan minimum (%)</label><input id="clamp_min" type="number" min="0" max="100"></div>
<div class="field"><label>Fan maximum (%)</label><input id="clamp_max" type="number" min="0" max="100"></div>
<div class="field"><label>Fan mode</label><select id="deadzone_fan"><option value="0">Fan allowed off</option><option value="1">Fan always on</option></select></div>
<div class="field"><label>Tank low (F)</label><input id="tank_low" type="number" min="40" max="189"></div>
<div class="field"><label>Tank high (F)</label><input id="tank_high" type="number" min="40" max="189"></div>
<div class="field"><label>Guardian minutes</label><input id="ember_minutes" type="number" min="1" max="120"></div>
<div class="field"><label>Flue low (F)</label><input id="flue_low" type="number" min="50" max="500"></div>
<div class="field"><label>Flue recovery (F)</label><input id="flue_recovery" type="number" min="50" max="500"></div>
 </div><div class="actions"><button class="primary" id="save">Save settings</button><button id="resetAlarm">Reset active alarm</button><button id="token">Set control password</button><span class="hint" id="message"></span></div></div></section>
<section id="sensors"><div class="grid"><div class="card"><div class="label">Outdoor temperature</div><div class="value" id="outdoor">---</div></div><div class="card"><div class="label">Humidity</div><div class="value" id="humidity">---</div></div><div class="card"><div class="label">Pressure</div><div class="value" id="pressure">---</div></div><div class="card"><div class="label">Exhaust sensor</div><div class="value" id="exhaustOk">---</div></div><div class="card"><div class="label">Tank sensor</div><div class="value" id="tankOk">---</div></div><div class="card"><div class="label">WiFi</div><div class="value" id="wifi">---</div></div></div><h2>Water probes</h2><div class="grid" id="waterProbes"></div></section>
</main><script>
const ids=['exhaust_setpoint','deadband','boost_time','clamp_min','clamp_max','deadzone_fan','tank_low','tank_high','ember_minutes','flue_low','flue_recovery'];
const tokenKey='boiler-control-token';
function token(){return localStorage.getItem(tokenKey)||''}
function set(id,v){const e=document.getElementById(id);if(e&&v!==undefined)e.value=v}
function text(id,v){const e=document.getElementById(id);if(e)e.textContent=v}
function renderProbes(probes){const root=document.getElementById('waterProbes');if(!root)return;root.innerHTML='';const names=['MAIN TANK','L1 SUPPLY','L1 RETURN','L2 SUPPLY','L2 RETURN','TANK BOTTOM','TANK TOP','EXTRA'];(probes||[]).forEach(p=>{const card=document.createElement('div');card.className='card';const label=document.createElement('div');label.className='label';label.textContent=p.name+(p.controls_tank?' (controls tank)':' (monitor only)');const value=document.createElement('div');value.className='value';value.textContent=p.temp_f===null?'ERR':Math.round(p.temp_f)+' F';const select=document.createElement('select');names.forEach(name=>{const option=document.createElement('option');option.value=name;option.textContent=name;if(name===p.name)option.selected=true;select.appendChild(option)});const button=document.createElement('button');button.textContent='Save name';button.onclick=()=>saveProbe(p.index,select.value);card.append(label,value,select,button);root.appendChild(card)})}
async function saveProbe(index,name){const r=await fetch('/api/probe',{method:'POST',headers:{'Content-Type':'application/json','X-Boiler-Token':token()},body:JSON.stringify({index:index,name:name})});if(!r.ok)showAlert('Probe name was not saved')}
async function resetAlarm(){const r=await fetch('/api/reset',{method:'POST',headers:{'X-Boiler-Token':token()}});const j=await r.json();showAlert(j.ok?'Alarm reset requested':(j.error||'Alarm reset rejected'));state()}
function showAlert(s){const e=document.getElementById('alert');const t=document.getElementById('alertText');if(t)t.textContent=s||'';e.classList.toggle('show',!!s)}
async function state(){try{const r=await fetch('/api/state',{cache:'no-store'});if(!r.ok)throw Error();const s=await r.json();
 text('connection','Online');text('siteName','Boiler Assistant');text('burnState',s.burn_state_text||s.burn_state);text('exhaust',s.exhaust_smooth===null?'ERR':Math.round(s.exhaust_smooth)+' F');text('fan',(s.fan_final??s.fan??0)+'%');text('adaptiveSlope',Number(s.adaptive_slope??1).toFixed(3));text('tank',s.tank_temp===null?'ERR':Math.round(s.tank_temp)+' F');text('tankLimits',(s.tank_high??'---')+' / '+(s.tank_low??'---')+' F');text('safety',s.safety_text||s.safety_state);text('outdoor',s.env?.temp_f===null?'ERR':Math.round(s.env?.temp_f)+' F');text('humidity',s.env?.humidity===null?'ERR':Math.round(s.env?.humidity)+'%');text('pressure',s.env?.pressure===null?'ERR':Number(s.env?.pressure).toFixed(1)+' hPa');text('exhaustOk',s.exhaust_sensor_ok?'OK':'FAULT');text('tankOk',s.tank_sensor_ok?'OK':'FAULT');text('wifi',s.wifi_ok?'OK':'OFF');renderProbes(s.water);showAlert(s.alert||'');
 }catch(e){text('connection','Offline');showAlert('Controller unavailable')}}
async function settings(){try{const r=await fetch('/api/settings',{cache:'no-store'});const s=await r.json();ids.forEach(k=>set(k,s[k]))}catch(e){}}
async function save(){const body={};ids.forEach(k=>{body[k]=Number(document.getElementById(k).value)});const r=await fetch('/api/set',{method:'POST',headers:{'Content-Type':'application/json','X-Boiler-Token':token()},body:JSON.stringify(body)});const j=await r.json();document.getElementById('message').textContent=j.ok?'Saved':(j.error||'Rejected');await settings()}
document.querySelectorAll('.tab').forEach(b=>b.onclick=()=>{document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));document.querySelectorAll('section').forEach(x=>x.classList.remove('active'));b.classList.add('active');document.getElementById(b.dataset.target).classList.add('active')});
document.getElementById('save').onclick=save;document.getElementById('resetAlarm').onclick=resetAlarm;document.getElementById('alertReset').onclick=resetAlarm;document.getElementById('token').onclick=()=>{const v=prompt('Enter the Control/API Password saved during WiFi setup');if(v!==null)localStorage.setItem(tokenKey,v)};settings();state();setInterval(state,1000);setInterval(settings,10000);
</script></body></html>
)rawliteral";

#endif
