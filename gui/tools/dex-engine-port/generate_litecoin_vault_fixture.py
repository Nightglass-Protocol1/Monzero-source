#!/usr/bin/env python3
"""Independent PUBLIC TEST seed encryption using Python Scrypt and libsodium."""
import hashlib,json,uuid
from nacl.bindings import crypto_aead_xchacha20poly1305_ietf_encrypt
seed=bytes(range(32));password=b'PUBLIC TEST PASSWORD ONLY';salt=bytes(range(16));nonce=bytes(range(24))
wallet=uuid.UUID('11111111-2222-4333-8444-555555555555')
genesis='530827f38f93b43ed12af0b3ad25a288dc02ed74d6d7857862df51fc56c416f9'
header=b'MZLTCV01'+genesis.encode()+wallet.bytes+salt+nonce
key=hashlib.scrypt(password,salt=salt,n=2**17,r=8,p=1,dklen=32,maxmem=256*1024*1024)
blob=header+crypto_aead_xchacha20poly1305_ietf_encrypt(seed,header,nonce,key)
print(json.dumps(dict(warning='PUBLIC TEST DATA; never use for funds',wallet=str(wallet),seed=seed.hex(),password=password.decode(),vault=blob.hex()),indent=2))
