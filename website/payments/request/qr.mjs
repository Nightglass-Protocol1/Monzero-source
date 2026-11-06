// Minimal QR Code encoder (ISO/IEC 18004): byte mode, error correction level M,
// versions 1–40, automatic mask selection. Structure follows Project Nayuki's
// reference QR generator (MIT). Returns a boolean module matrix; no network or DOM use.
const ECC_PER_BLOCK=[0,10,16,26,18,24,16,18,22,22,26,30,22,22,24,24,28,28,26,26,26,26,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28];
const BLOCKS=[0,1,1,1,2,2,4,4,4,5,5,5,8,9,9,10,10,11,13,14,16,17,17,18,20,21,23,25,26,28,29,31,33,35,37,38,40,43,45,47,49];
const FORMAT_LEVEL_M=0;

function rawModules(ver){
 let result=(16*ver+128)*ver+64;
 if(ver>=2){const n=Math.floor(ver/7)+2;result-=(25*n-10)*n-55;if(ver>=7)result-=36;}
 return result;
}
const dataCodewords=ver=>Math.floor(rawModules(ver)/8)-ECC_PER_BLOCK[ver]*BLOCKS[ver];

function gfMul(x,y){let z=0;for(let i=7;i>=0;i--){z=(z<<1)^((z>>>7)*0x11d);z^=((y>>>i)&1)*x;}return z;}
function rsDivisor(degree){
 const result=new Array(degree).fill(0);result[degree-1]=1;let root=1;
 for(let i=0;i<degree;i++){for(let j=0;j<degree;j++){result[j]=gfMul(result[j],root);if(j+1<degree)result[j]^=result[j+1];}root=gfMul(root,2);}
 return result;
}
function rsRemainder(data,divisor){
 const result=divisor.map(()=>0);
 for(const b of data){const factor=b^result.shift();result.push(0);divisor.forEach((coef,i)=>{result[i]^=gfMul(coef,factor);});}
 return result;
}

function alignmentPositions(ver,size){
 if(ver===1)return [];
 const n=Math.floor(ver/7)+2,step=ver===32?26:Math.ceil((ver*4+4)/(n*2-2))*2,result=[6];
 for(let pos=size-7;result.length<n;pos-=step)result.splice(1,0,pos);
 return result;
}

const maskTest=[
 (x,y)=>(x+y)%2===0,(x,y)=>y%2===0,(x,y)=>x%3===0,(x,y)=>(x+y)%3===0,
 (x,y)=>(Math.floor(x/3)+Math.floor(y/2))%2===0,(x,y)=>x*y%2+x*y%3===0,
 (x,y)=>(x*y%2+x*y%3)%2===0,(x,y)=>((x+y)%2+x*y%3)%2===0,
];

function penalty(m){
 const size=m.length;let score=0,dark=0;
 const lines=[];for(let i=0;i<size;i++){lines.push(m[i]);lines.push(m.map(row=>row[i]));}
 for(const line of lines){
  let run=1;
  for(let i=1;i<=size;i++){if(i<size&&line[i]===line[i-1])run++;else{if(run>=5)score+=run-2;run=1;}}
  const bits=line.map(v=>v?'1':'0').join('');
  for(const pattern of ['10111010000','00001011101'])for(let i=bits.indexOf(pattern);i>=0;i=bits.indexOf(pattern,i+1))score+=40;
 }
 for(let y=0;y<size;y++)for(let x=0;x<size;x++){
  if(m[y][x])dark++;
  if(y<size-1&&x<size-1&&m[y][x]===m[y][x+1]&&m[y][x]===m[y+1][x]&&m[y][x]===m[y+1][x+1])score+=3;
 }
 const total=size*size;score+=(Math.ceil(Math.abs(dark*20-total*10)/total)-1)*10;
 return score;
}

export function encodeQr(text){
 const bytes=new TextEncoder().encode(text);
 let ver=1;
 for(;ver<=40;ver++)if(4+(ver<=9?8:16)+bytes.length*8<=dataCodewords(ver)*8)break;
 if(ver>40)throw new RangeError('Payment request is too long for a QR code.');
 const capacity=dataCodewords(ver),bits=[];
 const push=(value,len)=>{for(let i=len-1;i>=0;i--)bits.push((value>>>i)&1);};
 push(4,4);push(bytes.length,ver<=9?8:16);for(const b of bytes)push(b,8);
 push(0,Math.min(4,capacity*8-bits.length));push(0,(8-bits.length%8)%8);
 const data=[];for(let i=0;i<bits.length;i+=8)data.push(bits.slice(i,i+8).reduce((a,b)=>a<<1|b,0));
 for(let pad=0xec;data.length<capacity;pad^=0xec^0x11)data.push(pad);

 const numBlocks=BLOCKS[ver],eccLen=ECC_PER_BLOCK[ver],raw=Math.floor(rawModules(ver)/8);
 const numShort=numBlocks-raw%numBlocks,shortLen=Math.floor(raw/numBlocks),divisor=rsDivisor(eccLen),blocks=[];
 for(let i=0,k=0;i<numBlocks;i++){
  const dat=data.slice(k,k+shortLen-eccLen+(i<numShort?0:1));k+=dat.length;
  const ecc=rsRemainder(dat,divisor);if(i<numShort)dat.push(0);blocks.push(dat.concat(ecc));
 }
 const codewords=[];
 for(let i=0;i<blocks[0].length;i++)blocks.forEach((block,j)=>{if(i!==shortLen-eccLen||j>=numShort)codewords.push(block[i]);});

 const size=ver*4+17,modules=[],fixed=[];
 for(let i=0;i<size;i++){modules.push(new Array(size).fill(false));fixed.push(new Array(size).fill(false));}
 const set=(x,y,dark)=>{modules[y][x]=dark;fixed[y][x]=true;};
 for(let i=0;i<size;i++){set(6,i,i%2===0);set(i,6,i%2===0);}
 for(const [cx,cy] of [[3,3],[size-4,3],[3,size-4]])
  for(let dy=-4;dy<=4;dy++)for(let dx=-4;dx<=4;dx++){const x=cx+dx,y=cy+dy,dist=Math.max(Math.abs(dx),Math.abs(dy));if(x>=0&&x<size&&y>=0&&y<size)set(x,y,dist!==2&&dist!==4);}
 const align=alignmentPositions(ver,size),n=align.length;
 for(let i=0;i<n;i++)for(let j=0;j<n;j++){
  if((i===0&&j===0)||(i===0&&j===n-1)||(i===n-1&&j===0))continue;
  for(let dy=-2;dy<=2;dy++)for(let dx=-2;dx<=2;dx++)set(align[i]+dx,align[j]+dy,Math.max(Math.abs(dx),Math.abs(dy))!==1);
 }
 const drawFormat=mask=>{
  const value=FORMAT_LEVEL_M<<3|mask;let rem=value;for(let i=0;i<10;i++)rem=(rem<<1)^((rem>>>9)*0x537);
  const f=(value<<10|rem)^0x5412,bit=i=>((f>>>i)&1)===1;
  for(let i=0;i<=5;i++)set(8,i,bit(i));
  set(8,7,bit(6));set(8,8,bit(7));set(7,8,bit(8));
  for(let i=9;i<15;i++)set(14-i,8,bit(i));
  for(let i=0;i<8;i++)set(size-1-i,8,bit(i));
  for(let i=8;i<15;i++)set(8,size-15+i,bit(i));
  set(8,size-8,true);
 };
 drawFormat(0);
 if(ver>=7){
  let rem=ver;for(let i=0;i<12;i++)rem=(rem<<1)^((rem>>>11)*0x1f25);
  const v=ver<<12|rem;
  for(let i=0;i<18;i++){const dark=((v>>>i)&1)===1,a=size-11+i%3,b=Math.floor(i/3);set(a,b,dark);set(b,a,dark);}
 }
 for(let right=size-1,i=0;right>=1;right-=2){
  if(right===6)right=5;
  for(let vert=0;vert<size;vert++)for(let j=0;j<2;j++){
   const x=right-j,y=((right+1)&2)===0?size-1-vert:vert;
   if(!fixed[y][x]&&i<codewords.length*8){modules[y][x]=((codewords[i>>>3]>>>(7-(i&7)))&1)===1;i++;}
  }
 }
 let best=null,bestScore=Infinity;
 for(let mask=0;mask<8;mask++){
  const candidate=modules.map((row,y)=>row.map((dark,x)=>!fixed[y][x]&&maskTest[mask](x,y)?!dark:dark));
  const saved=modules.map(row=>row.slice());for(let y=0;y<size;y++)modules[y]=candidate[y];
  drawFormat(mask);const score=penalty(modules);
  if(score<bestScore){bestScore=score;best=modules.map(row=>row.slice());}
  for(let y=0;y<size;y++)modules[y]=saved[y];
 }
 return {version:ver,size,modules:best};
}

// Dark modules as one SVG path with a four-module quiet zone.
export function qrSvg(qr,pixels=320){
 const total=qr.size+8;let path='';
 qr.modules.forEach((row,y)=>row.forEach((dark,x)=>{if(dark)path+=`M${x+4} ${y+4}h1v1h-1z`;}));
 return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${total} ${total}" width="${pixels}" height="${pixels}" shape-rendering="crispEdges"><rect width="100%" height="100%" fill="#ffffff"/><path d="${path}" fill="#000000"/></svg>`;
}
