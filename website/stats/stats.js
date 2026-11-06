"use strict";
const $=id=>document.getElementById(id), fmt=n=>new Intl.NumberFormat('en',{maximumFractionDigits:2}).format(n);
let data=null,busy=false;
const rate=n=>n>=1e6?fmt(n/1e6)+' MH/s':n>=1e3?fmt(n/1e3)+' kH/s':fmt(n)+' H/s';
function chart(id,rows,key,label){
 const values=rows.filter(r=>Number.isFinite(r[key])); const box=$(id); box.replaceChildren();
 if(!values.length){box.textContent='No samples available.';return;}
 const ns='http://www.w3.org/2000/svg', svg=document.createElementNS(ns,'svg');svg.setAttribute('viewBox','0 0 560 240');svg.setAttribute('role','img');svg.setAttribute('aria-label',label+' by block height. Exact values in the data table below.');
 const add=(tag,attrs,text)=>{const el=document.createElementNS(ns,tag);for(const [k,v] of Object.entries(attrs))el.setAttribute(k,v);if(text!==undefined)el.textContent=text;svg.append(el);return el;};
 const max=Math.max(1,...values.map(r=>r[key])), first=rows[0].height,last=rows.at(-1).height;
 const x=r=>60+(r.height-first)/Math.max(1,last-first)*480,y=r=>200-r[key]/max*175;
 for(let i=0;i<4;i++){let yy=200-i*175/3;add('line',{x1:60,y1:yy,x2:540,y2:yy,stroke:'#293b49'});add('text',{x:54,y:yy+4,'text-anchor':'end',class:'axis'},new Intl.NumberFormat('en',{notation:'compact',maximumFractionDigits:1}).format(max*i/3));}
 add('path',{class:'plot',d:rows.map((r,i)=>Number.isFinite(r[key])?((i===0||!Number.isFinite(rows[i-1][key]))?'M':'L')+x(r)+','+y(r):'').join(' ')});
 for(const r of values){const dot=add('circle',{cx:x(r),cy:y(r),r:5,class:'point',tabindex:'0','aria-label':'Block '+r.height+': '+fmt(r[key])+' '+label});const t=document.createElementNS(ns,'title');t.textContent='Block '+r.height+': '+fmt(r[key])+' '+label;dot.append(t);}
 add('text',{x:60,y:228,class:'axis'},'#'+first);add('text',{x:540,y:228,class:'axis','text-anchor':'end'},'#'+last);box.append(svg);
}
function render(){if(!data)return;const rows=data.blocks.slice(-Number($('window').value)).map((r,i,a)=>({...r,hashrate:r.difficulty/data.target_seconds,interval:i&&r.timestamp>=a[i-1].timestamp?r.timestamp-a[i-1].timestamp:null}));
 $('hashrate').textContent=rate(rows.at(-1).hashrate);$('height').textContent=fmt(data.height);const gaps=rows.filter(r=>r.interval!==null);$('interval').textContent=gaps.length?fmt(gaps.reduce((a,r)=>a+r.interval,0)/gaps.length)+' s':'—';
 for(const [id,key,label] of [['hash-chart','hashrate','H/s'],['difficulty-chart','difficulty','difficulty'],['interval-chart','interval','seconds'],['tx-chart','transactions','transactions']])chart(id,rows,key,label);
 $('status').textContent='Mainnet · Updated '+new Date(data.generated_at*1000).toLocaleString()+' · '+rows.length+' blocks · '+new Date(rows[0].timestamp*1000).toLocaleString()+' to '+new Date(rows.at(-1).timestamp*1000).toLocaleString()+(data.synchronized?'':' · Public node is syncing');
 $('samples').replaceChildren();const fragment=document.createDocumentFragment();for(const r of rows){const tr=document.createElement('tr');for(const value of [r.height,new Date(r.timestamp*1000).toISOString(),fmt(r.hashrate),fmt(r.difficulty),r.interval===null?'—':r.interval,r.transactions]){const td=document.createElement('td');td.textContent=value;tr.append(td);}fragment.append(tr);} $('samples').append(fragment);
}
async function get(url){const res=await fetch(url,{signal:AbortSignal.timeout(20000)});if(!res.ok)throw Error('Unavailable');return res.json();}
async function refresh(){if(busy)return;busy=true;$('refresh').disabled=true;await Promise.allSettled([(async()=>{try{const next=await get('api.php');if(next.network!=='mainnet'||!next.blocks?.length||!(next.target_seconds>0))throw Error('Invalid data');data=next;render();}catch{$('status').textContent=data?'Refresh failed. Displaying the last successful snapshot from '+new Date(data.generated_at*1000).toLocaleString()+'.':'Mainnet data is temporarily unavailable. Please retry.';}})(),(async()=>{try{const m=await get('../api/miner-stats/');if(m.status!=='OK')throw Error('Unavailable');$('miners').textContent=fmt(m.active_miners);$('miner-note').textContent='Opt-in reports within 3 minutes · '+rate(m.total_hashrate)+' reported';}catch{$('miners').textContent='—';$('miner-note').textContent='Reporting service unavailable; not zero miners.';}})()]);busy=false;$('refresh').disabled=false;}
$('window').addEventListener('change',render);$('refresh').addEventListener('click',refresh);refresh();setInterval(()=>{if(!document.hidden)refresh();},60000);
