#!/usr/bin/env python3
"""Independent fixed PUBLIC TEST SEED vectors; never use this seed for funds."""
import hashlib,hmac,json,sys
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.serialization import Encoding,PublicFormat
N=0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141
ALPHABET='qpzry9x8gf2tvdw0s3jn54khce6mua7l'
def public(secret):return ec.derive_private_key(secret,ec.SECP256K1()).public_key().public_bytes(Encoding.X962,PublicFormat.CompressedPoint)
def derive(seed,path):
 digest=hmac.new(b'Bitcoin seed',seed,hashlib.sha512).digest();secret=int.from_bytes(digest[:32],'big');chain=digest[32:]
 assert 0<secret<N
 for index in path:
  data=b'\0'+secret.to_bytes(32,'big') if index>=2**31 else public(secret)
  digest=hmac.new(chain,data+index.to_bytes(4,'big'),hashlib.sha512).digest()
  tweak=int.from_bytes(digest[:32],'big');assert tweak<N
  secret=(secret+tweak)%N;assert secret;chain=digest[32:]
 return public(secret)
def address(hrp,key):
 digest=hashlib.new('ripemd160',hashlib.sha256(key).digest()).digest()
 bits=''.join(f'{b:08b}' for b in digest);data=[0]+[int(bits[i:i+5],2) for i in range(0,len(bits),5)]
 values=[ord(c)>>5 for c in hrp]+[0]+[ord(c)&31 for c in hrp]+data+[0]*6
 checksum=1
 for value in values:
  high=checksum>>25;checksum=((checksum&0x1ffffff)<<5)^value
  for i,g in enumerate([0x3b6a57b2,0x26508e6d,0x1ea119fa,0x3d4233dd,0x2a1462b3]):
   if (high>>i)&1:checksum^=g
 checksum^=1
 return hrp+'1'+''.join(ALPHABET[v] for v in data+[(checksum>>(5*(5-i)))&31 for i in range(6)]), '0014'+digest.hex()
# Cross-check the independent derivation/encoding against BIP84's published BTC vector.
mnemonic=' '.join(['abandon']*11+['about'])
seed=hashlib.pbkdf2_hmac('sha512',mnemonic.encode(),b'mnemonic',2048)
key=derive(seed,[2**31+84,2**31,2**31,0,0])
assert key.hex()=='0330d54fd0dd420a6e5f8d3624f5f3482cae350f79d5f0753bf5beef9c2d91af3c'
assert address('bc',key)[0]=='bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu'
def generate(seed=bytes(range(16))):
 vectors=[]
 for network,coin,hrp in [('main',2,'ltc'),('test',1,'tltc'),('regtest',1,'rltc')]:
  for account,branch,index in [(0,0,0),(0,0,1),(0,1,0),(1,1,7)]:
   path=[2**31+84,2**31+coin,2**31+account,branch,index];key=derive(seed,path);encoded,script=address(hrp,key)
   vectors.append(dict(network=network,account=account,branch=branch,index=index,path=f"84'/{coin}'/{account}'/{branch}/{index}",publicKey=key.hex(),address=encoded,script=script))
 return dict(seed=seed.hex(),warning='PUBLIC TEST SEED; never fund these addresses',vectors=vectors)

if __name__=='__main__':
 json.dump(generate(),sys.stdout,indent=2);print()
