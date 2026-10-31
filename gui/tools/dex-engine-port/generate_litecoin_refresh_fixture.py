#!/usr/bin/env python3
"""Fixed synthetic inclusion fixture; not a consensus-valid block/UTXO history.
Uses public account vectors, independent Python serialization/SHA256d/Scrypt.
No node, network access or nonce search. Never fund these public test addresses.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path


def sha256d(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()


def transaction(previous, script, value):
    return (struct.pack('<i', 2) + b'\x01' + previous + struct.pack('<I', 0)
            + b'\x00' + struct.pack('<I', 0xfffffffe) + b'\x01'
            + struct.pack('<Q', value) + bytes([len(script)]) + script + bytes(4))


def generate(vectors, nonces=(1, 0)):
    scripts = [bytes.fromhex(next(item['script'] for item in vectors['vectors']
        if item['network'] == 'regtest' and item['account'] == 0
        and item['branch'] == branch and item['index'] == 0)) for branch in [0, 1]]
    credit = transaction(bytes([9])*32, scripts[0], 100000)
    spend = transaction(sha256d(credit), scripts[1], 60000)
    previous = bytes.fromhex('530827f38f93b43ed12af0b3ad25a288dc02ed74d6d7857862df51fc56c416f9')[::-1]
    blocks = []
    for index, (raw, nonce) in enumerate(zip([credit, spend], nonces)):
        header = (struct.pack('<i', 0x20000000) + previous + sha256d(raw)
            + struct.pack('<III', 1296688752 + index*150, 0x207fffff, nonce))
        proof = hashlib.scrypt(header, salt=header, n=1024, r=1, p=1, dklen=32)
        assert int.from_bytes(proof, 'little') <= (0x7fffff << (8*29))
        previous = sha256d(header)
        blocks.append(dict(height=index+1, header=header.hex(), transaction=raw.hex(),
            txid=sha256d(raw)[::-1].hex(), blockHash=previous[::-1].hex(), workHash=proof[::-1].hex()))
    return dict(warning='PUBLIC TEST SEED; synthetic inclusion, not consensus-valid transactions',
        seed=vectors['seed'], blocks=blocks)


if __name__ == '__main__':
    print(json.dumps(generate(json.loads(Path(sys.argv[1]).read_text())), indent=2))
