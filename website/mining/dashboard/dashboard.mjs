import {createAlertMonitor} from './alerts.mjs';
import {LABEL_STORAGE_KEY,parseLabels,setLabel} from './labels.mjs';
import {appendObservation,plotSegments} from './history.mjs';
const form=document.querySelector('#lookup'),address=document.querySelector('#address');
const status=document.querySelector('#status'),results=document.querySelector('#results');
const workers=document.querySelector('#workers'),payments=document.querySelector('#payments');
const auto=document.querySelector('#auto');let generation=0,controller=null,selected='';
const collect=document.querySelector('#collect-history'),historyView=document.querySelector('#session-history'),historyStatus=document.querySelector('#history-state');
let samples=[],historyGap=false;
const saveLabels=document.querySelector('#save-labels'),labelStatus=document.querySelector('#label-status');
let labels={},currentWorkers=[],savedEnabled=false;
saveLabels.checked=false;auto.checked=false;collect.checked=false;
const format=n=>typeof n==='number' && Number.isFinite(n)?new Intl.NumberFormat(undefined,{maximumSignificantDigits:7}).format(n):'Unavailable';
function atomic(value) {
  if(typeof value!=='string'||!/^(0|[1-9][0-9]{0,19})$/.test(value))return 'Unavailable';
  const padded=value.padStart(12,'0');return `${padded.slice(0,-11)}.${padded.slice(-11)} XMZ`;
}
function reset() {currentWorkers=[];results.replaceChildren();workers.replaceChildren();payments.replaceChildren();document.querySelector('#worker-state').textContent='No current observation.';}
function cancel() {generation++;controller?.abort();controller=null;reset();}
async function lookup(value) {
  cancel();const id=generation,monitorEpoch=alertEpoch;controller=new AbortController();const requestController=controller;const timer=setTimeout(()=>requestController.abort(),16000);
  status.textContent='Loading pool observation…';
  try {
    const response=await fetch('api.php',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({address:value}),signal:controller.signal,cache:'no-store',referrerPolicy:'no-referrer'});
    const data=await response.json();
    if(id!==generation)return;
    if(!response.ok || data.schema!=='monzero-pool-miner-view-v1')throw new Error(response.status===429?'Lookup limit reached. Try again in one minute.':'Pool observation unavailable. Try again later.');
    if(!Number.isSafeInteger(data.observed_at)||data.observed_at<=0)throw new Error('Pool timestamp unavailable.');
    if(collect.checked){const next=appendObservation(samples,data,historyGap);const skipped=next===samples;if(!skipped)historyGap=false;samples=next;renderHistory();if(skipped)historyStatus.textContent=`${samples.length} observations retained. Duplicate or earlier response time skipped.`;}
    checkAlerts(data,monitorEpoch);
    status.textContent=`Pool response retrieved ${new Date(data.observed_at*1000).toLocaleString()}. This is a dated snapshot, not a live guarantee.`;
    const rows=[['Estimated ten-minute hashrate (H/s)',format(data.hashrate_hps)],['Connected workers',format(data.active_workers)],
      ['Pool contribution (%)',format(data.pool_share_percent)],['Pending pool rewards',atomic(data.pending_atomic)],['Total reported paid',atomic(data.paid_atomic)]];
    for(const [name,value] of rows){const dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=name;dd.textContent=value;results.append(dt,dd);}
    if(Array.isArray(data.workers)) {
      currentWorkers=data.workers;renderWorkers();if(data.worker_labels_available===false)labelStatus.textContent='Worker labels are unavailable until the website configures stable identifiers.';
      document.querySelector('#worker-state').textContent=data.workers.length?'Connection numbers apply only to this snapshot. Raw worker names are hidden because they may contain identifying login information. Individual hashrate and uptime are unavailable.':'No currently connected workers reported.';
    } else document.querySelector('#worker-state').textContent='Worker data unavailable.';
    if(Array.isArray(data.payments)) {
      for(const payment of data.payments) {
        if(!/^[0-9a-f]{64}$/.test(payment.txid))continue;
        const li=document.createElement('li'),link=document.createElement('a');
        link.href=`../../explorer/#/tx/${payment.txid}`;link.textContent=payment.txid;
        li.append(`${atomic(payment.amount_atomic)} · pool payment recorded ${new Date(payment.timestamp*1000).toLocaleString()} · `,link);payments.append(li);
      }
      if(!data.payments.length){const li=document.createElement('li');li.textContent='No sent payments returned by the pool.';payments.append(li);}
    } else {const li=document.createElement('li');li.textContent='Payment history unavailable.';payments.append(li);}
  } catch(error) {if(id===generation){checkAlerts(null,monitorEpoch);historyGap=true;reset();if(collect.checked)historyStatus.textContent=`Last lookup failed; ${samples.length} observations retained. The next sample starts a new segment.`;status.textContent=error.name==='AbortError'?'Lookup timed out. Try again later.':error instanceof SyntaxError?'Pool returned an unreadable response. Try again later.':error.message;}}
  finally {clearTimeout(timer);if(id===generation)controller=null;}
}
form.addEventListener('submit',event=>{event.preventDefault();const value=address.value.trim();
  if(![95,106].includes(value.length)||!/^[1-9A-HJ-NP-Za-km-z]+$/.test(value)){cancel();selected='';status.textContent='Enter a public XMZ mining address. Do not enter keys or seed words.';return;}
  if(selected!==value){stopAlerts();if(!savedEnabled)labels={};clearHistory();}selected=value;lookup(value);
});
address.addEventListener('input',()=>{stopAlerts();if(!savedEnabled)labels={};clearHistory();cancel();selected='';status.textContent='Address changed. Submit a new lookup.';});
document.querySelector('#clear').addEventListener('click',()=>{stopAlerts();forgetLabels();clearHistory();collect.checked=false;historyStatus.textContent='Session charts are off.';cancel();selected='';address.value='';auto.checked=false;status.textContent='Dashboard cleared.';});
setInterval(()=>{if(auto.checked&&selected&&!document.hidden&&!controller&&!workers.contains(document.activeElement))lookup(selected);},60000);
document.querySelector('#calculator-inputs').disabled=false;

function clearHistory(){samples=[];historyGap=false;renderHistory();}
collect.addEventListener('change',clearHistory);
function renderHistory(){
 historyView.replaceChildren();
 historyStatus.textContent=collect.checked?`${samples.length} observations collected during this visit. Look up activity to add a sample.`:'Session charts are off.';
 if(!collect.checked||!samples.length)return;
 const ns='http://www.w3.org/2000/svg';
 for(const [field,title,unit] of [['hash','Estimated ten-minute hashrate','H/s'],['pending','Pending pool rewards','XMZ']]){
  const heading=document.createElement('h3');heading.textContent=title;historyView.append(heading);
  const {segments,max}=plotSegments(samples,field);
  if(!segments.length){const note=document.createElement('p');note.textContent='No available samples for this metric.';historyView.append(note);continue;}
  const svg=document.createElementNS(ns,'svg');svg.setAttribute('viewBox','0 0 600 200');svg.setAttribute('role','img');svg.setAttribute('aria-label',`${title} by website retrieval time; exact observations in the table below`);svg.style.width='100%';svg.style.height='auto';
  for(const y of [20,100,180]){const grid=document.createElementNS(ns,'line');grid.setAttribute('x1','20');grid.setAttribute('x2','580');grid.setAttribute('y1',y);grid.setAttribute('y2',y);grid.setAttribute('stroke','currentColor');grid.setAttribute('opacity','.2');svg.append(grid);}
  for(const points of segments){
   const line=document.createElementNS(ns,'polyline');line.setAttribute('points',points.map(p=>p.join(',')).join(' '));line.setAttribute('fill','none');line.setAttribute('stroke','currentColor');line.setAttribute('stroke-width','2');svg.append(line);
   for(const [x,y] of points){const dot=document.createElementNS(ns,'circle');dot.setAttribute('cx',x);dot.setAttribute('cy',y);dot.setAttribute('r','3');dot.setAttribute('fill','currentColor');svg.append(dot);}
  }
  const scale=document.createElement('p');scale.textContent=`Vertical scale: 0–${format(max)} ${unit}. Left to right: ${new Date(samples[0].at*1000).toLocaleString()}–${new Date(samples.at(-1).at*1000).toLocaleString()}.`;
  historyView.append(svg,scale);
 }
 const details=document.createElement('details'),summary=document.createElement('summary'),table=document.createElement('table');summary.textContent='View exact observations';
 const caption=document.createElement('caption');caption.textContent='Website retrieval time, hashrate and pending rewards';table.append(caption);
 const head=document.createElement('tr');for(const title of ['Time','H/s','Pending XMZ']){const th=document.createElement('th');th.scope='col';th.textContent=title;head.append(th);}const thead=document.createElement('thead');thead.append(head);table.append(thead);
 const body=document.createElement('tbody');for(const sample of samples){const row=document.createElement('tr');for(const value of [new Date(sample.at*1000).toLocaleString(),format(sample.hash),atomic(sample.pending)]){const cell=document.createElement('td');cell.textContent=value;row.append(cell);}body.append(row);}table.append(body);details.append(summary,table);historyView.append(details);
}

function renderWorkers(){
 workers.replaceChildren();
 for(const worker of currentWorkers){
  const li=document.createElement('li'),name=document.createElement('span');name.textContent=worker.name;li.append(name);
  if(typeof worker.label_id==='string'&&/^[0-9a-f]{64}$/.test(worker.label_id)){
   const form=document.createElement('form'),label=document.createElement('label'),input=document.createElement('input'),button=document.createElement('button');
   label.append('Your label for '+worker.name);input.value=labels[worker.label_id]||'';input.maxLength=60;input.autocomplete='off';label.append(input);button.type='submit';button.textContent='Apply label';form.append(label,button);
   form.addEventListener('submit',event=>{event.preventDefault();try{
    saveWorkerLabel(worker.label_id,input.value);
    // Update matching connections without replacing the focused form.
    workers.querySelectorAll('input[data-worker]').forEach(other=>{if(other.dataset.worker===worker.label_id)other.value=labels[worker.label_id]||'';});
   }catch(error){labelStatus.textContent='Label not saved: '+error.message;}});
   input.dataset.worker=worker.label_id;li.append(form);
  }
  workers.append(li);
 }
}

function saveWorkerLabel(id,value){
 let base=labels;
 if(savedEnabled){
  const raw=localStorage.getItem(LABEL_STORAGE_KEY);
  if(raw===null){savedEnabled=false;saveLabels.checked=false;base={};}
  else base=parseLabels(raw);
 }
 const next=setLabel(base,id,value);
 if(savedEnabled)localStorage.setItem(LABEL_STORAGE_KEY,JSON.stringify(next));
 labels=next;renderSavedLabels();labelStatus.textContent=savedEnabled?'Label saved on this device.':'Label applied in memory only.';
}
function renderSavedLabels(){
 const list=document.querySelector('#saved-label-list');list.replaceChildren();
 if(!savedEnabled)return;
 for(const [id,value] of Object.entries(labels)){
  const item=document.createElement('li'),button=document.createElement('button');button.type='button';button.textContent='Remove '+value;
  button.addEventListener('click',()=>{try{saveWorkerLabel(id,'');renderWorkers();}catch{labelStatus.textContent='Could not remove the saved label. Browser storage may be unavailable.';}});item.append(button);list.append(item);
 }
}
saveLabels.addEventListener('change',()=>{
 if(!saveLabels.checked){forgetLabels();return;}
 try{
  const next=parseLabels(JSON.stringify({...parseLabels(localStorage.getItem(LABEL_STORAGE_KEY)),...labels}));
  localStorage.setItem(LABEL_STORAGE_KEY,JSON.stringify(next));labels=next;savedEnabled=true;
  labelStatus.textContent='Saved labels enabled on this device.';renderWorkers();renderSavedLabels();
 }catch{savedEnabled=false;saveLabels.checked=false;labelStatus.textContent='Saved labels could not be accessed or exceed the 100-label limit. Existing saved data may remain; use Forget or browser site-data controls.';}
});
function forgetLabels(){
 labels={};savedEnabled=false;saveLabels.checked=false;renderWorkers();renderSavedLabels();
 try{localStorage.removeItem(LABEL_STORAGE_KEY);labelStatus.textContent='Saved labels forgotten.';}catch{labelStatus.textContent='Browser storage could not be cleared. Use browser site-data controls to remove saved labels.';}
}
document.querySelector('#forget-labels').addEventListener('click',forgetLabels);
window.addEventListener('storage',event=>{
 if(event.storageArea!==localStorage || (event.key!==LABEL_STORAGE_KEY&&event.key!==null))return;
 if(event.newValue===null){labels={};savedEnabled=false;saveLabels.checked=false;renderWorkers();renderSavedLabels();labelStatus.textContent='Saved labels were removed in another tab.';}
 else if(savedEnabled){try{labels=parseLabels(event.newValue);if(!workers.contains(document.activeElement))renderWorkers();renderSavedLabels();}catch{labels={};savedEnabled=false;saveLabels.checked=false;renderWorkers();renderSavedLabels();labelStatus.textContent='Saved labels changed to an unreadable format; local saving is off.';}}
});

let alertMonitor=null,desktopAlerts=false,alertEpoch=0;
const alertState=document.querySelector('#alert-state'),notificationState=document.querySelector('#notification-state'),alertEvents=document.querySelector('#alert-events'),desktopButton=document.querySelector('#desktop-alerts');
const notifications=new Set();
const paymentAlertState=document.querySelector('#payment-alert-state');
document.querySelector('#alert-payments').checked=false;
document.querySelector('#alert-controls').disabled=false;
function stopAlerts(resetChoices=true){
 if(resetChoices)document.querySelector('#alert-payments').checked=false;
 alertEpoch++;alertMonitor=null;desktopAlerts=false;desktopButton.disabled=true;alertEvents.replaceChildren();
 for(const notification of notifications)notification.close();notifications.clear();
 alertState.textContent='Alerts are off.';paymentAlertState.textContent='Payment alerts are off.';notificationState.textContent='Browser notifications are off.';
}
document.querySelector('#stop-alerts').addEventListener('click',stopAlerts);
document.querySelector('#alert-settings').addEventListener('submit',event=>{
 event.preventDefault();stopAlerts(false);
 if(!selected){alertState.textContent='Look up a mining address before enabling alerts.';return;}
 try{alertMonitor=createAlertMonitor({hashDropPercent:Number(document.querySelector('#alert-drop').value),minimumWorkers:Number(document.querySelector('#alert-workers').value),payments:document.querySelector('#alert-payments').checked});
 desktopButton.disabled=false;paymentAlertState.textContent=alertMonitor.settings().payments?'Payment alerts enabled; waiting for a baseline.':'Payment alerts are off.';alertState.textContent='Session alerts enabled. Waiting for new observations; no extra requests are made.';
 }catch(error){alertState.textContent=error.message;}
});
desktopButton.addEventListener('click',async()=>{
 if(!alertMonitor)return;
 const epoch=alertEpoch;
 if(!isSecureContext||!('Notification' in window)){notificationState.textContent='Browser notifications are unavailable here. On-page alerts remain enabled.';return;}
 try{
 const permission=await Notification.requestPermission();
 if(epoch!==alertEpoch||!alertMonitor)return;
 desktopAlerts=permission==='granted';notificationState.textContent=desktopAlerts?'Browser notifications enabled for this visit.':'Browser permission was not granted. On-page alerts remain enabled.';
 }catch{if(epoch===alertEpoch)notificationState.textContent='Browser notifications could not be enabled. On-page alerts remain enabled.';}
});
function checkAlerts(data,epoch){
 if(!alertMonitor||epoch!==alertEpoch)return;
 const events=alertMonitor.sample(data,performance.now());
 const paymentMessages={off:'Payment alerts are off.',waiting:'Payment alerts enabled; waiting for a valid payment baseline.',active:'Payment alerts enabled. Existing history was excluded; new pool-reported IDs are monitored.',limit:'Payment monitoring paused at the 1,000-ID limit. Re-enable alerts to establish a fresh baseline.'};
 paymentAlertState.textContent=paymentMessages[alertMonitor.paymentState()];
 const baseline=alertMonitor.baseline(),settings=alertMonitor.settings();alertState.textContent=baseline===null?'Session alerts enabled; waiting for a positive hashrate baseline.':`Session alerts enabled. Baseline: ${format(baseline)} H/s; drop threshold ${settings.hashDropPercent}%; minimum workers ${settings.minimumWorkers}. Checks use subsequent lookups.`;
 for(const event of events){
 const item=document.createElement('li');item.textContent=`${new Date().toLocaleString()}: ${event.message}`;alertEvents.prepend(item);
 while(alertEvents.children.length>20)alertEvents.lastElementChild.remove();
 if(desktopAlerts){try{
 if(Notification.permission!=='granted')throw new Error('permission unavailable');
 const notification=new Notification('Monzero mining observation',{body:'A session monitoring condition was detected. Open your dashboard for details.',tag:'monzero-session-'+event.kind});notifications.add(notification);
 notification.onclose=()=>notifications.delete(notification);
 setTimeout(()=>{notification.close();notifications.delete(notification);},30000);
 }catch{desktopAlerts=false;notificationState.textContent='Browser notification delivery is unavailable. Check the on-page alerts.';}}
 }
}
