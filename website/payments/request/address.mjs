// Public Monzero address parsing, mirroring get_account_address_from_str in
// src/cryptonote_basic/cryptonote_basic_impl.cpp. No keys or seeds are handled here.
export const networks={
 mainnet:{standard:86n,integrated:87n,subaddress:88n},
 testnet:{standard:111n,integrated:112n,subaddress:113n},
 stagenet:{standard:131n,integrated:132n,subaddress:133n},
};
const alphabet='123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz';
const encodedBlockSizes=[0,2,3,5,6,7,9,10,11];
const mask64=(1n<<64n)-1n;
const roundConstants=[0x1n,0x8082n,0x800000000000808an,0x8000000080008000n,0x808bn,0x80000001n,0x8000000080008081n,0x8000000000008009n,0x8an,0x88n,0x80008009n,0x8000000an,0x8000808bn,0x800000000000008bn,0x8000000000008089n,0x8000000000008003n,0x8000000000008002n,0x8000000000000080n,0x800an,0x800000008000000an,0x8000000080008081n,0x8000000000008080n,0x80000001n,0x8000000080008008n];
const rotations=[0,1,62,28,27,36,44,6,55,20,3,10,43,25,39,41,45,15,21,8,18,2,61,56,14];
const rotl=(x,n)=>n?((x<<BigInt(n))|(x>>BigInt(64-n)))&mask64:x;

// Original Keccak-256 (cn_fast_hash), not NIST SHA3-256: padding byte is 0x01.
export function keccak256(bytes){
 const rate=136,padded=new Uint8Array(Math.ceil((bytes.length+1)/rate)*rate);
 padded.set(bytes);padded[bytes.length]^=1;padded[padded.length-1]^=0x80;
 const s=new Array(25).fill(0n);
 for(let off=0;off<padded.length;off+=rate){
  for(let i=0;i<rate/8;i++){let lane=0n;for(let b=7;b>=0;b--)lane=(lane<<8n)|BigInt(padded[off+i*8+b]);s[i]^=lane;}
  for(let round=0;round<24;round++){
   const c=[0,1,2,3,4].map(x=>s[x]^s[x+5]^s[x+10]^s[x+15]^s[x+20]);
   for(let x=0;x<5;x++){const d=c[(x+4)%5]^rotl(c[(x+1)%5],1);for(let y=0;y<25;y+=5)s[x+y]^=d;}
   const b=new Array(25);
   for(let x=0;x<5;x++)for(let y=0;y<5;y++)b[y+5*((2*x+3*y)%5)]=rotl(s[x+5*y],rotations[x+5*y]);
   for(let x=0;x<5;x++)for(let y=0;y<5;y++)s[x+5*y]=b[x+5*y]^((~b[(x+1)%5+5*y]&mask64)&b[(x+2)%5+5*y]);
   s[0]^=roundConstants[round];
  }
 }
 const out=new Uint8Array(32);
 for(let i=0;i<32;i++)out[i]=Number((s[i>>3]>>BigInt(8*(i&7)))&0xffn);
 return out;
}

// Monero-style base58: 8-byte blocks encoded as 11 characters, final partial block shorter.
export function base58Decode(text){
 if(typeof text!=='string'||!text)return null;
 const full=Math.floor(text.length/11),rest=text.length%11,lastSize=encodedBlockSizes.indexOf(rest);
 if(lastSize<0)return null;
 const out=new Uint8Array(full*8+lastSize);
 for(let block=0;block*11<text.length;block++){
  const chunk=text.slice(block*11,block*11+11),size=block<full?8:lastSize;
  let value=0n;
  for(const ch of chunk){const digit=alphabet.indexOf(ch);if(digit<0)return null;value=value*58n+BigInt(digit);}
  if(value>=1n<<BigInt(8*size))return null;
  for(let i=size-1;i>=0;i--){out[block*8+i]=Number(value&0xffn);value>>=8n;}
 }
 return out;
}

export function base58Encode(bytes){
 let text='';
 for(let off=0;off<bytes.length;off+=8){
  const block=bytes.subarray(off,off+8);let value=0n;for(const b of block)value=(value<<8n)|BigInt(b);
  let chunk='';for(let i=0;i<encodedBlockSizes[block.length];i++){chunk=alphabet[Number(value%58n)]+chunk;value/=58n;}
  text+=chunk;
 }
 return text;
}

function readVarint(bytes){
 let value=0n,shift=0n;
 for(let i=0;i<bytes.length&&i<10;i++){
  value|=BigInt(bytes[i]&0x7f)<<shift;
  if(!(bytes[i]&0x80))return i>0&&bytes[i]===0?null:{value,length:i+1};
  shift+=7n;
 }
 return null;
}

const p=(1n<<255n)-19n;
const d=(-121665n*modPow(121666n,p-2n))%p+p;
const sqrtm1=modPow(2n,(p-1n)/4n);
function modPow(base,exp){let r=1n;base%=p;if(base<0n)base+=p;while(exp>0n){if(exp&1n)r=r*base%p;base=base*base%p;exp>>=1n;}return r;}

// Equivalent of crypto::check_key (ge_frombytes_vartime): canonical y, decompressible, no negative zero.
export function isValidPoint(bytes){
 let y=0n;for(let i=31;i>=0;i--)y=(y<<8n)|BigInt(bytes[i]);
 const sign=y>>255n;y&=(1n<<255n)-1n;
 if(y>=p)return false;
 const u=(y*y-1n+p)%p,v=(d*y%p*y+1n)%p;
 let x=u*modPow(v,p-2n)%p;x=modPow(x,(p+3n)/8n);
 const vxx=v*x%p*x%p;
 if(vxx!==u){if((vxx+u)%p!==0n)return false;x=x*sqrtm1%p;}
 if(x===0n&&sign===1n)return false;
 return true;
}

const hex=bytes=>Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');

// Returns {network,kind,paymentId} for a valid public address, otherwise {error}.
export function parseAddress(input){
 const text=typeof input==='string'?input.trim():'';
 if(!text)return {error:'Enter a recipient address.'};
 if(text.length>200)return {error:'This is too long to be a Monzero address.'};
 const bytes=base58Decode(text);
 if(!bytes)return {error:'The address contains characters or a length that Monzero base58 does not allow.'};
 if(bytes.length<4+1)return {error:'The address is too short.'};
 const body=bytes.subarray(0,bytes.length-4),checksum=bytes.subarray(bytes.length-4),expected=keccak256(body).subarray(0,4);
 if(expected.some((b,i)=>b!==checksum[i]))return {error:'The address checksum does not match. Check for a typing or copying mistake.'};
 const tag=readVarint(body);
 if(!tag)return {error:'The address prefix is malformed.'};
 let found=null;
 for(const [network,prefixes] of Object.entries(networks))for(const [kind,prefix] of Object.entries(prefixes))if(prefix===tag.value)found={network,kind};
 if(!found)return {error:'This address does not use a Monzero network prefix.'};
 const keys=body.subarray(tag.length);
 if(keys.length!==(found.kind==='integrated'?72:64))return {error:'The address has the wrong length for its type.'};
 if(!isValidPoint(keys.subarray(0,32))||!isValidPoint(keys.subarray(32,64)))return {error:'The address contains an invalid public key.'};
 return {...found,address:text,paymentId:found.kind==='integrated'?hex(keys.subarray(64,72)):null};
}
