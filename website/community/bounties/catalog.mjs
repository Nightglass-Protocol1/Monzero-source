export const categories=Object.freeze(['Bug fixes','Security improvements','Wallet improvements','Documentation','Translations','Website features','Blockchain integrations']);
const fail=()=>{throw Error('Invalid bounty catalog or missing review evidence.');};
const text=(v,max)=>typeof v==='string'&&v.trim()===v&&v.length>0&&v.length<=max&&!/[\p{Cc}\p{Cf}\p{Cs}\u2028\u2029]/u.test(v);
const prose=(v,max)=>typeof v==='string'&&text(v.replace(/\n/g,' '),max);
const keys=(o,names)=>o&&typeof o==='object'&&!Array.isArray(o)&&Object.keys(o).length===names.length&&names.every(k=>Object.prototype.hasOwnProperty.call(o,k));
const recordKeys='id title description category difficulty acceptance_criteria submission_instructions discussion_url status review_status updated_at maintainer reviewed_by reward_currency reward_atomic funding_status funding_evidence_url funding_checked_at completion_evidence_url completion_checked_at payment_evidence_url payment_checked_at'.split(' ');
function date(v){if(typeof v!=='string'||!/^\d{4}-\d{2}-\d{2}$/.test(v))return false;const d=new Date(v+'T00:00:00Z');return Number.isFinite(d.getTime())&&d.toISOString().slice(0,10)===v;}
function url(v){if(!text(v,2048)||/\s/.test(v))return false;try{const u=new URL(v);return u.protocol==='https:'&&!u.username&&!u.password&&!!u.hostname;}catch{return false;}}
export function validateBounties(data,today=new Date().toISOString().slice(0,10)){
 if(!date(today)||!keys(data,['schema','updated_at','records']))fail();
 if(date(data.updated_at)&&data.updated_at>today)throw Error('Catalog date is ahead of the browser clock. Check your clock or retry after the catalog date is corrected.');
 if(!data||data.schema!=='monzero-bounties-v1'||!date(data.updated_at)||data.updated_at>today||!Array.isArray(data.records)||data.records.length>200)fail();
 const ids=new Set();
 for(const b of data.records){
  if(!keys(b,recordKeys)||!text(b.id,64)||!/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(b.id)||ids.has(b.id))fail();ids.add(b.id);
  if(!text(b.title,160)||!prose(b.description,4000)||!categories.includes(b.category)||!['beginner','intermediate','advanced'].includes(b.difficulty))fail();
  if(!Array.isArray(b.acceptance_criteria)||b.acceptance_criteria.length<1||b.acceptance_criteria.length>20||!b.acceptance_criteria.every(v=>text(v,1000))||!prose(b.submission_instructions,3000)||!url(b.discussion_url))fail();
  if(!['open','in-review','completed','cancelled'].includes(b.status)||!['awaiting-submission','under-review','accepted','closed'].includes(b.review_status))fail();
  const reviewByStatus={'open':'awaiting-submission','in-review':'under-review','completed':'accepted','cancelled':'closed'};
  if(b.review_status!==reviewByStatus[b.status])fail();
  if(!date(b.updated_at)||b.updated_at>data.updated_at||!text(b.maintainer,120)||!text(b.reviewed_by,120))fail();
  if(b.reward_currency!=='XMZ'||typeof b.reward_atomic!=='string'||!/^(0|[1-9][0-9]{0,19})$/.test(b.reward_atomic)||BigInt(b.reward_atomic)>18446744073709551615n)fail();
  if(!['none','proposed','funded','paid'].includes(b.funding_status))fail();
  if(b.funding_status==='none'?(b.reward_atomic!=='0'):(b.reward_atomic==='0'))fail();
  if(['funded','paid'].includes(b.funding_status)){
   if(!url(b.funding_evidence_url)||!date(b.funding_checked_at)||b.funding_checked_at>b.updated_at)fail();
  }else if(b.funding_evidence_url!==null||b.funding_checked_at!==null)fail();
  if(b.status==='cancelled'&&b.funding_status==='funded')fail();
  if(b.status==='completed'){if(!url(b.completion_evidence_url)||!date(b.completion_checked_at)||b.completion_checked_at>b.updated_at)fail();}else if(b.completion_evidence_url!==null||b.completion_checked_at!==null)fail();
  if(b.funding_status==='paid'&&(b.status!=='completed'||!url(b.payment_evidence_url)||!date(b.payment_checked_at)||b.payment_checked_at>b.updated_at))fail();
  if(b.funding_status!=='paid'&&(b.payment_evidence_url!==null||b.payment_checked_at!==null))fail();
 }
 return data;
}
export function formatReward(atomic){
 if(typeof atomic!=='string'||!/^(0|[1-9][0-9]{0,19})$/.test(atomic)||BigInt(atomic)>18446744073709551615n)fail();
 const digits=atomic.padStart(12,'0'),fraction=digits.slice(-11).replace(/0+$/,'');
 return digits.slice(0,-11)+(fraction?'.'+fraction:'')+' XMZ';
}
