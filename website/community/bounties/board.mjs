import {categories,validateBounties,formatReward} from './catalog.mjs';
const $=id=>document.getElementById(id),el=(tag,value)=>{const n=document.createElement(tag);if(value!==undefined)n.textContent=value;return n;};
const fundingNames={none:'No reward',proposed:'Proposed — unfunded',funded:'Reported funded — see dated evidence',paid:'Reported paid — see evidence'};
let catalog=null,limit=20,busy=false;
function link(label,url){const a=el('a',label);a.href=url;a.rel='noopener noreferrer';return a;}
function card(b){
 const article=el('article');article.tabIndex=-1;article.append(el('h2',b.title),el('p',b.description));
 const dl=el('dl');for(const [label,value] of [['Category',b.category],['Difficulty',b.difficulty],['Task status',b.status],['Review status',b.review_status],['Reward',formatReward(b.reward_atomic)],['Funding',fundingNames[b.funding_status]],['Maintainer',b.maintainer],['Reviewed by',b.reviewed_by],['Updated',b.updated_at]])dl.append(el('dt',label),el('dd',value));article.append(dl);
 article.append(el('h3','Acceptance criteria'));const criteria=el('ul');for(const c of b.acceptance_criteria)criteria.append(el('li',c));article.append(criteria,el('h3','Submission instructions'),el('p',b.submission_instructions),link('Discuss this task',b.discussion_url));
 for(const [label,url] of [[`Funding evidence — checked ${b.funding_checked_at}`,b.funding_evidence_url],[`Completion evidence — checked ${b.completion_checked_at}`,b.completion_evidence_url],[`Payment evidence — checked ${b.payment_checked_at}`,b.payment_evidence_url]])if(url){const p=el('p');p.append(link(label,url));article.append(p);}
 return article;
}
function render(){
 if(!catalog)return;
 const query=$('search').value.trim().toLowerCase(),rows=catalog.records.filter(b=>(!query||`${b.title} ${b.description}`.toLowerCase().includes(query))&&(!$('category').value||b.category===$('category').value)&&(!$('state').value||b.status===$('state').value)&&(!$('funding').value||b.funding_status===$('funding').value));
 $('list').replaceChildren(...rows.slice(0,limit).map(card));$('more').hidden=rows.length<=limit;
 $('status').textContent=!catalog.records.length?'No reviewed bounty listings have been published. No funded rewards are advertised here.':!rows.length?'No tasks match these filters.':`Showing ${Math.min(limit,rows.length)} of ${rows.length} matching tasks.`;
}
async function load(){
 if(busy)return;busy=true;catalog=null;$('list').replaceChildren();$('more').hidden=true;$('updated').textContent='';$('status').textContent='Loading reviewed listings…';$('refresh').setAttribute('aria-disabled','true');
 const abort=new AbortController(),timer=setTimeout(()=>abort.abort(),10000);
 try{
  const response=await fetch('catalog.json',{signal:abort.signal,cache:'no-cache'});if(!response.ok)throw Error('Unavailable');
  const reader=response.body.getReader(),decoder=new TextDecoder('utf-8',{fatal:true});let raw='',bytes=0;
  try{while(true){const {value,done}=await reader.read();if(done)break;bytes+=value.byteLength;if(bytes>4194304)throw Error('Oversized');raw+=decoder.decode(value,{stream:true});}raw+=decoder.decode();}finally{await reader.cancel().catch(()=>{});reader.releaseLock();}
  catalog=validateBounties(JSON.parse(raw));limit=20;$('updated').textContent=`Catalog updated ${catalog.updated_at}. Funding evidence is checked separately for each task.`;render();
 }catch(error){catalog=null;$('list').replaceChildren();$('updated').textContent='';$('more').hidden=true;$('status').textContent=error.message.startsWith('Catalog date is ahead')?error.message:'Bounty catalog unavailable. This does not mean no tasks exist. Try refreshing later.';}
 finally{clearTimeout(timer);busy=false;$('refresh').disabled=false;$('refresh').removeAttribute('aria-disabled');}
}
for(const category of categories){const option=el('option',category);option.value=category;$('category').append(option);}
for(const id of ['search','category','state','funding']){$(id).disabled=false;$(id).addEventListener(id==='search'?'input':'change',()=>{limit=20;render();});}
$('more').addEventListener('click',()=>{const previous=$('list').children.length;limit+=20;render();$('list').children[previous]?.focus();});$('refresh').addEventListener('click',load);load();
