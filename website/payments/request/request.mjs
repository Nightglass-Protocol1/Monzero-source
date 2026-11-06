import {parseAddress} from './address.mjs';
import {parseAmount,displayAmount,validateText,buildUri} from './uri.mjs';
import {encodeQr,qrSvg} from './qr.mjs';
const $=id=>document.getElementById(id),el=(tag,value)=>{const n=document.createElement(tag);if(value!==undefined)n.textContent=value;return n;};
const kinds={standard:'Standard address',subaddress:'Subaddress',integrated:'Integrated address'};
let current=null;

function setAddressStatus(text,state){const s=$('address-status');s.textContent=text;s.dataset.state=state||'';}

function update(){
 current=null;$('result').hidden=true;$('error').textContent='';$('action-status').textContent='';
 const raw=$('address').value;
 if(!raw.trim()){setAddressStatus('');$('result-status').textContent='Enter a valid address to create a request.';return;}
 const address=parseAddress(raw);
 if(address.error){setAddressStatus(address.error,'bad');$('result-status').textContent='No request: the address is not valid.';return;}
 const network=address.network==='mainnet'?'':` on ${address.network} — not usable for real XMZ`;
 setAddressStatus(`Valid ${kinds[address.kind].toLowerCase()}${network}.`,address.network==='mainnet'?'ok':'bad');
 const amount=parseAmount($('amount').value),name=validateText('Recipient name',$('name').value),description=validateText('Description',$('description').value);
 const problem=amount.error||name.error||description.error;
 if(problem){$('error').textContent=problem;$('result-status').textContent='No request: fix the highlighted field.';return;}
 const uri=buildUri({address:address.address,atomic:amount.atomic,recipientName:name.text,description:description.text});
 let qr;try{qr=encodeQr(uri);}catch{$('error').textContent='This request is too long for a QR code. Shorten the name or description.';return;}
 current={uri,qr,address};
 $('qr').innerHTML=qrSvg(qr);
 const rows=[['Network',address.network],['Address type',kinds[address.kind]],['Amount',amount.atomic>0n?`${displayAmount(amount.atomic)} XMZ`:'Payer chooses']];
 if(address.paymentId)rows.push(['Payment ID',`${address.paymentId} (inside the integrated address)`]);
 if(name.text)rows.push(['Recipient name',name.text]);
 if(description.text)rows.push(['Description',description.text]);
 $('summary').replaceChildren(...rows.flatMap(([k,v])=>[el('dt',k),el('dd',v)]));
 $('uri').value=uri;$('result').hidden=false;
 $('result-status').textContent=address.network==='mainnet'?'Request ready. Check the address before sharing.':`Request ready for ${address.network}. It cannot receive mainnet XMZ.`;
}

async function copy(text,label){
 try{await navigator.clipboard.writeText(text);$('action-status').textContent=`${label} copied.`;}
 catch{$('action-status').textContent=`Could not copy automatically. Select the ${label.toLowerCase()} and copy it manually.`;}
}

function download(blob,name){
 const url=URL.createObjectURL(blob),a=el('a');a.href=url;a.download=name;document.body.append(a);a.click();a.remove();
 setTimeout(()=>URL.revokeObjectURL(url),1000);
}

function pngBlob(qr){
 const scale=8,dim=(qr.size+8)*scale,canvas=document.createElement('canvas');canvas.width=canvas.height=dim;
 const ctx=canvas.getContext('2d');ctx.fillStyle='#fff';ctx.fillRect(0,0,dim,dim);ctx.fillStyle='#000';
 qr.modules.forEach((row,y)=>row.forEach((dark,x)=>{if(dark)ctx.fillRect((x+4)*scale,(y+4)*scale,scale,scale);}));
 return new Promise((resolve,reject)=>canvas.toBlob(b=>b?resolve(b):reject(new Error('PNG unavailable')),'image/png'));
}

for(const id of ['address','amount','name','description'])$(id).addEventListener('input',update);
$('request').addEventListener('submit',e=>e.preventDefault());
$('copy-uri').addEventListener('click',()=>current&&copy(current.uri,'Payment link'));
$('copy-address').addEventListener('click',()=>current&&copy(current.address.address,'Address'));
$('download-svg').addEventListener('click',()=>current&&download(new Blob([qrSvg(current.qr,640)],{type:'image/svg+xml'}),'monzero-payment-request.svg'));
$('download-png').addEventListener('click',async()=>{if(!current)return;try{download(await pngBlob(current.qr),'monzero-payment-request.png');}catch{$('action-status').textContent='PNG download is unavailable in this browser. Use the SVG download.';}});
if(navigator.share){
 $('share').hidden=false;
 $('share').addEventListener('click',async()=>{if(!current)return;try{await navigator.share({title:'Monzero payment request',text:current.uri});}catch(e){if(e?.name!=='AbortError')$('action-status').textContent='Sharing failed. Copy the payment link instead.';}});
}
$('inputs').disabled=false;update();
