"""Independent test-only BIP39 derivation from decrypted disposable vault payloads.

Uses the reference English list and Python's SHA256/PBKDF2; no production keys,
logging, persistence or Rust helper output is involved in deriving expectations.
"""
import hashlib
from pathlib import Path
import unicodedata


def seed_from_payload(payload):
    assert payload and payload[0] in (16, 20, 24, 28, 32)
    size = payload[0]
    assert len(payload) >= size + 3
    length = int.from_bytes(payload[size + 1:size + 3], 'big')
    assert length <= 1024 and len(payload) == size + 3 + length
    entropy = payload[1:size + 1]
    passphrase = payload[size + 3:].decode('utf-8')
    words = Path(__file__).with_name('bip39-english.txt').read_text().splitlines()
    assert len(words) == 2048 and len(set(words)) == 2048
    bits = ''.join(f'{byte:08b}' for byte in entropy)
    bits += f'{hashlib.sha256(entropy).digest()[0]:08b}'[:size // 4]
    phrase = ' '.join(words[int(bits[i:i + 11], 2)] for i in range(0, len(bits), 11))
    return hashlib.pbkdf2_hmac('sha512', phrase.encode(),
                              ('mnemonic' + unicodedata.normalize('NFKD', passphrase)).encode(), 2048, 64)
