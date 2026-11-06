// Reviewed ecosystem directory. Every entry names who operates it; third-party services are labelled.
import {link} from '../exchanges/catalog.mjs';
export const categories=Object.freeze(['Mining pools','Wallet applications','Block explorers','Community projects','Developer libraries','Exchanges','Merchants','Educational resources']);
export const operators=Object.freeze({project:'Run by the Monzero project','third-party':'Third-party service'});
const fail=()=>{throw Error('Invalid ecosystem catalog.');};
const text=(v,max)=>typeof v==='string'&&v.trim()===v&&v.length>0&&v.length<=max&&!/[\p{Cc}\p{Cf}\p{Cs}\u2028\u2029]/u.test(v);
const keys=(o,names)=>o&&typeof o==='object'&&!Array.isArray(o)&&Object.keys(o).length===names.length&&names.every(k=>Object.prototype.hasOwnProperty.call(o,k));
function date(v){if(typeof v!=='string'||!/^\d{4}-\d{2}-\d{2}$/.test(v))return false;const d=new Date(v+'T00:00:00Z');return Number.isFinite(d.getTime())&&d.toISOString().slice(0,10)===v;}
export function validateEcosystem(data,today=new Date().toISOString().slice(0,10)){
 if(!date(today)||!keys(data,['schema','updated_at','records'])||data.schema!=='monzero-ecosystem-v1'||!date(data.updated_at)||!Array.isArray(data.records)||data.records.length>500)fail();
 if(data.updated_at>today)throw Error('Catalog date is ahead of the browser clock. Check your clock and refresh.');
 const ids=new Set();
 for(const r of data.records){
  if(!keys(r,['id','name','category','operator','description','url','verified_at'])||!text(r.id,64)||!/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(r.id)||ids.has(r.id))fail();ids.add(r.id);
  if(!text(r.name,100)||!categories.includes(r.category)||!Object.hasOwn(operators,r.operator)||!text(r.description,400)||!link(r.url))fail();
  // Third-party services must be external: a same-site path would imply the project runs them.
  if(r.operator==='third-party'&&!r.url.startsWith('https://'))fail();
  // Exchanges are listed only through the reviewed exchange tracker.
  if(r.category==='Exchanges')fail();
  if(!date(r.verified_at)||r.verified_at>data.updated_at)fail();
 }
 return data;
}
