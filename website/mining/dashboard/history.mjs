// Opt-in, memory-only observations. Never reconstruct upstream history.
export function appendObservation(samples,data,gap=false) {
 if(!Number.isSafeInteger(data.observed_at)||data.observed_at<=0)return samples;
 if(samples.length && data.observed_at<=samples.at(-1).at)return samples;
 const hash=typeof data.hashrate_hps==='number'&&Number.isFinite(data.hashrate_hps)&&data.hashrate_hps>=0?data.hashrate_hps:null;
 const pending=typeof data.pending_atomic==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(data.pending_atomic)?data.pending_atomic:null;
 return [...samples,{at:data.observed_at,hash,pending,gap:gap||Boolean(samples.length&&data.observed_at-samples.at(-1).at>90)}].slice(-120);
}
export function plotSegments(samples,field) {
 const values=samples.map(s=>field==='hash'?s.hash:s.pending===null?null:Number(s.pending)/1e11);
 const max=(Math.max(0,...values.filter(v=>v!==null))||1);
 const start=samples[0]?.at??0,end=samples.at(-1)?.at??start;
 const segments=[];let segment=[];
 for(let i=0;i<samples.length;i++) {
  if(samples[i].gap || values[i]===null){if(segment.length)segments.push(segment);segment=[];}
  if(values[i]===null)continue;
  segment.push([end===start?300:20+560*(samples[i].at-start)/(end-start),180-160*values[i]/max]);
 }
 if(segment.length)segments.push(segment);
 return {segments,max};
}
