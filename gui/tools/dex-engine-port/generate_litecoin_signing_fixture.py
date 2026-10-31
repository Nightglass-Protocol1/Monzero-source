#!/usr/bin/env python3
"""Independent PUBLIC signing fixture. Synthetic history, never fund its addresses.
Fixed header nonces only: no node, network, or nonce search in this generator.
"""
import hashlib,json
from pathlib import Path
from nacl.bindings import crypto_aead_xchacha20poly1305_ietf_encrypt
from generate_litecoin_account_vectors import generate as accounts
from generate_litecoin_refresh_fixture import generate as refresh
from generate_litecoin_watch_descriptor import descriptor

def generate():
 seed=bytes(range(32));vectors=accounts(seed);value=refresh(vectors,(1,3))
 vault=json.loads((Path(__file__).resolve().parents[3]/'build/eigenwallet-engine-port/swap-machine/src/litecoin_light/vault/fixture.json').read_text())
 assert bytes.fromhex(vault['seed'])==seed
 value['account']=descriptor(seed);value['vault']=vault
 value['addresses']=[item for item in vectors['vectors'] if item['network']=='regtest' and item['account']==0 and item['index']==0]
 # An authenticated vault for a different PUBLIC seed, with the same identity/password.
 header=bytes.fromhex(vault['vault'])[:128]
 key=hashlib.scrypt(vault['password'].encode(),salt=header[88:104],n=2**17,r=8,p=1,dklen=32,maxmem=256*1024*1024)
 value['wrongVault']=(header+crypto_aead_xchacha20poly1305_ietf_encrypt(bytes([43])*32,header,header[104:128],key)).hex()
 return value
if __name__=='__main__':print(json.dumps(generate(),indent=2))
