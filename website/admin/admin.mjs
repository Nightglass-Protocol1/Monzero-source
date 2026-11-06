// Renders the authenticator enrolment QR code locally; the secret never leaves the page.
import {encodeQr,qrSvg} from '/payments/request/qr.mjs';
const box=document.getElementById('totp-qr');
if(box&&box.dataset.uri)box.innerHTML=qrSvg(encodeQr(box.dataset.uri),220);
