// monzero: payment URIs as produced by wallet2::make_uri and accepted by wallet2::parse_uri
// (src/wallet/wallet2.cpp). Supported keys: tx_amount, recipient_name, tx_description.
export const DECIMALS=11;
export const MAX_ATOMIC=(1n<<64n)-1n;
export const MAX_TEXT=200;

// Mirrors cryptonote::parse_amount, but rejects signs, exponents and spaces inside the value.
export function parseAmount(input){
 let text=typeof input==='string'?input.trim():'';
 if(!text)return {atomic:0n};
 if(!/^\d*\.?\d*$/.test(text)||text==='.')return {error:'Enter the amount as a plain decimal number, for example 12.5.'};
 let [whole,fraction='']=text.split('.');
 fraction=fraction.replace(/0+$/,'');
 if(fraction.length>DECIMALS)return {error:`XMZ amounts have at most ${DECIMALS} decimal places.`};
 const atomic=BigInt((whole||'0')+fraction.padEnd(DECIMALS,'0'));
 if(atomic>MAX_ATOMIC)return {error:'The amount is larger than a wallet can request.'};
 return {atomic};
}

// cryptonote::print_money: always shows every decimal place.
export function formatAtomic(atomic){
 const digits=atomic.toString().padStart(DECIMALS+1,'0');
 return `${digits.slice(0,-DECIMALS)}.${digits.slice(-DECIMALS)}`;
}

// Trim trailing zeros for display only; the URI keeps print_money's exact form.
export function displayAmount(atomic){
 return formatAtomic(atomic).replace(/\.?0+$/,'');
}

// parse_uri splits on & and = before decoding and decodes any %XX byte, so encode every
// reserved byte (the wallet's own encoder leaves = and ? unescaped, which its parser rejects).
export function encodeText(text){
 return Array.from(new TextEncoder().encode(text),b=>/[A-Za-z0-9\-._~]/.test(String.fromCharCode(b))?String.fromCharCode(b):'%'+b.toString(16).toUpperCase().padStart(2,'0')).join('');
}

export function validateText(label,value){
 const text=typeof value==='string'?value.trim():'';
 if(text.length>MAX_TEXT)return {error:`${label} must be ${MAX_TEXT} characters or fewer.`};
 if(/[\u0000-\u001f\u007f]/.test(text))return {error:`${label} cannot contain control characters or line breaks.`};
 return {text};
}

export function buildUri({address,atomic=0n,recipientName='',description=''}){
 const fields=[];
 if(atomic>0n)fields.push(`tx_amount=${formatAtomic(atomic)}`);
 if(recipientName)fields.push(`recipient_name=${encodeText(recipientName)}`);
 if(description)fields.push(`tx_description=${encodeText(description)}`);
 return `monzero:${address}${fields.length?'?'+fields.join('&'):''}`;
}
