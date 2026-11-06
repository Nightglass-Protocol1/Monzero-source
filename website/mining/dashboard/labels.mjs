export const LABEL_STORAGE_KEY='monzero-pool-worker-labels-v1';
const identifier=id=>typeof id==='string'&&/^[0-9a-f]{64}$/.test(id);
export const validLabel=value=>typeof value==='string'&&value.trim().length>0&&value.length<=60&&!/[\p{Cc}\p{Cf}\p{Cs}\p{Zl}\p{Zp}]/u.test(value);
export function parseLabels(raw){
 if(raw===null)return {};
 if(typeof raw!=='string'||raw.length>20000)throw new Error('Saved labels are invalid');
 const value=JSON.parse(raw);
 if(!value||Array.isArray(value)||typeof value!=='object'||Object.keys(value).length>100||Object.entries(value).some(([key,label])=>!identifier(key)||!validLabel(label)))throw new Error('Saved labels are invalid');
 return value;
}
export function setLabel(labels,id,label){
 if(!identifier(id))throw new Error('Worker identifier unavailable');
 const next={...labels};label=label.trim();
 if(label==='')delete next[id];
 else {if(!validLabel(label))throw new Error('Use up to 60 visible characters');next[id]=label;}
 if(Object.keys(next).length>100)throw new Error('Maximum 100 labels; remove a label first');
 return next;
}
