// After a successful switch both fields (network name and password) are cleared: popup and /wifi page.
const {JSDOM}=require('jsdom');const X=require('./extract');
let pass=0,fail=0;const ok=(c,m)=>{c?pass++:(fail++,console.log('FAIL',m))};
const tick=ms=>new Promise(r=>setTimeout(r,ms));
const state={scanning:false,connected:true,ssid:'Home',ip:'1.2.3.4',rssi:-50,networks:[{ssid:'Home',rssi:-50,lock:true},{ssid:'Other',rssi:-60,lock:true}],host:'dev',ap_ip:'100.64.4.1',ap_ssid:'Dev',ap_active:true,later:''};
function api(watchResult){return {check:{ssid:s=>s?'':'ssid_empty',psk:()=>'',apPassword:()=>''},
 networks:()=>Promise.resolve(JSON.parse(JSON.stringify(state))),scan:()=>Promise.resolve({ok:true}),
 connect:()=>Promise.resolve({host:'dev',addresses:[]}),apply:()=>Promise.resolve({}),cancel:()=>Promise.resolve({}),
 watch:()=>watchResult(),addresses:()=>[],pending:{save(){},load(){return null},clear(){}}}}
async function popup(name,watchResult,expectCleared){
 const d=new JSDOM('<!DOCTYPE html><body></body>',{runScripts:'outside-only',url:'http://100.64.4.1/'});const w=d.window;
 w.apPortal=api(watchResult);w.eval(X.popup());const R=[...w.document.body.children].find(e=>e.shadowRoot).shadowRoot;const $=s=>R.querySelector(s);
 R.querySelector('.fab').click();await tick(50);
 $('#ss').value='Other';$('#pw').value='12345678';$('#go').onclick();await tick(50);
 ok(($('#ss').value===''&&$('#pw').value==='')===expectCleared,`popup ${name}: ss='${$('#ss').value}' pw='${$('#pw').value}'`)}
async function page(name,watchResult,expectCleared){
 const d=new JSDOM(X.wifiPage(),{runScripts:'dangerously',url:'http://100.64.4.1/wifi',beforeParse(w){w.apPortal=api(watchResult)}});
 const w=d.window;await tick(50);const g=id=>w.document.getElementById(id);
 g('ss').value='Other';g('pw').value='12345678';w.conn();await tick(50);
 ok((g('ss').value===''&&g('pw').value==='')===expectCleared,`page ${name}: ss='${g('ss').value}' pw='${g('pw').value}'`)}
(async()=>{
 const okW=()=>Promise.resolve({onAp:false,ip:'10.1.30.117',url:'http://10.1.30.117/'});
 const errW=()=>Promise.reject({code:'wrong_password',previous:'Home'});
 await popup('success',okW,true);await popup('failure',errW,false);
 await page('success',okW,true);await page('failure',errW,false);
 console.log(`=== ${pass} passed, ${fail} failed`);process.exit(fail?1:0)})();
