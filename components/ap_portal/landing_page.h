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
{{#if secondary_href}}<a class="s" href="{{secondary_href}}">{{secondary_label}}</a>{{/if}}
<div class="o"><b id="oa">All addresses</b>
<div><span id="o1">Device Wi-Fi</span>: <a href="http://{{ap_ip}}/">http://{{ap_ip}}/</a></div>
{{#if sta_ip}}<div><span id="o2">Home network</span>: <a href="http://{{sta_ip}}/">http://{{sta_ip}}/</a></div>{{/if}}
<div><span id="o3">Home network (mDNS)</span>: <a href="http://{{host}}.local/">http://{{host}}.local/</a></div>
</div><div class="h" id="h">If this page opened in a small window, open the address above in your browser.</div>
</div><script>
var L=(navigator.language||'en').slice(0,2),T={
uk:['Ви підключені до Wi-Fi пристрою.','Відкрити сторінку пристрою','Усі адреси','Wi-Fi пристрою','Домашня мережа','Домашня мережа (mDNS)','Якщо сторінка відкрилась у маленькому вікні, відкрийте адресу вище у браузері.'],
ru:['Вы подключены к Wi-Fi устройства.','Открыть страницу устройства','Все адреса','Wi-Fi устройства','Домашняя сеть','Домашняя сеть (mDNS)','Если страница открылась в маленьком окне, откройте адрес выше в браузере.']};
if(T[L]){var x=T[L],g=function(i){return document.getElementById(i)},s=function(i,v){var e=g(i);if(e)e.textContent=v};
s('t',x[0]);{{#if primary_default}}s('p',x[1]);{{/if}}s('oa',x[2]);s('o1',x[3]);s('o2',x[4]);s('o3',x[5]);s('h',x[6]);}
</script></body></html>
)HTML";

}  // namespace esphome::ap_portal
