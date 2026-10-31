#!/usr/bin/env python3
"""Independent watch-only descriptor for PUBLIC TEST SEED 000102...0f only."""
import hashlib,hmac,json
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.serialization import Encoding,PublicFormat
N=0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141
def public(secret):
 return ec.derive_private_key(secret,ec.SECP256K1()).public_key().public_bytes(Encoding.X962,PublicFormat.CompressedPoint)
def fingerprint(key):return hashlib.new('ripemd160',hashlib.sha256(key).digest()).digest()[:4]
def descriptor(seed=bytes(range(16))):
 digest=hmac.new(b'Bitcoin seed',seed,hashlib.sha512).digest();secret=int.from_bytes(digest[:32],'big');chain=digest[32:]
 master=fingerprint(public(secret))
 for index in [84+2**31,1+2**31,2**31]:
  parent=fingerprint(public(secret));digest=hmac.new(chain,b'\0'+secret.to_bytes(32,'big')+index.to_bytes(4,'big'),hashlib.sha512).digest()
  tweak=int.from_bytes(digest[:32],'big');assert tweak<N;secret=(secret+tweak)%N;assert secret;chain=digest[32:]
 raw=bytes.fromhex('043587cf')+b'\x03'+parent+index.to_bytes(4,'big')+chain+public(secret)
 raw+=hashlib.sha256(hashlib.sha256(raw).digest()).digest()[:4]
 alphabet='123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz';number=int.from_bytes(raw,'big');encoded=''
 while number:number,mod=divmod(number,58);encoded=alphabet[mod]+encoded
 return {'schema':'monzero-ltc-public-account-v1','network':'regtest','account':0,'masterFingerprint':master.hex(),'accountXpub':encoded}
if __name__=='__main__':print(json.dumps(descriptor(),indent=2))
