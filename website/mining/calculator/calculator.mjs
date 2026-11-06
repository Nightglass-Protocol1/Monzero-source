import { estimateMining } from './calculations.mjs';
const form=document.querySelector('#calculator'), status=document.querySelector('#network-status');
const results=document.querySelector('#results'), error=document.querySelector('#error');
let observation=null;
const format=value=>new Intl.NumberFormat(undefined,{maximumSignificantDigits:8}).format(value);
const resultStatus=document.querySelector('#result-status');
function announce(message) { if(resultStatus.textContent!==message)resultStatus.textContent=message; }
function calculate(event) {
  event?.preventDefault();error.textContent='';results.replaceChildren();
  if (!(event ? form.reportValidity() : form.checkValidity())) return;
  if (!observation || Math.max(performance.now()-observation.receivedAt,Date.now()-observation.receivedWallAt)>300000) {
    document.querySelector('#result-status').textContent='A current mainnet observation is required. Refresh network data.';return;
  }
  try {
    const values=Object.fromEntries(new FormData(form));
    for (const field of ['hashrate','watts','electricityPrice','poolFeePercent']) values[field]=Number(values[field]);
    values.hardwareCost=values.hardwareCost.trim()===''?null:Number(values.hardwareCost);
    const result=estimateMining({...values,...observation});
    const rows=[['Expected XMZ / hour',result.xmzPerHour],['Expected XMZ / day',result.xmzPerDay],
      ['Expected XMZ / week',result.xmzPerWeek],['Expected XMZ / 30 days',result.xmzPerMonth],
      ['Electricity / day (kWh)',result.kwhPerDay],[`Electricity cost / day (${values.electricityCurrency})`,result.electricityPerDay]];
    for(const [label,value] of rows) {const dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=label;dd.textContent=format(value);results.append(dt,dd);}
    document.querySelector('#result-status').textContent='Approximate averages at the observed difficulty and reward.';
  } catch {error.textContent='Check the input values. This combination is outside the supported calculation range.';}
}
form.addEventListener('submit',calculate);
document.querySelector('#calculator-inputs').disabled=false;
form.addEventListener('input',()=>{results.replaceChildren();announce('Inputs changed. Calculate again to update the estimate.');});
async function refresh() {
  const button=document.querySelector('#refresh');button.disabled=true;observation=null;results.replaceChildren();
  document.querySelector('#network-details').textContent='';status.textContent='Loading public-node data…';
  document.querySelector('#result-status').textContent='Waiting for network data.';
  const controller=new AbortController(), timer=setTimeout(()=>controller.abort(),15000);
  try {
    const response=await fetch('../../api/node-info/index.php',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}',signal:controller.signal,cache:'no-store'});
    if(!response.ok)throw new Error('Unavailable');
    const info=await response.json();
    if(info?.status!=='OK'||info.nettype!=='mainnet'||info.synchronized!==true||info.tip_fresh!==true
      ||!Number.isSafeInteger(info.height)||info.height<1||info.tip_height!==info.height-1
      ||!Number.isFinite(info.difficulty)||info.difficulty<=0||info.difficulty_top64!==0
      ||!Number.isSafeInteger(info.tip_reward_atomic)||info.tip_reward_atomic<0
      ||!Number.isFinite(info.target)||info.target<=0||!Number.isSafeInteger(info.tip_timestamp)||info.tip_timestamp<=0)
      throw new Error('Unusable observation');
    observation={difficulty:info.difficulty,rewardXmz:info.tip_reward_atomic/1e11,observedAt:Date.now(),receivedWallAt:Date.now()-(Number.isFinite(info.observation_age_seconds)?Math.max(0,info.observation_age_seconds)*1000:0),receivedAt:performance.now()-(Number.isFinite(info.observation_age_seconds)?Math.max(0,info.observation_age_seconds)*1000:0)};
    status.textContent=`Mainnet block ${info.tip_height.toLocaleString()} · retrieved ${new Date(observation.observedAt).toLocaleString()}`;
    document.querySelector('#network-details').textContent=`Estimated network hashrate: ${format(info.difficulty/info.target)} H/s · target: ${info.target} seconds · observed block reward including fees: ${format(observation.rewardXmz)} XMZ · block time: ${new Date(info.tip_timestamp*1000).toLocaleString()}`;
    if(Number.isSafeInteger(info.observed_at))document.querySelector('#network-details').textContent+=` · node observation: ${new Date(info.observed_at*1000).toLocaleString()} (shared cache up to 15 seconds)`;
    calculate();
  } catch {status.textContent='Current mainnet data is unavailable or too old. No reward estimate is shown. Try refreshing later.';document.querySelector('#result-status').textContent='Network data unavailable.';}
  finally {clearTimeout(timer);button.disabled=false;}
}
document.querySelector('#refresh').addEventListener('click',refresh);
// Expire visible estimates too, rather than retaining an apparently current result.
setInterval(()=>{if(observation && Math.max(performance.now()-observation.receivedAt,Date.now()-observation.receivedWallAt)>300000){observation=null;results.replaceChildren();status.textContent='Observation expired. Refresh network data.';document.querySelector('#result-status').textContent='Estimate expired.';}},10000);
refresh();
