#pragma once
// The built-in landing page: a template for the same engine as your own `html` (see
// ap_portal_core.h, "templates"), so everything it shows is a variable you can use too.
// Languages: en / uk / ru, chosen by the browser.

namespace esphome::ap_portal {

static const char BUILTIN_LANDING[] = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{{title}}</title><style>
:root{color-scheme:light dark}
body{margin:0;min-height:100vh;display:grid;place-items:center;font:16px/1.45 system-ui,-apple-system,Roboto,sans-serif;background:#f4f5f8;color:#1b2230}
@media(prefers-color-scheme:dark){body{background:#0b0d12;color:#f2f4f8}.c{background:rgba(255,255,255,.05)!important}}
.c{background:#fff;border-radius:24px;padding:28px;max-width:340px;margin:16px;text-align:center}
.l{width:48px;height:48px;border-radius:14px;background:{{accent_color}};color:{{accent_text}};display:inline-grid;place-items:center;margin-bottom:12px}
.l svg{width:26px;height:26px;fill:currentColor}
h1{font-size:20px;margin:0 0 8px}p{margin:0 0 20px;opacity:.7;font-size:14px}
a.b{display:block;background:{{accent_color}};color:{{accent_text}};text-decoration:none;font-weight:700;padding:14px;border-radius:14px;-webkit-tap-highlight-color:transparent}
a.s{display:block;color:inherit;text-decoration:none;border:1px solid rgba(127,127,127,.35);margin-top:10px;font-weight:600;padding:14px;border-radius:14px}
.o{margin-top:18px;font-size:13px;opacity:.75;text-align:left}.o b{display:block;margin-bottom:4px}.o div{margin:3px 0}
.o a{color:{{accent_color}};font-weight:600;text-decoration:none}.h{margin-top:12px;font-size:12px;opacity:.6}
</style></head><body><div class="c">
{{#if icon_svg}}<div class="l">{{{icon_svg}}}</div>{{/if}}
<h1>{{title}}</h1><p id="t">Connected to the device's own Wi-Fi.</p>
<a class="b" id="p" href="{{enter}}">{{primary_label}}</a>
{{#if secondary_href}}<a class="s" id="sc" href="{{secondary_href}}">{{secondary_label}}</a>{{/if}}
<div class="o"><b id="oa">All addresses</b>
<div><span id="o1">Device Wi-Fi</span>: <a href="http://{{ap_ip}}/">http://{{ap_ip}}/</a></div>
{{#if sta_ip}}<div><span id="o2">Home network</span>: <a href="http://{{sta_ip}}/">http://{{sta_ip}}/</a></div>{{/if}}
<div><span id="o3">Home network (mDNS)</span>: <a href="http://{{host}}.local/">http://{{host}}.local/</a></div>
</div><div class="h" id="h">If this page opened in a small window, open the address above in your browser.</div>
</div><script>
var L=(navigator.language||'en').slice(0,2),T={
uk:['Ви підключені до Wi-Fi пристрою.','Відкрити сторінку пристрою','Усі адреси','Wi-Fi пристрою','Домашня мережа','Домашня мережа (mDNS)','Якщо сторінка відкрилась у маленькому вікні, відкрийте адресу вище у браузері.','Wi-Fi роутера'],
ru:['Вы подключены к Wi-Fi устройства.','Открыть страницу устройства','Все адреса','Wi-Fi устройства','Домашняя сеть','Домашняя сеть (mDNS)','Если страница открылась в маленьком окне, откройте адрес выше в браузере.','Wi-Fi роутера']};
if(T[L]){var x=T[L],g=function(i){return document.getElementById(i)},s=function(i,v){var e=g(i);if(e)e.textContent=v};
s('t',x[0]);{{#if primary_default}}s('p',x[1]);{{/if}}s('oa',x[2]);s('o1',x[3]);s('o2',x[4]);s('o3',x[5]);s('h',x[6]);{{#if secondary_default}}s('sc',x[7]);{{/if}}}
</script></body></html>
)HTML";

// The built-in Wi-Fi page: the router network (scan list, connect) and the access point password.
// Served only to access point clients. Endpoints (ap_portal.cpp): GET /ap_portal/networks,
// POST /ap_portal/scan, /ap_portal/connect (ssid, psk), /ap_portal/ap_password (password),
// /ap_portal/ap_password_reset.
static const char BUILTIN_WIFI[] = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{{title}} · Wi-Fi</title><style>
:root{color-scheme:light dark}
body{margin:0;font:16px/1.45 system-ui,-apple-system,Roboto,sans-serif;background:#f4f5f8;color:#1b2230}
@media(prefers-color-scheme:dark){body{background:#0b0d12;color:#f2f4f8}.c{background:rgba(255,255,255,.05)!important}input{background:rgba(255,255,255,.06)!important;color:inherit}}
main{max-width:420px;margin:0 auto;padding:16px}
.c{background:#fff;border-radius:20px;padding:18px;margin:0 0 14px}
h1{font-size:20px;margin:6px 0 14px}h2{font-size:15px;margin:0 0 10px}
.m{font-size:13px;opacity:.7}.ok{color:#22a559}.er{color:#e5484d}
ul{list-style:none;margin:0;padding:0}li{display:flex;justify-content:space-between;align-items:center;padding:11px 4px;border-bottom:1px solid rgba(127,127,127,.18);cursor:pointer}
li:last-child{border:0}li.sel{color:{{accent_color}};font-weight:600}
input{width:100%;box-sizing:border-box;font:inherit;padding:12px;border-radius:12px;border:1px solid rgba(127,127,127,.35);margin:0 0 10px;background:#fff}
button{width:100%;font:inherit;font-weight:700;padding:13px;border-radius:12px;border:0;background:{{accent_color}};color:{{accent_text}};cursor:pointer}
button.s{background:transparent;color:inherit;border:1px solid rgba(127,127,127,.35);font-weight:600;margin-top:8px}
a{color:{{accent_color}};text-decoration:none;font-weight:600}
</style></head><body><main>
<a href="/" id="bk">← {{title}}</a><h1 id="h">Wi-Fi</h1>
<div class="c"><h2 id="hs">Router network</h2><div id="st" class="m">…</div></div>
<div class="c"><h2 id="hn">Networks</h2><ul id="ls"><li class="m" id="ld">Searching…</li></ul>
<button class="s" id="rf" onclick="scan()">Search again</button></div>
<div class="c"><h2 id="hc">Connect</h2>
<input id="ss" placeholder="Network name (SSID)" autocomplete="off" autocapitalize="none">
<input id="pw" type="password" placeholder="Password">
<button id="cn" onclick="conn()">Connect</button><div id="cm" class="m"></div></div>
{{#if ap_password_block}}<div class="c"><h2 id="ha">Device Wi-Fi password</h2>
<div class="m" id="as">{{#if custom_password}}Changed from this page{{else}}From the firmware{{/if}}</div><br>
<input id="ap" type="password" placeholder="New password, 8–63 characters">
<button id="ab" onclick="appw()">Save</button>
<button class="s" id="ar" onclick="aprs()">Back to the firmware password</button>
<div id="am" class="m"></div></div>{{/if}}
</main><script>
var L=(navigator.language||'en').slice(0,2),T={
uk:{h:'Wi-Fi',hs:'Мережа роутера',hn:'Мережі',ld:'Пошук…',rf:'Шукати ще раз',hc:'Підключення',ss:'Назва мережі (SSID)',pw:'Пароль',cn:'Підключити',ha:'Пароль Wi-Fi пристрою',ap:'Новий пароль, 8–63 символи',ab:'Зберегти',ar:'Повернути пароль з прошивки',
 con:'Підключено до ',none:'Не підключено до роутера',going:'Підключаюсь… Точка доступу може зникнути на кілька секунд — підключіться знову.',saved:'Збережено. Пристрій від\'єднає вас — підключіться з новим паролем.',bad:'8–63 символи або порожньо',empty:'Мереж не знайдено',name:'Вкажіть назву мережі'},
ru:{h:'Wi-Fi',hs:'Сеть роутера',hn:'Сети',ld:'Поиск…',rf:'Искать ещё раз',hc:'Подключение',ss:'Имя сети (SSID)',pw:'Пароль',cn:'Подключить',ha:'Пароль Wi-Fi устройства',ap:'Новый пароль, 8–63 символа',ab:'Сохранить',ar:'Вернуть пароль из прошивки',
 con:'Подключено к ',none:'Не подключено к роутеру',going:'Подключаюсь… Точка доступа может пропасть на несколько секунд — подключитесь снова.',saved:'Сохранено. Устройство отключит вас — подключитесь с новым паролем.',bad:'8–63 символа или пусто',empty:'Сетей не найдено',name:'Укажите имя сети'}},
E={con:'Connected to ',none:'Not connected to the router',going:'Connecting… The access point may drop for a few seconds — join it again.',saved:'Saved. The device will drop you — join with the new password.',bad:'8–63 characters or empty',empty:'No networks found',name:'Enter the network name'},
X=T[L]||{},t=function(k){return X[k]||E[k]},g=function(i){return document.getElementById(i)};
for(var k in X){var e=g(k);if(e){if(e.tagName=='INPUT')e.placeholder=X[k];else e.textContent=X[k]}}
function bars(r){return r>-55?'▂▄▆█':r>-67?'▂▄▆':r>-78?'▂▄':'▂'}
function post(u,d){var b=[];for(var k in d)b.push(k+'='+encodeURIComponent(d[k]));return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b.join('&')})}
function load(){fetch('/ap_portal/networks').then(function(r){return r.json()}).then(function(j){
 g('st').innerHTML='';var s=document.createElement('span');s.className=j.connected?'ok':'m';
 s.textContent=j.connected?t('con')+j.ssid+' · '+j.ip+' · '+j.rssi+' dB':t('none');g('st').appendChild(s);
 var ls=g('ls');ls.innerHTML='';if(!j.networks.length){var e=document.createElement('li');e.className='m';e.textContent=j.scanning?(X.ld||'Searching…'):t('empty');ls.appendChild(e)}
 j.networks.forEach(function(n){var li=document.createElement('li');var a=document.createElement('span');a.textContent=n.ssid;var b=document.createElement('span');b.className='m';b.textContent=(n.lock?'🔒 ':'')+bars(n.rssi);
  li.appendChild(a);li.appendChild(b);if(n.ssid==g('ss').value)li.className='sel';li.onclick=function(){g('ss').value=n.ssid;g('pw').focus();load()};ls.appendChild(li)});
 if(j.scanning)setTimeout(load,1500)}).catch(function(){setTimeout(load,3000)})}
function scan(){post('/ap_portal/scan',{}).then(function(){setTimeout(load,800)})}
function conn(){var s=g('ss').value;if(!s){g('cm').textContent=t('name');return}
 post('/ap_portal/connect',{ssid:s,psk:g('pw').value}).then(function(r){g('cm').textContent=r.ok?t('going'):'HTTP '+r.status})}
function appw(){var p=g('ap').value;if(p&&(p.length<8||p.length>63)){g('am').textContent=t('bad');return}
 post('/ap_portal/ap_password',{password:p}).then(function(r){g('am').textContent=r.ok?t('saved'):'HTTP '+r.status})}
function aprs(){post('/ap_portal/ap_password_reset',{}).then(function(r){g('am').textContent=r.ok?t('saved'):'HTTP '+r.status})}
load();scan();
</script></body></html>
)HTML";

}  // namespace esphome::ap_portal
