// Reviewed exchange listing records. Venue fields exist only once a listing is live or suspended,
// and every venue claim needs a dated verification link. Prices and volume are never stored here.
export const statuses=Object.freeze({'planned':'Planned','application-submitted':'Application submitted','under-review':'Under review','approved':'Approved','live':'Live','suspended':'Suspended'});
const fail=()=>{throw Error('Invalid exchange catalog.');};
const text=(v,max)=>typeof v==='string'&&v.trim()===v&&v.length>0&&v.length<=max&&!/[\p{Cc}\p{Cf}\p{Cs}\u2028\u2029]/u.test(v);
const keys=(o,names)=>o&&typeof o==='object'&&!Array.isArray(o)&&Object.keys(o).length===names.length&&names.every(k=>Object.prototype.hasOwnProperty.call(o,k));
const recordKeys='id exchange status note updated_at application_date listing_fee funding announcements pair trading_url deposits withdrawals trading last_verified verification_url'.split(' ');
const venueKeys=['pair','trading_url','deposits','withdrawals','trading','last_verified','verification_url'];
function date(v){if(typeof v!=='string'||!/^\d{4}-\d{2}-\d{2}$/.test(v))return false;const d=new Date(v+'T00:00:00Z');return Number.isFinite(d.getTime())&&d.toISOString().slice(0,10)===v;}
// Absolute HTTPS links, or same-site paths such as /fund/.
export function link(v){
 if(!text(v,2048)||/\s/.test(v))return false;
 if(/^\/(?!\/)/.test(v))return !v.includes('\\');
 try{const u=new URL(v);return u.protocol==='https:'&&!u.username&&!u.password&&!!u.hostname;}catch{return false;}
}
const amount=v=>typeof v==='string'&&/^(0|[1-9]\d{0,11})(\.\d{1,2})?$/.test(v)&&!/^0(\.0+)?$/.test(v);
export function validateExchanges(data,today=new Date().toISOString().slice(0,10)){
 if(!date(today)||!keys(data,['schema','updated_at','records'])||data.schema!=='monzero-exchanges-v1'||!date(data.updated_at)||!Array.isArray(data.records)||data.records.length>100)fail();
 if(data.updated_at>today)throw Error('Catalog date is ahead of the browser clock. Check your clock and refresh.');
 const ids=new Set();
 for(const r of data.records){
  if(!keys(r,recordKeys)||!text(r.id,64)||!/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(r.id)||ids.has(r.id))fail();ids.add(r.id);
  if(!text(r.exchange,80)||!Object.hasOwn(statuses,r.status)||!text(r.note,600)||!date(r.updated_at)||r.updated_at>data.updated_at)fail();
  if(r.status==='planned'?r.application_date!==null:(!date(r.application_date)||r.application_date>r.updated_at))fail();
  if(r.listing_fee!==null&&(!keys(r.listing_fee,['amount','currency','evidence_url'])||!amount(r.listing_fee.amount)||!/^[A-Z]{3,5}$/.test(r.listing_fee.currency)||!link(r.listing_fee.evidence_url)))fail();
  if(r.funding!==null&&(!keys(r.funding,['target','currency','progress_url'])||!amount(r.funding.target)||!/^[A-Z]{3,5}$/.test(r.funding.currency)||!link(r.funding.progress_url)))fail();
  if(!Array.isArray(r.announcements)||r.announcements.length>10||!r.announcements.every(a=>keys(a,['label','url'])&&text(a.label,160)&&link(a.url)))fail();
  const venue=r.status==='live'||r.status==='suspended';
  if(venue){
   if(!text(r.pair,24)||!/^XMZ\/[A-Z0-9]{2,10}$/.test(r.pair)||!link(r.trading_url)||!r.trading_url.startsWith('https://'))fail();
   if(!['enabled','disabled'].includes(r.deposits)||!['enabled','disabled'].includes(r.withdrawals))fail();
   if(r.trading!==(r.status==='live'?'active':'halted'))fail();
   if(!date(r.last_verified)||r.last_verified>r.updated_at||!link(r.verification_url))fail();
  }else if(venueKeys.some(k=>r[k]!==null))fail();
 }
 return data;
}
