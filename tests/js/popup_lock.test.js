const {JSDOM}=require('jsdom');const fs=require('fs');const X=require('./extract');
let pass=0,fail=0;const ok=(c,m)=>{c?pass++:(fail++,console.log('FAIL',m))};
const dom=new JSDOM('<!DOCTYPE html><body></body>',{runScripts:'outside-only',url:'http://100.64.4.1/'});
const w=dom.window;w.localStorage.clear();
let state={scanning:true,connected:true,ssid:'Home',ip:'192.168.1.5',rssi:-50,networks:[{ssid:'Home',rssi:-50,lock:true},{ssid:'Other',rssi:-60,lock:true}],host:'dev',ap_ip:'100.64.4.1',ap_ssid:'Dev',ap_active:true,later:''};
const calls=[];
w.apPortal={check:{ssid:s=>s?'':'ssid_empty',psk:()=>'',apPassword:()=>''},
 networks:()=>Promise.resolve(JSON.parse(JSON.stringify(state))),
 scan:()=>{calls.push('scan');return Promise.resolve({ok:true})},
 connect:(s,p)=>{calls.push('connect:'+s);return new Promise(()=>{})},
 apply:()=>Promise.resolve({}),cancel:()=>Promise.resolve({}),
 watch:()=>new Promise(()=>{}),addresses:()=>[],pending:{save(){},load(){return null},clear(){}}};
w.eval(X.popup());
const host=[...w.document.body.children].find(e=>e.shadowRoot);const R=host.shadowRoot;const $=s=>R.querySelector(s);
const tick=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
 w.apPortalPopup.open?w.apPortalPopup.open():$('.fab').click();
 await tick(50);
 // 1. scanning
 ok($('#go').disabled,'go disabled while scanning');ok($('#rf').disabled,'rf disabled while scanning');
 ok($('#ls').classList.contains('off'),'list off while scanning');ok(!$('#pw').disabled,'pw editable while scanning');
 ok($('#bz').textContent.length>0,'hint shown while scanning');ok($('#rf').innerHTML.includes('sp'),'spinner on rf');
 const other=[...R.querySelectorAll('#ls li')].find(l=>l.textContent.includes('Other'));
 other&&other.onclick&&other.onclick();ok($('#ss').value==='','list pick ignored while scanning');
 $('#ss').value='Other';$('#go').onclick();ok(!calls.some(c=>c.startsWith('connect')),'connect ignored while scanning');
 $('#rf').onclick&&$('#rf').onclick();ok(calls.filter(c=>c==='scan').length===1,'second scan ignored while scanning');
 // 2. scan done
 state.scanning=false;$('#ss').value='';
 await tick(1700);
 ok(!$('#go').disabled,'go enabled when idle');ok(!$('#ls').classList.contains('off'),'list on when idle');ok($('#bz').textContent==='','hint cleared');
 const o2=[...R.querySelectorAll('#ls li')].find(l=>l.textContent.includes('Other'));o2.onclick();ok($('#ss').value==='Other','list pick works when idle');
 // 3. connect in flight
 $('#pw').value='12345678';$('#go').onclick();await tick(10);
 ok(calls.includes('connect:Other'),'connect sent');ok($('#go').disabled&&$('#go').innerHTML.includes('sp'),'go spinner while sending');
 ok($('#ss').disabled&&$('#pw').disabled,'inputs locked while sending');ok($('#rf').disabled,'rf locked while sending');
 $('#go').onclick();ok(calls.filter(c=>c.startsWith('connect')).length===1,'double connect blocked');
 // 4. device reports a switch (another tab started it)
 const d2=new JSDOM('<!DOCTYPE html><body></body>',{runScripts:'outside-only',url:'http://100.64.4.1/'});const w2=d2.window;
 w2.apPortal=Object.assign({},w.apPortal,{networks:()=>Promise.resolve(Object.assign({},state,{pending:true,target:'Other',connected:true,ssid:'Home'}))});
 w2.eval(X.popup());const R2=[...w2.document.body.children].find(e=>e.shadowRoot).shadowRoot;
 R2.querySelector('.fab').click();await tick(50);state.scanning=false;
 await tick(1700);
 ok(R2.querySelector('#go').disabled,'go locked during a switch from elsewhere');ok(R2.querySelector('#pw').disabled,'pw locked during switch');
 ok(R2.querySelector('#bz').textContent.length>0,'switch hint');
 // 5. busy error text exists in all languages
 const src=X.popup();ok((src.match(/busy:'/g)||[]).length===3,'busy text en/uk/ru');
 console.log(`=== ${pass} passed, ${fail} failed`);process.exit(fail?1:0)})();
