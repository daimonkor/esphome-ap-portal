#pragma once
// The built-in landing page: a template for the same engine as your own `html` (see
// ap_portal_core.h, "templates"), so everything it shows is a variable you can use too.
// Languages: en / uk / ru, chosen by the browser.

namespace esphome::ap_portal {

static const char BUILTIN_LANDING[] = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{{title}}</title><style>
*{-webkit-tap-highlight-color:transparent}:focus:not(:focus-visible){outline:none}button,a,li{transition:opacity .15s}button:active:not(:disabled),a:active,li:active{opacity:.7}
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
.o a{color:{{accent_color}};font-weight:600;text-decoration:none}.o a.go{opacity:.6;margin-left:2px}.h{margin-top:12px;font-size:12px;opacity:.6}
</style></head><body><div class="c">
{{#if icon_svg}}<div class="l">{{{icon_svg}}}</div>{{/if}}
<h1>{{title}}</h1><p id="t">Connected to the device's own Wi-Fi.</p>
<a class="b" id="p" href="{{enter}}">{{primary_label}}</a>
<div class="h" id="ib" style="display:none">For the full version open http://{{ap_ip}}/ in the browser (Safari).</div>
<a class="s" id="br" style="display:none" href="{{enter}}">Open in the browser ↗</a>
<div class="h" id="bh" style="display:none">The sign-in window closes and the "Sign in" prompt won't come back. Return at http://{{ap_ip}}/ — or type any http:// address while on this Wi-Fi.</div>
{{#if secondary_href}}<a class="s" id="sc" href="{{secondary_href}}">{{secondary_label}}</a>{{/if}}
<div class="o"><b id="oa">All addresses</b>
<div><span id="o1">Device Wi-Fi</span>: <a class="cp" href="http://{{ap_ip}}/" data-copy="http://{{ap_ip}}/">http://{{ap_ip}}/</a> <a class="go" href="http://{{ap_ip}}/" target="_blank" rel="noopener">↗</a></div>
{{#if sta_ip}}<div><span id="o2">Home network</span>: <a class="cp" href="http://{{sta_ip}}/" data-copy="http://{{sta_ip}}/">http://{{sta_ip}}/</a> <a class="go" href="http://{{sta_ip}}/" target="_blank" rel="noopener">↗</a></div>{{/if}}
<div><span id="o3">Home network (mDNS)</span>: <a class="cp" href="http://{{host}}.local/" data-copy="http://{{host}}.local/">http://{{host}}.local/</a> <a class="go" href="http://{{host}}.local/" target="_blank" rel="noopener">↗</a></div>
</div><div class="h" id="h">Tap an address to copy it, then paste it into your browser.</div>
</div><script>
var L=(navigator.language||'en').slice(0,2),T={
uk:['Ви підключені до Wi-Fi пристрою.','Відкрити сторінку пристрою','Усі адреси','Wi-Fi пристрою','Домашня мережа','Домашня мережа (mDNS)','Натисніть адресу, щоб скопіювати, і вставте її в браузер.','Wi-Fi роутера','Скопійовано','Повна версія — у браузері (Safari): http://{{ap_ip}}/','Адресу скопійовано:','Відкрити в браузері ↗','Вікно входу закриється, і плашка «Увійти в мережу» більше не з’явиться. Повертайтеся за адресою http://{{ap_ip}}/ — або наберіть будь-яку адресу з http://, поки ви в цій мережі.'],
ru:['Вы подключены к Wi-Fi устройства.','Открыть страницу устройства','Все адреса','Wi-Fi устройства','Домашняя сеть','Домашняя сеть (mDNS)','Нажмите адрес, чтобы скопировать, и вставьте его в браузер.','Wi-Fi роутера','Скопировано','Полная версия — в браузере (Safari): http://{{ap_ip}}/','Адрес скопирован:','Открыть в браузере ↗','Окно входа закроется, и плашка «Войти в сеть» больше не появится. Возвращайтесь по адресу http://{{ap_ip}}/ — или наберите любой адрес с http://, пока вы в этой сети.']};
if(T[L]){var x=T[L],g=function(i){return document.getElementById(i)},s=function(i,v){var e=g(i);if(e)e.textContent=v};
s('t',x[0]);{{#if primary_default}}s('p',x[1]);{{/if}}s('oa',x[2]);s('o1',x[3]);s('o2',x[4]);s('o3',x[5]);s('h',x[6]);{{#if secondary_default}}s('sc',x[7]);{{/if}}s('ib',x[9]);s('br',x[11]);s('bh',x[12]);}
function copyText(t){try{if(navigator.clipboard&&window.isSecureContext){navigator.clipboard.writeText(t);return}}catch(e){}var a=document.createElement('textarea');a.value=t;a.setAttribute('readonly','');a.style.cssText='position:fixed;top:0;left:0;opacity:0';document.body.appendChild(a);a.select();a.setSelectionRange(0,t.length);try{document.execCommand('copy')}catch(e){}document.body.removeChild(a)}
function onCopy(root,label){root.addEventListener('click',function(e){var a=e.target.closest&&e.target.closest('a.cp');if(!a)return;e.preventDefault();copyText(a.getAttribute('data-copy'));var o=a.textContent;a.textContent='\u2713 '+label;setTimeout(function(){a.textContent=o},1800)})}
onCopy(document,(T[L]||[])[8]||'Copied');
// The sign-in window is a cut-down browser; the main button opens the page right there (a tap on an
// address below copies it). Android's (a WebView) also gets "Open in the browser ↗" — an
// intent:// link to the phone's browser; after it the sign-in prompt does not come back, which the
// note under it says. Apple's (iPhone, iPad, Mac) cannot open Safari on a network without internet.
(function(){var ua=navigator.userAgent,p=document.getElementById('p'),ib=document.getElementById('ib'),
 br=document.getElementById('br'),bh=document.getElementById('bh'),x=T[L]||[];
 var droid=/Android/i.test(ua)&&/; wv\)/.test(ua),apple=/iPhone|iPad|iPod|Macintosh/.test(ua)&&!/Safari\/|Chrome|Firefox|Edg/.test(ua);
 if(!p||!(droid||apple))return;
 var abs=function(){try{return new URL(p.getAttribute('href'),location.href)}catch(e){return null}};
 if(apple&&ib)ib.style.display='block';
 if(!droid||!br)return;
 br.style.display='block';if(bh)bh.style.display='block';
 br.onclick=function(e){var u=abs();if(!u||!/^https?:$/.test(u.protocol))return;e.preventDefault();
  location.href='intent://'+u.host+u.pathname+u.search+'#Intent;scheme='+u.protocol.slice(0,-1)+
   ';action=android.intent.action.VIEW;category=android.intent.category.BROWSABLE;S.browser_fallback_url='+encodeURIComponent(u.href)+';end'}})();
{{#if secondary_default}}var sc=document.getElementById('sc');if(sc)sc.onclick=function(e){if(window.apPortalPopup){e.preventDefault();window.apPortalPopup.open()}};{{/if}}
</script>{{#if secondary_default}}<script src="/ap_portal/popup.js" data-accent="{{accent_color}}" data-button="none"></script>{{/if}}</body></html>
)HTML";

// The built-in Wi-Fi page: the router network (scan list, connect) and the access point password.
// Served only to access point clients. Endpoints (ap_portal.cpp): GET /ap_portal/networks,
// POST /ap_portal/scan, /ap_portal/connect (ssid, psk), /ap_portal/ap_password (password),
// /ap_portal/ap_password_reset.
static const char BUILTIN_WIFI[] = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{{title}} · Wi-Fi</title><style>
*{-webkit-tap-highlight-color:transparent}:focus:not(:focus-visible){outline:none}button,a,li{transition:opacity .15s}button:active:not(:disabled),a:active,li:active{opacity:.7}
:root{color-scheme:light dark}
body{margin:0;font:16px/1.45 system-ui,-apple-system,Roboto,sans-serif;background:#f4f5f8;color:#1b2230}
@media(prefers-color-scheme:dark){body{background:#0b0d12;color:#f2f4f8}.c{background:rgba(255,255,255,.05)!important}input{background:rgba(255,255,255,.06)!important;color:inherit}}
main{max-width:420px;margin:0 auto;padding:16px}
.c{background:#fff;border-radius:20px;padding:18px;margin:0 0 14px}
h1{font-size:20px;margin:6px 0 14px}h2{font-size:15px;margin:0 0 10px}
.m{font-size:13px;opacity:.7}.ok{color:#22a559}.er{color:#e5484d}
ul{list-style:none;margin:0;padding:0}li{display:flex;justify-content:space-between;align-items:center;padding:11px 4px;border-bottom:1px solid rgba(127,127,127,.18);cursor:pointer}
li:last-child{border:0}li.sel{color:{{accent_color}};font-weight:600}.lk{font-size:12px;margin-left:6px;opacity:.8}li.cur,li.cur:active{cursor:default;opacity:.55}.cu{font-size:12px;margin-left:8px;font-weight:600}
input{width:100%;box-sizing:border-box;font:inherit;padding:12px;border-radius:12px;border:1px solid rgba(127,127,127,.35);margin:0 0 10px;background:#fff}
button{width:100%;font:inherit;font-weight:700;padding:13px;border-radius:12px;border:0;background:{{accent_color}};color:{{accent_text}};cursor:pointer}
button.s{background:transparent;color:inherit;border:1px solid rgba(127,127,127,.35);font-weight:600;margin-top:8px}
a{color:{{accent_color}};text-decoration:none;font-weight:600}
.tt{display:none;position:fixed;top:0;right:0;bottom:0;left:0;z-index:50;align-items:center;justify-content:center;padding:16px;background:rgba(11,13,18,.82);-webkit-backdrop-filter:blur(4px);backdrop-filter:blur(4px)}
.tc{width:100%;max-width:380px;background:#1b2230;color:#f2f4f8;border-radius:20px;padding:24px 20px;text-align:center;font-size:15px;box-shadow:0 12px 40px rgba(0,0,0,.45)}
.tc b{display:block;font-size:17px;margin-bottom:6px}.tc div{margin-top:6px}.tc .m{opacity:.75}.tc a{color:#9fc0ff}.tc .er{color:#ff8a8f}
.tc a.tb{display:block;text-align:center;margin-top:14px;background:{{accent_color}};color:{{accent_text}};padding:12px;border-radius:12px;font-weight:700}
.tc button.cl{margin-top:10px;background:transparent;color:inherit;border:1px solid rgba(255,255,255,.25)}
.sp.big{width:44px;height:44px;border-width:4px;margin:0 auto 16px;display:block;color:{{accent_color}}}
main{padding-bottom:140px}input.bad,input.bad:focus{border-color:#e5484d!important;box-shadow:0 0 0 1px #e5484d;outline:2px solid #e5484d}.err{color:#e5484d}#lt>div{margin-top:12px;padding:12px;border-radius:12px;background:rgba(127,127,127,.12)}#lt .row{display:flex;gap:8px}#lt .row button{margin-top:8px}
.sp{display:inline-block;width:14px;height:14px;border:2px solid currentColor;border-right-color:transparent;border-radius:50%;animation:spin .8s linear infinite;vertical-align:-2px;margin-right:8px}
@keyframes spin{to{transform:rotate(360deg)}}button:disabled{opacity:.7;cursor:default}ul.off li{opacity:.45;pointer-events:none}input:disabled{opacity:.6}
.pw{position:relative}.pw input{padding-right:48px}button.ey{position:absolute;right:6px;top:5px;width:38px;height:38px;padding:0;margin:0;border:0;background:none;color:inherit;opacity:.6;cursor:pointer;display:grid;place-items:center}button.ey svg{width:20px;height:20px;fill:currentColor}
</style></head><body><main>
<a href="/" id="bk">← {{title}}</a><h1 id="h">Wi-Fi</h1>
<div class="c"><h2 id="hs">Router network</h2><div id="st" class="m">…</div></div>
<div class="c"><h2 id="hn">Networks</h2><ul id="ls"><li class="m" id="ld">Searching…</li></ul>
<button class="s" id="rf" onclick="scan()">Search again</button></div>
<div class="c"><h2 id="hc">Connect</h2>
<input id="ss" placeholder="Network name (SSID)" autocomplete="off" autocapitalize="none">
<div class="pw"><input id="pw" type="password" placeholder="Password" autocomplete="off"><button type="button" class="ey" aria-label="Show password"></button></div>
<button id="cn" onclick="conn()">Connect now</button><button class="s" id="cl2" onclick="connLater()" style="display:none">Save, connect later</button><div id="bz" class="m"></div><div id="cm" class="m"></div><div id="lt"></div></div>
{{#if ap_password_block}}<div class="c"><h2 id="ha">Device Wi-Fi password</h2>
<div class="m" id="as">{{#if custom_password}}Changed from this page{{else}}From the firmware{{/if}}</div><br>
<div class="pw"><input id="ap" type="password" placeholder="New password, 8–63 characters" autocomplete="new-password"><button type="button" class="ey" aria-label="Show password"></button></div>
<button id="ab" onclick="appw()">Save</button>
<button class="s" id="ar" onclick="aprs()">Back to the firmware password</button>
<div id="am" class="m"></div></div>{{/if}}
</main><div id="tt" class="tt"></div><script src="/ap_portal/client.js"></script><script>
var L=(navigator.language||'en').slice(0,2),T={
uk:{h:'Wi-Fi',hs:'Мережа роутера',hn:'Мережі',ld:'Пошук…',rf:'Шукати ще раз',cur:'підключено',hc:'Підключення',ss:'Назва мережі (SSID)',pw:'Пароль',cn:'Підключити зараз',ha:'Пароль Wi-Fi пристрою',ap:'Новий пароль, 8–63 символи',ab:'Зберегти',ar:'Повернути пароль з прошивки',
 ssid_empty:'Вкажіть назву мережі',ssid_long:'Назва мережі довша за 32 байти',psk_length:'Пароль — 8–64 символи (або порожній для відкритої мережі)',ap_password_length:'8–63 символи (латиниця, цифри, знаки) або порожньо для відкритої мережі',cnow:'Підключити зараз',cl2:'Зберегти, підключити пізніше',lw:'Чекає підключення:',lh:'Пристрій лишається у поточній мережі, поки ви на його Wi-Fi. Підключиться, коли останній телефон від\'єднається від його Wi-Fi, або після перезавантаження.',lx:'Скасувати',stayap:'Ви на Wi-Fi пристрою — продовжуйте тут. Адреса вище — для домашньої мережі.',tapcopy:'Натисніть адресу, щоб скопіювати.',copied:'Скопійовано',close:'Закрити',phone:'Телефон може перейти на іншу мережу — тоді відкрийте адресу вище.',wrong_password:'невірний пароль',not_found:'мережу не знайдено',failed:'помилка підключення',err:'Не вдається підключитися до',stay:'Пристрій лишається на своїй точці доступу — виправте і підключіть знову.',back:'Пристрій повернеться до',con:'Пристрій у мережі ',lost:'Немає зв’язку з пристроєм — телефон не на його точці?',none:'Не підключено до роутера',going1:'Підключаюсь до',going2:'Сторінка може перестати відповідати. Пристрій буде тут:',viaap:'Через Wi-Fi пристрою',vianew:'У новій мережі',found:'Пристрій у мережі',open:'Відкрити',lost:'Не вдалося знайти пристрій. Спробуйте адреси:',saved:'Збережено. Пристрій від\'єднає вас — підключіться з новим паролем.',bad:'8–63 символи або порожньо',empty:'Мереж не знайдено',name:'Вкажіть назву мережі',bs:'Зачекайте, поки закінчиться пошук.',bw:'Зачекайте — пристрій перемикає мережу.',busy:'Пристрій зайнятий — зачекайте кінця пошуку або перемикання.'},
ru:{h:'Wi-Fi',hs:'Сеть роутера',hn:'Сети',ld:'Поиск…',rf:'Искать ещё раз',cur:'подключено',hc:'Подключение',ss:'Имя сети (SSID)',pw:'Пароль',cn:'Подключить сейчас',ha:'Пароль Wi-Fi устройства',ap:'Новый пароль, 8–63 символа',ab:'Сохранить',ar:'Вернуть пароль из прошивки',
 ssid_empty:'Укажите имя сети',ssid_long:'Имя сети длиннее 32 байт',psk_length:'Пароль — 8–64 символа (или пустой для открытой сети)',ap_password_length:'8–63 символа (латиница, цифры, знаки) или пусто для открытой сети',cnow:'Подключить сейчас',cl2:'Сохранить, подключить позже',lw:'Ожидает подключения:',lh:'Устройство остаётся в текущей сети, пока вы на его Wi-Fi. Подключится, когда последний телефон отключится от его Wi-Fi, или после перезагрузки.',lx:'Отменить',stayap:'Вы на Wi-Fi устройства — продолжайте здесь. Адрес выше — для домашней сети.',tapcopy:'Нажмите адрес, чтобы скопировать.',copied:'Скопировано',close:'Закрыть',phone:'Телефон может перейти на другую сеть — тогда откройте адрес выше.',wrong_password:'неверный пароль',not_found:'сеть не найдена',failed:'ошибка подключения',err:'Не удаётся подключиться к',stay:'Устройство остаётся на своей точке доступа — исправьте и подключите снова.',back:'Устройство вернётся к',con:'Устройство в сети ',lost:'Нет связи с устройством — телефон не на его точке?',none:'Не подключено к роутеру',going1:'Подключаюсь к',going2:'Страница может перестать отвечать. Устройство будет здесь:',viaap:'Через Wi-Fi устройства',vianew:'В новой сети',found:'Устройство в сети',open:'Открыть',lost:'Не удалось найти устройство. Попробуйте адреса:',saved:'Сохранено. Устройство отключит вас — подключитесь с новым паролем.',bad:'8–63 символа или пусто',empty:'Сетей не найдено',name:'Укажите имя сети',bs:'Подождите, пока закончится поиск.',bw:'Подождите — устройство переключает сеть.',busy:'Устройство занято — дождитесь конца поиска или переключения.'}},
E={bs:'Wait for the search to finish.',bw:'Wait — the device is switching networks.',busy:'The device is busy — wait for the search or the switch to finish.',ssid_empty:'Enter the network name',ssid_long:'The network name is longer than 32 bytes',psk_length:'The password is 8–64 characters (or empty for an open network)',ap_password_length:'8–63 characters (Latin letters, digits, symbols) or empty for an open network',cnow:'Connect now',lw:'Waiting to connect:',lh:'The device stays on its current network while you are on its Wi-Fi. It connects when the last phone leaves its Wi-Fi, or after a restart.',lx:'Cancel',stayap:'You are on the device Wi-Fi — carry on here. The address above is for the home network.',tapcopy:'Tap an address to copy it.',copied:'Copied',close:'Close',phone:'Your phone may switch to another network — then open the address above.',wrong_password:'wrong password',not_found:'network not found',failed:'connection failed',err:'Cannot connect to',stay:'The device stays on its own Wi-Fi — fix it and connect again.',back:'The device goes back to',con:'Device is on ',lost:'No connection to the device — is the phone on its access point?',none:'Not connected to the router',going1:'Connecting to',going2:'This page may stop answering. The device will be here:',viaap:'Via the device Wi-Fi',vianew:'On the new network',found:'The device is on',open:'Open',lost:'Could not find the device. Try these addresses:',saved:'Saved. The device will drop you — join with the new password.',bad:'8–63 characters or empty',empty:'No networks found',name:'Enter the network name'},
X=T[L]||{},t=function(k){return X[k]||E[k]},g=function(i){return document.getElementById(i)};
for(var k in X){var e=g(k);if(e){if(e.tagName=='INPUT')e.placeholder=X[k];else e.textContent=X[k]}}
function bars(r){return r>-55?'▂▄▆█':r>-67?'▂▄▆':r>-78?'▂▄':'▂'}
// Full-screen layer while switching: nothing underneath can be pressed; a result gets Close.
function toast(html,btn,wait){var e=g('tt');e.innerHTML='<div class="tc">'+(wait?'<span class="sp big"></span>':'')+html+'</div>';var c=e.firstChild;
 if(btn){var b=document.createElement('a');b.className='tb';b.href=btn.url;b.textContent=btn.label;c.appendChild(b)}
 if(!wait){var x=document.createElement('button');x.className='cl';x.textContent=t('close');x.onclick=function(){e.style.display='none'};c.appendChild(x)}
 e.style.display='flex'}
function esc(x){return String(x==null?'':x).replace(/[&<>"']/g,function(c){return'&#'+c.charCodeAt(0)+';'})}
function addrs(a){return a.map(function(x){return '<div>'+(x.kind=='ap'?t('viaap')+' «'+esc(x.ssid)+'»: ':t('vianew')+': ')+'<a class="cp" href="'+esc(x.url)+'" data-copy="'+esc(x.url)+'">'+esc(x.url)+'</a> <a class="go" href="'+esc(x.url)+'" target="_blank" rel="noopener">↗</a></div>'}).join('')+'<div class="m">'+t('tapcopy')+'</div>'}
function copyText(t){try{if(navigator.clipboard&&window.isSecureContext){navigator.clipboard.writeText(t);return}}catch(e){}var a=document.createElement('textarea');a.value=t;a.setAttribute('readonly','');a.style.cssText='position:fixed;top:0;left:0;opacity:0';document.body.appendChild(a);a.select();a.setSelectionRange(0,t.length);try{document.execCommand('copy')}catch(e){}document.body.removeChild(a)}
function onCopy(root,label){root.addEventListener('click',function(e){var a=e.target.closest&&e.target.closest('a.cp');if(!a)return;e.preventDefault();copyText(a.getAttribute('data-copy'));var o=a.textContent;a.textContent='\u2713 '+label;setTimeout(function(){a.textContent=o},1800)})}
onCopy(document,t('copied'));
var lt=0;function again(ms){clearTimeout(lt);lt=setTimeout(load,ms)}
function load(){apPortal.networks().then(function(j){
 g('cl2').style.display=j.later_button===false?'none':'';later(j.later);g('st').innerHTML='';var s=document.createElement('span');s.className=j.connected?'ok':'m';
 var sw=j.pending&&j.target&&!(j.connected&&j.ssid===j.target),rv=!j.connected&&!sw&&j.reverted&&j.previous;LK.w=!!(sw||rv||busy.on);
 s.textContent=j.connected?t('con')+j.ssid+' · '+j.ip+' · '+j.rssi+' dBm':sw||rv?'':t('none');g('st').appendChild(s);
 if(sw||rv){var q=document.createElement('div');q.className='m';q.innerHTML='<span class="sp"></span>'+esc((rv?t('back')+' «'+j.previous:t('going1')+' «'+j.target)+'»…');g('st').appendChild(q)}
 rfb(!!j.scanning);var ls=g('ls');ls.innerHTML='';if(!j.networks.length){var e=document.createElement('li');e.className='m';e.innerHTML=(j.scanning?'<span class="sp"></span>':'')+esc(j.scanning?(X.ld||'Searching…'):t('empty'));ls.appendChild(e)}
 j.networks.forEach(function(n){var li=document.createElement('li');var cur=j.connected&&n.ssid==j.ssid;var a=document.createElement('span');a.textContent=n.ssid;if(n.lock){var l=document.createElement('span');l.className='lk';l.textContent='🔒';a.appendChild(l)}if(cur){var c=document.createElement('span');c.className='cu';c.textContent='✓ '+(X.cur||'connected');a.appendChild(c)}var b=document.createElement('span');b.className='m';b.textContent=bars(n.rssi);
  li.appendChild(a);li.appendChild(b);if(cur){li.className='cur';li.setAttribute('aria-disabled','true')}else{if(n.ssid==g('ss').value)li.className='sel';li.onclick=function(){if(LK.s||LK.w||LK.r)return;g('ss').value=n.ssid;g('pw').value='';g('pw').focus();load()}}ls.appendChild(li)});
 again(j.scanning?1500:5000)}).catch(function(){rfb(false);g('st').innerHTML='';var s=document.createElement('span');s.className='m';s.textContent=t('lost');g('st').appendChild(s);again(3000)})}
var LK={s:false,w:false,r:false};function lock(){var on=LK.s||LK.w||LK.r,w=LK.w||LK.r;['cn','cl2','la','rf','ab','ar'].forEach(function(k){var b=g(k);if(b)b.disabled=on});['ss','pw','ap'].forEach(function(k){var i=g(k);if(i)i.disabled=w});g('ls').classList.toggle('off',on);var c=g('cn');c.innerHTML=(w?'<span class="sp"></span>':'')+esc(X.cn||'Connect now');g('bz').textContent=on?t(w?'bw':'bs'):''}function rfb(on){var b=g('rf');LK.s=on;b.innerHTML=on?'<span class="sp"></span>'+esc(X.ld||'Searching…'):esc(X.rf||'Search again');lock()}
function scan(){if(LK.s||LK.w||LK.r)return;rfb(true);apPortal.scan().then(load,function(e){rfb(false);fail('cm',e.code||String(e.message||e))})}
var tick=null;function busy(on){busy.on=on;LK.w=on;lock();
 clearInterval(tick);if(on)tick=setInterval(function(){var e=g('tc');if(e)e.textContent=Math.round((Date.now()-busy.t)/1000)+' s'},500)}
function follow(p){busy.t=p.t;busy(true);toast('<b>'+t('going1')+' «'+esc(p.ssid)+'»… <span id="tc"></span></b><div class="m">'+t('going2')+'</div>'+addrs(p.addresses)+'<div class="m">'+t('phone')+'</div>',null,true);
 apPortal.watch(p.ssid,{host:p.host,since:p.t}).then(function(x){busy(false);return x},function(e){busy(false);throw e}).then(function(x){apPortal.pending.clear();g('pw').value='';g('ss').value='';
  if(x.onAp)toast('<b>'+t('found')+' «'+esc(p.ssid)+'»</b>'+addrs([{kind:'new',url:x.url}])+'<div class="m">'+t('stayap')+'</div>');
  else toast('<b>'+t('found')+' «'+esc(p.ssid)+'»: '+esc(x.ip)+'</b>',{url:x.url,label:t('open')})})
 .catch(function(e){apPortal.pending.clear();if(e.code)toast('<b class="er">'+t('err')+' «'+esc(p.ssid)+'»: '+esc(t(e.code))+(e.reason>0?' ('+(e.reason|0)+')':'')+'</b><div class="m">'+(e.previous?t('back')+' «'+esc(e.previous)+'».':t('stay'))+'</div>');else toast('<b>'+t('lost')+'</b>'+addrs(p.addresses))})}
// Field check before sending; the device answers with the same codes (shown the same way).
function fail(box,code,field){var m=g(box);m.className='m err';m.textContent=t(code)||code;if(field){g(field).classList.add('bad');g(field).focus()}}
function clr(box){var m=g(box);m.className='m';m.textContent='';['ss','pw','ap'].forEach(function(i){var e=g(i);if(e)e.classList.remove('bad')})}
function sta(){clr('cm');var s=g('ss').value,c=apPortal.check.ssid(s);if(c){fail('cm',c,'ss');return null}c=apPortal.check.psk(g('pw').value);if(c){fail('cm',c,'pw');return null}return s}
function conn(){if(LK.s||LK.w||LK.r)return;var s=sta();if(!s)return;LK.r=true;lock();
 apPortal.connect(s,g('pw').value).then(function(r){LK.r=false;g('cm').textContent='';var p={ssid:s,host:r.host,addresses:r.addresses,t:Date.now()};apPortal.pending.save(p);follow(p)})
 .catch(function(e){LK.r=false;lock();fail('cm',e.code||String(e.message||e))})}
function later(n){var e=g('lt');if(!n){e.innerHTML='';return}
 e.innerHTML='<div><b>'+t('lw')+' «'+esc(n)+'»</b><div class="m">'+t('lh')+'</div><div class="row"><button id="la">'+esc(t('cnow'))+'</button><button class="s" id="lc">'+esc(t('lx'))+'</button></div></div>';
 g('la').onclick=function(){if(LK.s||LK.w||LK.r)return;apPortal.networks().then(function(s){return apPortal.apply().then(function(){var p={ssid:n,host:s.host,addresses:apPortal.addresses(s,n),t:Date.now()};apPortal.pending.save(p);e.innerHTML='';follow(p)})}).catch(function(x){fail('cm',x.code||String(x.message||x))})};
 g('lc').onclick=function(){apPortal.cancel().then(function(){e.innerHTML='';again(500)},function(x){fail('cm',x.code||String(x.message||x))})}}
function connLater(){if(LK.s||LK.w||LK.r)return;var s=sta();if(!s)return;LK.r=true;lock();
 apPortal.connect(s,g('pw').value,{later:true}).then(function(){LK.r=false;lock();g('pw').value='';later(s);again(800)}).catch(function(e){LK.r=false;lock();fail('cm',e.code||String(e.message||e))})}
function appw(){if(LK.s||LK.w||LK.r)return;clr('am');var p=g('ap').value,c=apPortal.check.apPassword(p);if(c){fail('am',c,'ap');return}
 apPortal.setApPassword(p).then(function(){g('am').textContent=t('saved')}).catch(function(e){fail('am',e.code||String(e.message||e),'ap')})}
function aprs(){if(LK.s||LK.w||LK.r)return;apPortal.resetApPassword().then(function(){g('am').textContent=t('saved')}).catch(function(e){g('am').textContent=String(e.message||e)})}
function eyes(root){var o='<svg viewBox="0 0 24 24"><path d="M12 5C7 5 2.7 8.1 1 12c1.7 3.9 6 7 11 7s9.3-3.1 11-7c-1.7-3.9-6-7-11-7zm0 11.5a4.5 4.5 0 1 1 0-9 4.5 4.5 0 0 1 0 9zm0-7a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5z"/></svg>',c='<svg viewBox="0 0 24 24"><path d="M12 5C7 5 2.7 8.1 1 12c1.7 3.9 6 7 11 7s9.3-3.1 11-7c-1.7-3.9-6-7-11-7zm0 11.5a4.5 4.5 0 1 1 0-9 4.5 4.5 0 0 1 0 9zm0-7a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5z"/><path d="M3.3 2 22 20.7 20.7 22 2 3.3z"/></svg>';[].forEach.call(root.querySelectorAll('button.ey'),function(b){var i=b.previousElementSibling;b.innerHTML=o;b.onclick=function(){var h=i.type=='password';i.type=h?'text':'password';b.innerHTML=h?c:o;i.focus()}})}
eyes(document);load();scan();(function(){var p=apPortal.pending.load();if(p)follow(p)})();
document.addEventListener('visibilitychange',function(){if(!document.hidden)load()});window.addEventListener('online',function(){load()});
</script></body></html>
)HTML";

// The API client for the device's own site: GET /ap_portal/client.js, then window.apPortal.
// Draw the Wi-Fi popup in the site's own style and call these; the built-in /wifi page uses the
// same client.
//   apPortal.networks()                -> {scanning, connected, ssid, ip, rssi, host, ap_ip, ap_ssid,
//                                          ap_active, networks:[{ssid, rssi, lock}]}
//   apPortal.scan()
//   apPortal.connect(ssid, psk)        -> {ssid, addresses:[{kind:'mdns'|'ap', url, ssid?}]}
//   apPortal.watch(ssid, {host, timeout, interval}) -> Promise {ip, url, onAp} once the device answers
//                                          from the new network (polls this origin and host.local);
//                                          onAp: this page is on the device's own Wi-Fi, so the new
//                                          address cannot be opened from here
//   apPortal.connect(ssid, psk, {later: true}) -> saved; switches when the access point is free,
//                                          on restart, or on apPortal.apply(); apPortal.cancel()
//   networks().later                   -> the saved network still waiting, or ""
//   apPortal.setApPassword(pw), apPortal.resetApPassword()
//   Errors: the promise rejects with Error.code = ssid_empty | ssid_long | psk_length |
//   ap_password_length | method | not_found | forbidden | busy | http_<status>; apPortal.check.ssid(s) / .psk(p) /
//   .apPassword(p) give the same code before sending ('' = fine).
static const char CLIENT_JS[] = R"JS((function(w){var A='/ap_portal/';
function j(u,o){return fetch(u,o).then(function(r){var t=r.headers.get('content-type')||'',b=t.indexOf('json')>=0?r.json():r.text();
 if(r.ok)return b;return b.then(function(x){var e=new Error((x&&x.error)||('HTTP '+r.status));e.code=(x&&x.error)||('http_'+r.status);e.status=r.status;throw e},function(){var e=new Error('HTTP '+r.status);e.code='http_'+r.status;throw e})})}
function bytes(s){try{return new TextEncoder().encode(s).length}catch(e){return unescape(encodeURIComponent(s)).length}}
function f(d){var b=[];for(var k in d)b.push(k+'='+encodeURIComponent(d[k]));return{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-AP-Portal':'1'},body:b.join('&')}}
var P={base:'',
// The same rules as the device: '' when fine, otherwise the error code it would answer with.
check:{ssid:function(s){return !s?'ssid_empty':bytes(s)>32?'ssid_long':''},
 psk:function(p){var n=p?bytes(p):0;return p&&(n<8||n>64)?'psk_length':''},
 apPassword:function(p){return p&&(p.length<8||p.length>63||/[^\x20-\x7e]/.test(p))?'ap_password_length':''}},
networks:function(){return new Promise(function(res,rej){var k=setTimeout(function(){var e=new Error('timeout');e.code='timeout';rej(e)},4000);j(P.base+A+'networks').then(function(x){clearTimeout(k);res(x)},function(e){clearTimeout(k);rej(e)})})},
scan:function(){return j(P.base+A+'scan',f({}))},
addresses:function(s,ssid){var a=[{kind:'mdns',url:'http://'+s.host+'.local/',ssid:ssid}];if(s.ap_active)a.push({kind:'ap',url:'http://'+s.ap_ip+'/',ssid:s.ap_ssid});return a},
connect:function(ssid,psk,o){var d={ssid:ssid,psk:psk||''};if(o&&o.later)d.when='later';return P.networks().then(function(s){return j(P.base+A+'connect',f(d)).then(function(){return{ssid:ssid,host:s.host,addresses:P.addresses(s,ssid)}})})},
apply:function(){return j(P.base+A+'apply',f({}))},
cancel:function(){return j(P.base+A+'cancel',f({}))},
watch:function(ssid,o){o=o||{};var t0=o.since||Date.now(),lim=o.timeout||90000,iv=o.interval||3000,u=[P.base+A+'networks'],bad=0;
 if(o.host)u.push('http://'+o.host+'.local'+A+'networks');
 return new Promise(function(res,rej){(function tick(){if(Date.now()-t0>lim)return rej(new Error('timeout'));var i=0;
  (function next(){if(i>=u.length)return setTimeout(tick,iv);var c=window.AbortController?new AbortController():null,k=setTimeout(function(){if(c)c.abort()},2500),n=i;
   j(u[i++],c?{signal:c.signal}:{}).then(function(s){clearTimeout(k);if(s.connected&&s.ssid===ssid&&/^\d{1,3}(\.\d{1,3}){3}$/.test(s.ip))return res({ip:s.ip,url:'http://'+s.ip+'/',state:s,
     onAp:n===0&&location.hostname===s.ap_ip});
    if(s.target===ssid&&(s.sta_error||s.reverted)){if(s.reverted||(!s.pending&&++bad>=2)){var ec=/^[a-z0-9_]{1,32}$/.test(s.sta_error||'')?s.sta_error:'failed';var e=new Error(ec);e.code=ec;e.previous=s.previous;e.reason=s.sta_reason;return rej(e)}}else bad=0;next()}).catch(function(){clearTimeout(k);next()})})()})()})},
pending:{key:'apPortalWatch',save:function(o){try{localStorage.setItem(this.key,JSON.stringify(o))}catch(e){}},
 load:function(){try{var o=JSON.parse(localStorage.getItem(this.key)||'null');return o&&Date.now()-o.t<180000?o:null}catch(e){return null}},
 clear:function(){try{localStorage.removeItem(this.key)}catch(e){}}},
setApPassword:function(p){return j(P.base+A+'ap_password',f({password:p}))},
resetApPassword:function(){return j(P.base+A+'ap_password_reset',f({}))}};
w.apPortal=P})(window);
)JS";

// A Wi-Fi button in the corner + popup for any page: <script src="/ap_portal/popup.js"
// data-accent="#3d7bff"></script>. Served after CLIENT_JS (one file). Shadow DOM: the site's styles
// never reach it, its styles never reach the site. en / uk / ru.
static const char POPUP_JS[] = R"JS((function(){if(window.apPortalPopup)return;
var me=document.currentScript,acc=(me&&me.dataset.accent)||'#3d7bff',pos=(me&&me.dataset.position)||'right';
var L=(navigator.language||'en').slice(0,2),T={
en:{ssid_empty:'Enter the network name',ssid_long:'The network name is longer than 32 bytes',psk_length:'The password is 8–64 characters (or empty for an open network)',ap_password_length:'8–63 characters (Latin letters, digits, symbols) or empty for an open network',gl:'Save, connect later',lw:'Waiting to connect:',lh:'The device stays on its current network while you are on its Wi-Fi. It connects when the last phone leaves its Wi-Fi, or after a restart.',lx:'Cancel',sap:'You are on the device Wi-Fi — carry on here. The address above is for the home network.',tap:'Tap an address to copy it.',cop:'Copied',ph:'Your phone may switch to another network — then open the address above.',wrong_password:'wrong password',not_found:'network not found',failed:'connection failed',err:'Cannot connect to',stay:'The device stays on its own Wi-Fi — fix it and connect again.',back:'The device goes back to',t:'Wi-Fi',st:'Router network',con:'Device is on ',lost:'No connection to the device — is the phone on its access point?',none:'Not connected to the router',nets:'Networks',srch:'Searching…',again:'Search again',cur:'connected',empty:'No networks found',ssid:'Network name (SSID)',pw:'Password',go:'Connect now',name:'Enter the network name',g1:'Connecting to',g2:'This page may stop answering. The device will be here:',ap:'Via the device Wi-Fi',nw:'On the new network',found:'The device is on',open:'Open',lost:'Could not find the device. Try these addresses:',close:'Close',bs:'Wait for the search to finish.',bw:'Wait — the device is switching networks.',busy:'The device is busy — wait for the search or the switch to finish.'},
uk:{ssid_empty:'Вкажіть назву мережі',ssid_long:'Назва мережі довша за 32 байти',psk_length:'Пароль — 8–64 символи (або порожній для відкритої мережі)',ap_password_length:'8–63 символи (латиниця, цифри, знаки) або порожньо для відкритої мережі',gl:'Зберегти, підключити пізніше',lw:'Чекає підключення:',lh:'Пристрій лишається у поточній мережі, поки ви на його Wi-Fi. Підключиться, коли останній телефон від\'єднається від його Wi-Fi, або після перезавантаження.',lx:'Скасувати',sap:'Ви на Wi-Fi пристрою — продовжуйте тут. Адреса вище — для домашньої мережі.',tap:'Натисніть адресу, щоб скопіювати.',cop:'Скопійовано',ph:'Телефон може перейти на іншу мережу — тоді відкрийте адресу вище.',wrong_password:'невірний пароль',not_found:'мережу не знайдено',failed:'помилка підключення',err:'Не вдається підключитися до',stay:'Пристрій лишається на своїй точці доступу — виправте і підключіть знову.',back:'Пристрій повернеться до',t:'Wi-Fi',st:'Мережа роутера',con:'Пристрій у мережі ',lost:'Немає зв’язку з пристроєм — телефон не на його точці?',none:'Не підключено до роутера',nets:'Мережі',srch:'Пошук…',again:'Шукати ще раз',cur:'підключено',empty:'Мереж не знайдено',ssid:'Назва мережі (SSID)',pw:'Пароль',go:'Підключити зараз',name:'Вкажіть назву мережі',g1:'Підключаюсь до',g2:'Сторінка може перестати відповідати. Пристрій буде тут:',ap:'Через Wi-Fi пристрою',nw:'У новій мережі',found:'Пристрій у мережі',open:'Відкрити',lost:'Не вдалося знайти пристрій. Спробуйте адреси:',close:'Закрити',bs:'Зачекайте, поки закінчиться пошук.',bw:'Зачекайте — пристрій перемикає мережу.',busy:'Пристрій зайнятий — зачекайте кінця пошуку або перемикання.'},
ru:{ssid_empty:'Укажите имя сети',ssid_long:'Имя сети длиннее 32 байт',psk_length:'Пароль — 8–64 символа (или пустой для открытой сети)',ap_password_length:'8–63 символа (латиница, цифры, знаки) или пусто для открытой сети',gl:'Сохранить, подключить позже',lw:'Ожидает подключения:',lh:'Устройство остаётся в текущей сети, пока вы на его Wi-Fi. Подключится, когда последний телефон отключится от его Wi-Fi, или после перезагрузки.',lx:'Отменить',sap:'Вы на Wi-Fi устройства — продолжайте здесь. Адрес выше — для домашней сети.',tap:'Нажмите адрес, чтобы скопировать.',cop:'Скопировано',ph:'Телефон может перейти на другую сеть — тогда откройте адрес выше.',wrong_password:'неверный пароль',not_found:'сеть не найдена',failed:'ошибка подключения',err:'Не удаётся подключиться к',stay:'Устройство остаётся на своей точке доступа — исправьте и подключите снова.',back:'Устройство вернётся к',t:'Wi-Fi',st:'Сеть роутера',con:'Устройство в сети ',lost:'Нет связи с устройством — телефон не на его точке?',none:'Не подключено к роутеру',nets:'Сети',srch:'Поиск…',again:'Искать ещё раз',cur:'подключено',empty:'Сетей не найдено',ssid:'Имя сети (SSID)',pw:'Пароль',go:'Подключить сейчас',name:'Укажите имя сети',g1:'Подключаюсь к',g2:'Страница может перестать отвечать. Устройство будет здесь:',ap:'Через Wi-Fi устройства',nw:'В новой сети',found:'Устройство в сети',open:'Открыть',lost:'Не удалось найти устройство. Попробуйте адреса:',close:'Закрыть',bs:'Подождите, пока закончится поиск.',bw:'Подождите — устройство переключает сеть.',busy:'Устройство занято — дождитесь конца поиска или переключения.'}},X=T[L]||T.en;
var host=document.createElement('div');host.style.cssText='position:fixed;z-index:2147483000;bottom:16px;'+(pos=='left'?'left':'right')+':16px';
(document.body||document.documentElement).appendChild(host);var R=host.attachShadow({mode:'open'});
R.innerHTML='<style>:host{all:initial}*{-webkit-tap-highlight-color:transparent}:focus:not(:focus-visible){outline:none}button,a,li{transition:opacity .15s}button:active:not(:disabled),a:active,li:active{opacity:.7}*{box-sizing:border-box;font:15px/1.4 system-ui,-apple-system,Roboto,sans-serif}'+
'.fab{width:52px;height:52px;border-radius:50%;border:0;background:'+acc+';color:#fff;box-shadow:0 6px 20px rgba(0,0,0,.3);cursor:pointer;display:grid;place-items:center}'+
'.fab svg{width:26px;height:26px;fill:currentColor}.bg{display:none;position:fixed;top:0;right:0;bottom:0;left:0;background:rgba(0,0,0,.45);align-items:flex-end;justify-content:center}'+
'.bg.on{display:flex}.p{width:100%;max-width:440px;max-height:88vh;overflow:auto;background:#fff;color:#1b2230;border-radius:20px 20px 0 0;padding:18px}'+
'.p{max-height:85vh;max-height:calc(100dvh - 12px);overscroll-behavior:contain;-webkit-overflow-scrolling:touch}@media(min-width:600px){.bg{align-items:center}.p{border-radius:20px}}@media(prefers-color-scheme:dark){.p{background:#151922;color:#f2f4f8}input{background:#0b0d12!important;color:#f2f4f8}}'+
'h2{font-size:18px;font-weight:700;margin:0 0 12px;display:flex;justify-content:space-between;align-items:center}h3{font-size:14px;font-weight:700;margin:14px 0 6px;opacity:.8}h2 *,h3 *{font-weight:inherit;font-size:inherit}.x{cursor:pointer;opacity:.6;background:none;border:0;color:inherit;font-size:20px}'+
'.m{font-size:13px;opacity:.7}.ok{color:#22a559}.er{color:#e5484d}.lk{font-size:12px;margin-left:6px;opacity:.8}li.cur,li.cur:active{cursor:default;opacity:.55}.cu{font-size:12px;margin-left:8px;font-weight:600}ul{list-style:none;margin:0;padding:0}li{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:10px 4px;border-bottom:1px solid rgba(127,127,127,.18);cursor:pointer}'+
'li.sel{color:'+acc+';font-weight:600}input{width:100%;padding:11px;border-radius:12px;border:1px solid rgba(127,127,127,.35);margin:0 0 8px;background:#fff}'+
'button.b{width:100%;padding:12px;border-radius:12px;border:0;background:'+acc+';color:#fff;font-weight:700;cursor:pointer}button.s{width:100%;padding:10px;border-radius:12px;border:1px solid rgba(127,127,127,.35);background:none;color:inherit;font-weight:600;cursor:pointer;margin-top:8px}'+
'.sp{display:inline-block;width:14px;height:14px;border:2px solid currentColor;border-right-color:transparent;border-radius:50%;animation:spin .8s linear infinite;vertical-align:-2px;margin-right:8px}@keyframes spin{to{transform:rotate(360deg)}}button:disabled{opacity:.7;cursor:default}ul.off li{opacity:.45;pointer-events:none}input:disabled{opacity:.6}#bz{margin-top:6px}'+
'input.bad,input.bad:focus{border-color:#e5484d!important;box-shadow:0 0 0 1px #e5484d;outline:2px solid #e5484d}.err{color:#e5484d}#lt>div{margin-top:12px;padding:12px;border-radius:12px;background:rgba(127,127,127,.12)}#lt .row{display:flex;gap:8px}#lt .row button{margin-top:8px}'+
'.tt{display:none;position:fixed;top:0;right:0;bottom:0;left:0;z-index:3;align-items:center;justify-content:center;padding:16px;background:rgba(11,13,18,.82);-webkit-backdrop-filter:blur(4px);backdrop-filter:blur(4px)}'+
'.tc{width:100%;max-width:380px;background:#1b2230;color:#f2f4f8;border-radius:20px;padding:24px 20px;text-align:center;box-shadow:0 12px 40px rgba(0,0,0,.45)}.tc b{display:block;font-size:17px;margin-bottom:6px}.tc div{margin-top:6px}.tc .m{opacity:.75}.tc .er{color:#ff8a8f}'+
'.tc a{color:#9fc0ff;font-weight:600;text-decoration:none}.tc a.o{display:block;text-align:center;margin-top:14px;background:'+acc+';color:#fff;padding:12px;border-radius:12px}.tc button.s{border-color:rgba(255,255,255,.25)}'+
'.sp.big{width:44px;height:44px;border-width:4px;margin:0 auto 16px;display:block;color:'+acc+'}'+
'.pw{position:relative}.pw input{padding-right:48px}button.ey{position:absolute;right:6px;top:5px;width:38px;height:38px;padding:0;margin:0;border:0;background:none;color:inherit;opacity:.6;cursor:pointer;display:grid;place-items:center}button.ey svg{width:20px;height:20px;fill:currentColor}</style>'+
'<button class="fab" title="Wi-Fi"><svg viewBox="0 0 24 24"><path d="M12 21l3.6-4.8a6 6 0 0 0-7.2 0zm0-18C7.95 3 4.21 4.34 1.2 6.6L3 9a15 15 0 0 1 18 0l1.8-2.4A18.9 18.9 0 0 0 12 3zm0 6a12 12 0 0 0-7.2 2.4l1.8 2.4a9 9 0 0 1 10.8 0l1.8-2.4A12 12 0 0 0 12 9z"/></svg></button>'+
'<div class="bg"><div class="p"><h2><span>'+X.t+'</span><button class="x" aria-label="'+X.close+'">✕</button></h2>'+
'<div class="m" id="st">…</div><h3>'+X.nets+'</h3><ul id="ls"><li class="m">'+X.srch+'</li></ul><button class="s" id="rf">'+X.again+'</button>'+
'<h3>'+X.st+'</h3><input id="ss" placeholder="'+X.ssid+'" autocapitalize="none" autocomplete="off"><div class="pw"><input id="pw" type="password" placeholder="'+X.pw+'" autocomplete="off"><button type="button" class="ey" aria-label="Show password"></button></div>'+
'<button class="b" id="go">'+X.go+'</button><button class="s" id="gl" style="display:none">'+X.gl+'</button><div class="m" id="bz"></div><div class="m" id="cm"></div><div id="lt"></div></div></div><div class="tt" id="tt"></div>';
var $=function(s){return R.querySelector(s)},bg=$('.bg'),open=false;
function esc(x){return String(x==null?'':x).replace(/[&<>"']/g,function(c){return'&#'+c.charCodeAt(0)+';'})}
function bars(r){return r>-55?'▂▄▆█':r>-67?'▂▄▆':r>-78?'▂▄':'▂'}
function addrs(a){return a.map(function(x){return '<div>'+(x.kind=='ap'?X.ap+' «'+esc(x.ssid)+'»: ':X.nw+': ')+'<a class="cp" href="'+esc(x.url)+'" data-copy="'+esc(x.url)+'">'+esc(x.url)+'</a> <a class="go" href="'+esc(x.url)+'" target="_blank" rel="noopener">↗</a></div>'}).join('')+'<div class="m">'+X.tap+'</div>'}
function copyText(t){try{if(navigator.clipboard&&window.isSecureContext){navigator.clipboard.writeText(t);return}}catch(e){}var a=document.createElement('textarea');a.value=t;a.setAttribute('readonly','');a.style.cssText='position:fixed;top:0;left:0;opacity:0';document.body.appendChild(a);a.select();a.setSelectionRange(0,t.length);try{document.execCommand('copy')}catch(e){}document.body.removeChild(a)}
function onCopy(root,label){root.addEventListener('click',function(e){var a=e.target.closest&&e.target.closest('a.cp');if(!a)return;e.preventDefault();copyText(a.getAttribute('data-copy'));var o=a.textContent;a.textContent='\u2713 '+label;setTimeout(function(){a.textContent=o},1800)})}
onCopy(R,X.cop);
function toast(h,btn,wait){var e=$('#tt');e.innerHTML='<div class="tc">'+(wait?'<span class="sp big"></span>':'')+h+(btn?'<a class="o" href="'+esc(btn)+'">'+X.open+'</a>':'')+(wait?'':'<button class="s" id="tx">'+X.close+'</button>')+'</div>';
 e.style.display='flex';var x=$('#tx');if(x)x.onclick=function(){e.style.display='none'}}
var lt=0;function again(ms){clearTimeout(lt);lt=setTimeout(load,ms)}
var LK={s:false,w:false,r:false};function lock(){var on=LK.s||LK.w||LK.r,w=LK.w||LK.r;['#go','#gl','#la','#rf'].forEach(function(k){var b=$(k);if(b)b.disabled=on});$('#ss').disabled=w;$('#pw').disabled=w;$('#ls').classList.toggle('off',on);$('#go').innerHTML=(w?'<span class="sp"></span>':'')+esc(X.go);$('#bz').textContent=on?(w?X.bw:X.bs):''}function rfb(on){var b=$('#rf');LK.s=on;b.innerHTML=on?'<span class="sp"></span>'+esc(X.srch):esc(X.again);lock()}
function load(){if(!open)return;apPortal.networks().then(function(j){
 $('#gl').style.display=j.later_button===false?'none':'';later(j.later);var sw=j.pending&&j.target&&!(j.connected&&j.ssid===j.target),rv=!j.connected&&!sw&&j.reverted&&j.previous;LK.w=!!(sw||rv||busy.on);
 $('#st').innerHTML=(j.connected?'<span class="ok">'+esc(X.con+j.ssid+' · '+j.ip+' · '+j.rssi+' dBm')+'</span>':sw||rv?'':esc(X.none))+(sw||rv?'<div class="m"><span class="sp"></span>'+esc((rv?X.back+' «'+j.previous:X.g1+' «'+j.target)+'»…')+'</div>':'');
 rfb(!!j.scanning);var ls=$('#ls');ls.innerHTML='';if(!j.networks.length)ls.innerHTML='<li class="m">'+(j.scanning?'<span class="sp"></span>'+esc(X.srch):esc(X.empty))+'</li>';
 j.networks.forEach(function(n){var li=document.createElement('li');var cur=j.connected&&n.ssid==j.ssid;li.innerHTML='<span>'+esc(n.ssid)+(n.lock?'<span class="lk">🔒</span>':'')+(cur?'<span class="cu">✓ '+esc(X.cur)+'</span>':'')+'</span><span class="m">'+bars(n.rssi)+'</span>';
  if(cur){li.className='cur';li.setAttribute('aria-disabled','true')}else{if(n.ssid==$('#ss').value)li.className='sel';li.onclick=function(){if(LK.s||LK.w||LK.r)return;$('#ss').value=n.ssid;$('#pw').value='';$('#pw').focus();load()}}ls.appendChild(li)});
 if(open)again(j.scanning?1500:5000)}).catch(function(){if(!open)return;rfb(false);$('#st').innerHTML='<span class="m">'+esc(X.lost)+'</span>';again(3000)})}
function show(v){open=v;$('#pw').value='';bg.classList.toggle('on',v);if(v){rfb(true);apPortal.scan().then(load,load)}else clearTimeout(lt)}
$('.fab').onclick=function(){show(true)};if(me&&me.dataset.button=='none')$('.fab').style.display='none';$('.x').onclick=function(){show(false)};bg.onclick=function(e){if(e.target===bg)show(false)};
$('#rf').onclick=function(){if(LK.s||LK.w||LK.r)return;rfb(true);apPortal.scan().then(load,function(e){rfb(false);fail(e.code||String(e.message||e))})};
var tick=null;function busy(on){busy.on=on;LK.w=on;lock();
 clearInterval(tick);if(on)tick=setInterval(function(){var e=$('#tc');if(e)e.textContent=Math.round((Date.now()-busy.t)/1000)+' s'},500)}
function follow(p){busy.t=p.t;busy(true);toast('<b>'+X.g1+' «'+esc(p.ssid)+'»… <span id="tc"></span></b><div class="m">'+X.g2+'</div>'+addrs(p.addresses)+'<div class="m">'+X.ph+'</div>',null,true);
 apPortal.watch(p.ssid,{host:p.host,since:p.t}).then(function(x){busy(false);return x},function(e){busy(false);throw e}).then(function(x){apPortal.pending.clear();$('#pw').value='';$('#ss').value='';
  if(x.onAp)toast('<b>'+X.found+' «'+esc(p.ssid)+'»</b>'+addrs([{kind:'new',url:x.url}])+'<div class="m">'+X.sap+'</div>');
  else toast('<b>'+X.found+' «'+esc(p.ssid)+'»: '+esc(x.ip)+'</b>',x.url)})
 .catch(function(e){apPortal.pending.clear();if(e.code)toast('<b class="er">'+X.err+' «'+esc(p.ssid)+'»: '+esc(X[e.code]||e.code)+(e.reason>0?' ('+(e.reason|0)+')':'')+'</b><div class="m">'+(e.previous?X.back+' «'+esc(e.previous)+'».':X.stay)+'</div>');else toast('<b>'+X.lost+'</b>'+addrs(p.addresses))})}
function fail(code,field){var m=$('#cm');m.className='m err';m.textContent=X[code]||code;if(field){$(field).classList.add('bad');$(field).focus()}}
function sta(){var m=$('#cm');m.className='m';m.textContent='';$('#ss').classList.remove('bad');$('#pw').classList.remove('bad');
 var s=$('#ss').value,c=apPortal.check.ssid(s);if(c){fail(c,'#ss');return null}c=apPortal.check.psk($('#pw').value);if(c){fail(c,'#pw');return null}return s}
$('#go').onclick=function(){if(LK.s||LK.w||LK.r)return;var s=sta();if(!s)return;LK.r=true;lock();
 apPortal.connect(s,$('#pw').value).then(function(r){LK.r=false;var p={ssid:s,host:r.host,addresses:r.addresses,t:Date.now()};apPortal.pending.save(p);follow(p)})
 .catch(function(e){LK.r=false;lock();fail(e.code||String(e.message||e))})};
function later(n){var e=$('#lt');if(!n){e.innerHTML='';return}
 e.innerHTML='<div><b>'+X.lw+' «'+esc(n)+'»</b><div class="m">'+X.lh+'</div><div class="row"><button class="b" id="la">'+X.go+'</button><button class="s" id="lc">'+X.lx+'</button></div></div>';
 $('#la').onclick=function(){if(LK.s||LK.w||LK.r)return;apPortal.networks().then(function(s){return apPortal.apply().then(function(){var p={ssid:n,host:s.host,addresses:apPortal.addresses(s,n),t:Date.now()};apPortal.pending.save(p);e.innerHTML='';follow(p)})}).catch(function(x){fail(x.code||String(x.message||x))})};
 $('#lc').onclick=function(){apPortal.cancel().then(function(){e.innerHTML='';again(500)},function(x){fail(x.code||String(x.message||x))})}}
$('#gl').onclick=function(){if(LK.s||LK.w||LK.r)return;var s=sta();if(!s)return;LK.r=true;lock();
 apPortal.connect(s,$('#pw').value,{later:true}).then(function(){LK.r=false;lock();$('#pw').value='';later(s);again(800)}).catch(function(e){LK.r=false;lock();fail(e.code||String(e.message||e))})};
(function(){var p=apPortal.pending.load();if(p){show(true);follow(p)}})();
document.addEventListener('visibilitychange',function(){if(!document.hidden&&open)load()});window.addEventListener('online',function(){if(open)load()});
function eyes(root){var o='<svg viewBox="0 0 24 24"><path d="M12 5C7 5 2.7 8.1 1 12c1.7 3.9 6 7 11 7s9.3-3.1 11-7c-1.7-3.9-6-7-11-7zm0 11.5a4.5 4.5 0 1 1 0-9 4.5 4.5 0 0 1 0 9zm0-7a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5z"/></svg>',c='<svg viewBox="0 0 24 24"><path d="M12 5C7 5 2.7 8.1 1 12c1.7 3.9 6 7 11 7s9.3-3.1 11-7c-1.7-3.9-6-7-11-7zm0 11.5a4.5 4.5 0 1 1 0-9 4.5 4.5 0 0 1 0 9zm0-7a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5z"/><path d="M3.3 2 22 20.7 20.7 22 2 3.3z"/></svg>';[].forEach.call(root.querySelectorAll('button.ey'),function(b){var i=b.previousElementSibling;b.innerHTML=o;b.onclick=function(){var h=i.type=='password';i.type=h?'text':'password';b.innerHTML=h?c:o;i.focus()}})}
eyes(R);
window.apPortalPopup={open:function(){show(true)},close:function(){show(false)}};
})();
)JS";

// "/" for an access point client with ui_button: ESPHome's own page in a frame, the Wi-Fi button
// on top. The frame asks for "/?esp", which web_server answers as usual.
static const char WRAP_PAGE[] = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>{{title}}</title>
<style>html,body{margin:0;height:100%;overflow:hidden}iframe{border:0;width:100%;height:100%}</style></head>
<body><iframe src="/?esp=1" title="{{title}}"></iframe>
<script src="/ap_portal/popup.js" data-accent="{{accent_color}}"></script></body></html>
)HTML";

}  // namespace esphome::ap_portal
