import {statuses,validateExchanges} from './catalog.mjs';
const $=id=>document.getElementById(id),el=(tag,value)=>{const n=document.createElement(tag);if(value!==undefined)n.textContent=value;return n;};
const availability={enabled:'Enabled',disabled:'Disabled',active:'Active',halted:'Halted'};
let busy=false;

function anchor(label,url){const a=el('a',label);a.href=url;if(/^https:/.test(url))a.rel='noopener noreferrer';return a;}
function money(amount,currency){const [whole,fraction]=amount.split('.');return `${currency==='USD'?'$':''}${whole.replace(/\B(?=(\d{3})+(?!\d))/g,',')}${fraction?'.'+fraction.padEnd(2,'0'):''}${currency==='USD'?'':' '+currency}`;}

function card(r,venue){
 const article=el('article'),pill=el('span',statuses[r.status]);pill.className=`pill ${r.status}`;
 article.append(pill,el('h3',venue?`${r.exchange} — ${r.pair}`:r.exchange),el('p',r.note));
 const rows=[];
 if(venue)rows.push(['Deposits',availability[r.deposits]],['Withdrawals',availability[r.withdrawals]],['Trading',availability[r.trading]],['Last verified',r.last_verified]);
 else{
  rows.push(['Application date',r.application_date??'Not submitted']);
  rows.push(['Listing fee',r.listing_fee?money(r.listing_fee.amount,r.listing_fee.currency):'Not published']);
  if(r.funding)rows.push(['Funding target',money(r.funding.target,r.funding.currency)]);
 }
 rows.push(['Last updated',r.updated_at]);
 const dl=el('dl');for(const [k,v] of rows)dl.append(el('dt',k),el('dd',v));article.append(dl);
 const links=[];
 if(venue)links.push(anchor(`Official ${r.pair} market`,r.trading_url),anchor(`Verification — ${r.last_verified}`,r.verification_url));
 if(!venue&&r.listing_fee)links.push(anchor('Listing fee source',r.listing_fee.evidence_url));
 if(!venue&&r.funding)links.push(anchor('Live funding progress',r.funding.progress_url));
 for(const a of r.announcements)links.push(anchor(a.label,a.url));
 if(links.length){const ul=el('ul');for(const a of links){const li=el('li');li.append(a);ul.append(li);}article.append(ul);}
 return article;
}

async function load(){
 if(busy)return;busy=true;$('refresh').disabled=true;$('status').textContent='Loading reviewed records…';
 const abort=new AbortController(),timer=setTimeout(()=>abort.abort(),10000);
 try{
  const response=await fetch('catalog.json',{signal:abort.signal,cache:'no-cache'});if(!response.ok)throw Error('Unavailable');
  const raw=await response.text();if(raw.length>1048576)throw Error('Oversized');
  const data=validateExchanges(JSON.parse(raw));
  const venues=data.records.filter(r=>r.status==='live'||r.status==='suspended'),pending=data.records.filter(r=>!venues.includes(r));
  $('venues').replaceChildren(...(venues.length?venues.map(r=>card(r,true)):[el('p','XMZ is not trading on any verified exchange yet.')]));
  $('tracker').replaceChildren(...(pending.length?pending.map(r=>card(r,false)):[el('p','No listings are in progress.')]));
  $('status').textContent=`${venues.length} verified venue${venues.length===1?'':'s'}, ${pending.length} listing${pending.length===1?'':'s'} in progress.`;
  $('updated').textContent=`Catalog reviewed ${data.updated_at}.`;
 }catch(error){
  $('venues').replaceChildren();$('tracker').replaceChildren();$('updated').textContent='';
  $('status').textContent=error.message.startsWith('Catalog date is ahead')?error.message:'Exchange records are unavailable right now. This does not mean XMZ is or is not listed anywhere. Try refreshing later.';
 }finally{clearTimeout(timer);busy=false;$('refresh').disabled=false;}
}
$('refresh').addEventListener('click',load);load();
