#!/usr/bin/env python3
"""Create the DEX operator control signing key (run by the operator, offline).

Writes <dir>/control-signing.key (hex seed, mode 0600) and prints the public key
to pass to CMake as -DMONZERO_DEX_CONTROL_PUBKEY=<hex>. Refuses to overwrite.
Keep the signing key offline; anyone holding it can pause or resume new trades
and change limits for every user of builds that pin its public key.
"""
import os, sys
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PrivateFormat, PublicFormat, NoEncryption

def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make-control-key.py <private-directory>")
    directory = sys.argv[1]
    os.makedirs(directory, mode=0o700, exist_ok=True)
    path = os.path.join(directory, "control-signing.key")
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    key = Ed25519PrivateKey.generate()
    seed = key.private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption())
    with os.fdopen(fd, "w") as out:
        out.write(seed.hex())
    print(key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex())

if __name__ == "__main__":
    main()
