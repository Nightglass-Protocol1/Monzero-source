// Admin area helpers. Renders the authenticator enrolment QR code locally (the secret never
// leaves the page) and wires copy buttons ([data-copy-target] copies that element's text).
import {encodeQr,qrSvg} from '/payments/request/qr.mjs';
const box=document.getElementById('totp-qr');
if(box&&box.dataset.uri)box.innerHTML=qrSvg(encodeQr(box.dataset.uri),220);
document.addEventListener('click',async(ev)=>{
 const b=ev.target.closest('[data-copy-target]');if(!b)return;
 const el=document.getElementById(b.dataset.copyTarget);if(!el)return;
 try{await navigator.clipboard.writeText(el.textContent);b.textContent='Copied ✓';}
 catch{const r=document.createRange();r.selectNodeContents(el);const s=getSelection();s.removeAllRanges();s.addRange(r);b.textContent='Selected: press Ctrl+C';}
});
