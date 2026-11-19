const {JSDOM}=require('jsdom');const fs=require('fs');const X=require('./extract');
let pass=0,fail=0;const ok=(c,m)=>{c?pass++:(fail++,console.log('FAIL',m))};
let state={scanning:true,connected:true,ssid:'Home',ip:'1.2.3.4',rssi:-50,networks:[{ssid:'Home',rssi:-50,lock:true},{ssid:'Other',rssi:-60,lock:true}],host:'dev',ap_ip:'100.64.4.1',ap_ssid:'Dev',ap_active:true,later:''};
const calls=[];
const dom=new JSDOM(X.wifiPage(),{runScripts:'dangerously',url:'http://100.64.4.1/wifi',beforeParse(w){
 w.apPortal={check:{ssid:s=>s?'':'ssid_empty',psk:()=>'',apPassword:()=>''},networks:()=>Promise.resolve(JSON.parse(JSON.stringify(state))),
 scan:()=>{calls.push('scan');return Promise.resolve({ok:true})},connect:(s)=>{calls.push('connect:'+s);return new Promise(()=>{})},
 apply:()=>Promise.resolve({}),cancel:()=>Promise.resolve({}),watch:()=>new Promise(()=>{}),addresses:()=>[],
 setApPassword:()=>{calls.push('appw');return Promise.resolve({})},resetApPassword:()=>{calls.push('aprs');return Promise.resolve({})},
 pending:{save(){},load(){return null},clear(){}}};}});
const w=dom.window,g=id=>w.document.getElementById(id),tick=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{await tick(100);
 ok(g('cn').disabled,'cn disabled scanning');ok(g('rf').disabled,'rf disabled scanning');ok(g('ab')&&g('ab').disabled,'ap save disabled scanning');
 ok(g('ls').classList.contains('off'),'list off');ok(g('bz').textContent.length>0,'hint');
 g('ss').value='Other';w.conn();w.appw&&w.appw();w.aprs&&w.aprs();w.scan();
 ok(!calls.some(c=>c.startsWith('connect')||c==='appw'||c==='aprs'),'actions ignored while scanning');ok(calls.filter(c=>c=='scan').length===1,'one scan');
 state.scanning=false;await tick(1700);
 ok(!g('cn').disabled&&!g('ab').disabled&&!g('rf').disabled,'enabled idle');ok(!g('ls').classList.contains('off'),'list on idle');
 g('pw').value='12345678';w.conn();await tick(10);
 ok(calls.includes('connect:Other'),'connect sent');ok(g('cn').disabled&&g('ss').disabled&&g('pw').disabled&&g('ap').disabled,'locked while sending');
 w.conn();w.appw();ok(calls.filter(c=>c.startsWith('connect')).length===1&&!calls.includes('appw'),'no double action');
 console.log(`=== ${pass} passed, ${fail} failed`);process.exit(fail?1:0)})();
