const text = value => typeof value === 'string' && value.trim().length > 0 && value.length <= 200 && !/[\p{Cc}\p{Cf}\p{Cs}\p{Zl}\p{Zp}]/u.test(value);
const positive = value => typeof value === 'number' && Number.isFinite(value) && value > 0;
function date(value) {
  if(typeof value!=='string'||!/^\d{4}-\d{2}-\d{2}$/.test(value))return false;
  const time=Date.parse(value+'T00:00:00Z');return Number.isFinite(time)&&new Date(time).toISOString().slice(0,10)===value;
}
function url(value) {
  if(typeof value!=='string'||value.length>2048||!/^https:\/\/[^\s]+$/.test(value)
    ||/[\p{Cc}\p{Cf}\p{Cs}\p{Zl}\p{Zp}\\]/u.test(value)
    ||value.slice(8).split(/[/?#]/,1)[0].includes('@'))return false;
  try {const u=new URL(value);return u.protocol==='https:'&&!u.username&&!u.password;}catch{return false;}
}
function evidenceURL(value) {
  const u=new URL(value);u.hash='';if(!u.searchParams.toString())u.search='';return u.href;
}
export function validateCatalog(data) {
  if(!data || Object.keys(data).length!==3 || data?.schema!=='monzero-randomx-benchmarks-v1'||!date(data.updated_at)||!Array.isArray(data.records)||data.records.length>5000)throw new Error('Invalid benchmark catalog');
  const ids=new Set();
  const fields=['id','manufacturer','model','os','miner','miner_version','methodology','cores','threads','hashrate_hps','power_watts','power_basis','date','status','source_url','verification_url','verified_by'];
  for(const r of data.records) {
    if(!r || Object.keys(r).length!==fields.length || Object.keys(r).some(k=>!fields.includes(k)) || typeof r.id!=='string' || !/^[a-z0-9][a-z0-9-]{0,79}$/.test(r.id)||ids.has(r.id)
      ||!['manufacturer','model','os','miner','miner_version','methodology'].every(k=>text(r[k]))
      ||!Number.isInteger(r.cores)||r.cores<1||r.cores>4096||!Number.isInteger(r.threads)||r.threads<r.cores||r.threads>16384
      ||!positive(r.hashrate_hps)||(r.power_watts!==null&&!positive(r.power_watts))
      ||(r.power_watts===null)!==(r.power_basis==='unknown')
      ||!['wall','cpu-package','unknown'].includes(r.power_basis)||!date(r.date)||r.date>data.updated_at
      ||!['community','verified'].includes(r.status)||!url(r.source_url)
      ||(r.status==='verified'&&(!url(r.verification_url)||!text(r.verified_by)||evidenceURL(r.verification_url)===evidenceURL(r.source_url)))
      ||(r.status==='community'&&(r.verification_url!==null||r.verified_by!==null)))throw new Error('Invalid benchmark record');
    ids.add(r.id);
  }
  return data;
}
export function efficiency(record) {
  // Whole-machine and package power are not interchangeable.
  const result=record.power_basis==='wall' && record.power_watts>0 ? record.hashrate_hps/record.power_watts : null;
  return Number.isFinite(result) ? result : null;
}
export function selectRecords(records,{query='',status='all',manufacturer='',sort='hashrate'}={}) {
  const q=query.trim().toLowerCase();
  const selected=records.filter(r=>(status==='all'||r.status===status)&&(!manufacturer||r.manufacturer===manufacturer)
    &&[r.manufacturer,r.model,r.os,r.miner,r.miner_version].join(' ').toLowerCase().includes(q));
  const score=r=>sort==='efficiency'?efficiency(r):sort==='date'?Date.parse(r.date):r.hashrate_hps;
  return selected.sort((a,b)=>{const x=score(a),y=score(b);return x===null?(y===null?a.id.localeCompare(b.id):1):y===null?-1:y-x||a.id.localeCompare(b.id);});
}
