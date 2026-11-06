import {categories,operators,validateEcosystem} from './catalog.mjs';
const $=id=>document.getElementById(id),el=(tag,value)=>{const n=document.createElement(tag);if(value!==undefined)n.textContent=value;return n;};
let catalog=null,busy=false;

function card(r){
 const article=el('article'),pill=el('span',operators[r.operator]);pill.className=`pill ${r.operator==='project'?'live':''}`;
 const a=el('a',r.operator==='third-party'?`Visit ${r.name} (external)`:`Open ${r.name}`);a.href=r.url;if(r.url.startsWith('https://'))a.rel='noopener noreferrer';
 const dl=el('dl');dl.append(el('dt','Category'),el('dd',r.category),el('dt','Last checked'),el('dd',r.verified_at));
 article.append(pill,el('h2',r.name),el('p',r.description),dl,a);
 return article;
}
function render(){
 if(!catalog)return;
 const q=$('search').value.trim().toLowerCase(),rows=catalog.records.filter(r=>(!q||`${r.name} ${r.description} ${r.category}`.toLowerCase().includes(q))&&(!$('category').value||r.category===$('category').value)&&(!$('operator').value||r.operator===$('operator').value));
 $('list').replaceChildren(...rows.map(card));
 $('status').textContent=!catalog.records.length?'No reviewed entries have been published yet.':!rows.length?'No entries match these filters.':`Showing ${rows.length} of ${catalog.records.length} entries.`;
}
async function load(){
 if(busy)return;busy=true;$('refresh').disabled=true;catalog=null;$('list').replaceChildren();$('status').textContent='Loading the reviewed directory…';
 const abort=new AbortController(),timer=setTimeout(()=>abort.abort(),10000);
 try{
  const response=await fetch('catalog.json',{signal:abort.signal,cache:'no-cache'});if(!response.ok)throw Error('Unavailable');
  const raw=await response.text();if(raw.length>1048576)throw Error('Oversized');
  catalog=validateEcosystem(JSON.parse(raw));$('updated').textContent=`Directory reviewed ${catalog.updated_at}.`;render();
 }catch(error){catalog=null;$('list').replaceChildren();$('updated').textContent='';$('status').textContent=error.message.startsWith('Catalog date is ahead')?error.message:'The directory is unavailable right now. Try refreshing later.';}
 finally{clearTimeout(timer);busy=false;$('refresh').disabled=false;}
}
for(const c of categories){const o=el('option',c);o.value=c;$('category').append(o);}
for(const id of ['search','category','operator']){$(id).disabled=false;$(id).addEventListener(id==='search'?'input':'change',render);}
$('refresh').addEventListener('click',load);load();
